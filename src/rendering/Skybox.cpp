#include "rendering/Skybox.hpp"
#include "core/TextureManager.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    // Half-extent of the cube; comfortably inside raylib's default far clip
    // plane (1000 units) so it never gets clipped away.
    constexpr float SIZE = 500.0f;

    // Day's own look - the flat colors this whole gradient used to be,
    // unconditionally, before day/night existed.
    constexpr Color DAY_SKY_COLOR = {135, 190, 235, 255};
    constexpr Color DAY_HORIZON_COLOR = {215, 235, 245, 255};

    // Night's own look - a dark, slightly blue-tinted sky rather than pure
    // black, same "still legible, not a void" spirit as MIN_LIGHT_FRACTION
    // keeps terrain from ever reading as flat black either.
    constexpr Color NIGHT_SKY_COLOR = {8, 10, 26, 255};
    constexpr Color NIGHT_HORIZON_COLOR = {20, 22, 40, 255};

    // The warm horizon band real Minecraft shows right at sunrise/sunset -
    // blended in only near the horizon (see draw_skybox()'s own glow
    // calculation), not the whole sky, and only when the sun is actually
    // near the horizon line.
    constexpr Color SUNSET_HORIZON_COLOR = {255, 130, 60, 255};
    constexpr float SUNSET_GLOW_STRENGTH = 0.55f; // how much the glow color takes over at its strongest, 0..1

    // Current, time-of-day-blended colors - what draw_skybox() actually
    // draws with this frame, recomputed by it every call (see its own
    // sun_direction parameter) and read back by skybox_horizon_color()/
    // skybox_sky_color() for anything else that needs to match (World's
    // own distance fog - see set_chunk_fog/chunk.fs). Initialized to day's
    // own look for the vanishingly unlikely case something reads them
    // before the first draw_skybox() call this session.
    Color current_sky_color = DAY_SKY_COLOR;
    Color current_horizon_color = DAY_HORIZON_COLOR;

    void Vertex(Vector3 center, float x, float y, float z, Color color) {
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(center.x + x, center.y + y, center.z + z);
    }

    const char* SUN_TEXTURE_PATH = "sprites/sky/sun.png";
    const char* MOON_TEXTURE_PATH = "sprites/sky/moon.png";

    // Comfortably inside SIZE above, so the billboards read as sitting just
    // in front of the sky cube's own gradient rather than behind it.
    constexpr float CELESTIAL_DISTANCE = 400.0f;
    // 1.5x the original 60/46 - visual size only, doesn't touch
    // CELESTIAL_DISTANCE or DayNightCycle's own orbit/position math at all.
    constexpr float SUN_SIZE = 90.0f;
    constexpr float MOON_SIZE = 69.0f; // vanilla's own moon reads a bit smaller/dimmer than its sun

    // A flat quad centered `CELESTIAL_DISTANCE` units from the camera along
    // `direction`, facing back toward it. DayNightCycle::sun_direction()
    // only ever varies within the X-Y plane (world +Z component always
    // exactly 0), so world +Z is guaranteed perpendicular to `direction`
    // here - a plain cross product against it is enough to build the
    // quad's own "up" axis without a degenerate case (direction pointing
    // straight along +Z/-Z) to special-case the way a general-purpose
    // billboard helper would need to.
    void draw_celestial_quad(Vector3 camera_position, Vector3 direction, const Texture2D& texture, float size)
    {
        Vector3 center = Vector3Add(camera_position, Vector3Scale(direction, CELESTIAL_DISTANCE));
        constexpr Vector3 WORLD_Z = {0.0f, 0.0f, 1.0f};
        Vector3 right = Vector3Scale(WORLD_Z, size * 0.5f);
        Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(WORLD_Z, direction)), size * 0.5f);

        Vector3 top_left     = Vector3Subtract(Vector3Add(center, up), right);
        Vector3 top_right    = Vector3Add(Vector3Add(center, up), right);
        Vector3 bottom_right = Vector3Subtract(Vector3Add(center, right), up);
        Vector3 bottom_left  = Vector3Subtract(Vector3Subtract(center, right), up);

        rlSetTexture(texture.id);
        rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(bottom_left.x, bottom_left.y, bottom_left.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(bottom_right.x, bottom_right.y, bottom_right.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(top_right.x, top_right.y, top_right.z);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(top_left.x, top_left.y, top_left.z);
        rlEnd();
        rlSetTexture(0);
    }
}

void draw_skybox(Vector3 camera_position, Vector3 sun_direction)
{
    // Blend day's own look toward night's, following the sun's own height
    // above the horizon (sun_direction.y) - the same value DayNightCycle::
    // sky_light_factor() itself follows for the actual light level, just
    // remapped to 0..1 first (that one only cares about "day or not", this
    // one needs the full night<->day range) and smoothstepped so the
    // transition eases in/out around dawn/dusk rather than moving at a
    // constant rate the whole cycle through.
    float day_factor = std::clamp((sun_direction.y + 1.0f) * 0.5f, 0.0f, 1.0f);
    day_factor = day_factor * day_factor * (3.0f - 2.0f * day_factor); // smoothstep
    Color sky = ColorLerp(NIGHT_SKY_COLOR, DAY_SKY_COLOR, day_factor);
    Color horizon = ColorLerp(NIGHT_HORIZON_COLOR, DAY_HORIZON_COLOR, day_factor);

    // Sunrise/sunset glow: strongest exactly at the horizon (sun_direction.y
    // == 0) and fades out within a fairly narrow band either side of it, so
    // it reads as a brief, distinct event rather than half the day/night
    // cycle. Only tints the horizon color - the zenith stays whatever
    // night/day blend it already was, same as real Minecraft's own glow
    // never actually reaching straight up.
    float glow = std::clamp(1.0f - std::fabs(sun_direction.y) * 5.0f, 0.0f, 1.0f);
    horizon = ColorLerp(horizon, SUNSET_HORIZON_COLOR, glow * SUNSET_GLOW_STRENGTH);

    current_sky_color = sky;
    current_horizon_color = horizon;

    rlSetTexture(0);
    rlDisableBackfaceCulling(); // the camera sits inside this cube
    rlDisableDepthTest();       // always render behind everything else

    rlBegin(RL_QUADS);
        // Top
        Vertex(camera_position, -SIZE, SIZE, -SIZE, sky);
        Vertex(camera_position, -SIZE, SIZE,  SIZE, sky);
        Vertex(camera_position,  SIZE, SIZE,  SIZE, sky);
        Vertex(camera_position,  SIZE, SIZE, -SIZE, sky);

        // Bottom
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, horizon);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, horizon);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, horizon);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, horizon);

        // North (-Z), sky at the top edge fading to horizon at the bottom edge
        Vertex(camera_position, -SIZE,  SIZE, -SIZE, sky);
        Vertex(camera_position,  SIZE,  SIZE, -SIZE, sky);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, horizon);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, horizon);

        // South (+Z)
        Vertex(camera_position,  SIZE,  SIZE,  SIZE, sky);
        Vertex(camera_position, -SIZE,  SIZE,  SIZE, sky);
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, horizon);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, horizon);

        // East (+X)
        Vertex(camera_position,  SIZE,  SIZE, -SIZE, sky);
        Vertex(camera_position,  SIZE,  SIZE,  SIZE, sky);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, horizon);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, horizon);

        // West (-X)
        Vertex(camera_position, -SIZE,  SIZE,  SIZE, sky);
        Vertex(camera_position, -SIZE,  SIZE, -SIZE, sky);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, horizon);
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, horizon);
    rlEnd();

    // rlEnableDepthTest/rlEnableBackfaceCulling below flip GL state
    // immediately, but rlEnd() doesn't flush these quads to the GPU by
    // itself - without this, they'd get rasterized later (whenever the
    // batch actually flushes) with depth test and culling back on, and
    // vanish: back-face culling would discard them since the camera sits
    // inside the cube, facing their back side.
    rlDrawRenderBatchActive();

    rlEnableDepthTest();
    rlEnableBackfaceCulling();
}

void draw_celestial_bodies(Vector3 camera_position, Vector3 sun_direction)
{
    // Same self-contained "always infinitely far away" GL state as
    // draw_skybox() above - disabling backface culling rather than getting
    // each quad's winding order exactly right is simplest given the
    // billboard's own orientation continuously changes through the day.
    rlDisableBackfaceCulling();
    rlDisableDepthTest();

    draw_celestial_quad(camera_position, sun_direction, TextureManager::get(SUN_TEXTURE_PATH), SUN_SIZE);
    draw_celestial_quad(camera_position, Vector3Negate(sun_direction), TextureManager::get(MOON_TEXTURE_PATH), MOON_SIZE);

    rlDrawRenderBatchActive(); // see draw_skybox()'s own comment on why this can't wait for the batch's next natural flush
    rlEnableDepthTest();
    rlEnableBackfaceCulling();
}

Color skybox_horizon_color()
{
    return current_horizon_color;
}

Color skybox_sky_color()
{
    return current_sky_color;
}
