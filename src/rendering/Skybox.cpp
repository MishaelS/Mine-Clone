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
    constexpr Color NIGHT_SKY_COLOR = {3, 5, 14, 255};
    constexpr Color NIGHT_HORIZON_COLOR = {7, 9, 22, 255};

    // The warm horizon band real Minecraft shows right at sunrise/sunset -
    // blended in only near the horizon (see draw_skybox()'s own glow
    // calculation), not the whole sky, and only when the sun is actually
    // near the horizon line.
    constexpr Color SUNSET_HORIZON_COLOR = {255, 126, 48, 255};
    constexpr Color SUNSET_SKY_COLOR = {104, 62, 132, 255};
    constexpr float SUNSET_HORIZON_STRENGTH = 0.78f; // how much the glow color takes over at its strongest, 0..1
    constexpr float SUNSET_SKY_STRENGTH = 0.18f;

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

void draw_skybox(Vector3 camera_position, float celestial_angle)
{
    // Minecraft-style celestial curve: with DayNightCycle::celestial_angle()
    // 0 is noon and 0.5 is midnight, so cos(angle * 2PI) gives sun height
    // directly. Mapping that through a smooth threshold keeps true night
    // dark, instead of blending halfway toward day exactly at the horizon.
    float sun_height = std::cos(celestial_angle * 2.0f * PI);
    float brightness = std::clamp(sun_height * 0.5f + 0.5f, 0.0f, 1.0f);
    float day_factor = std::clamp((brightness - 0.35f) / 0.60f, 0.0f, 1.0f);
    day_factor = day_factor * day_factor * (3.0f - 2.0f * day_factor); // smoothstep
    Color sky = ColorLerp(NIGHT_SKY_COLOR, DAY_SKY_COLOR, day_factor);
    Color horizon = ColorLerp(NIGHT_HORIZON_COLOR, DAY_HORIZON_COLOR, day_factor);

    // Sunrise/sunset glow: strongest exactly when the cosine-derived sun
    // height crosses the horizon and gone shortly after. It paints mostly
    // the horizon, with a much weaker purple lift overhead so the sky has
    // an actual sunset gradient rather than one flat orange strip.
    float glow = std::clamp(1.0f - std::fabs(sun_height) * 4.8f, 0.0f, 1.0f);
    glow = glow * glow * (3.0f - 2.0f * glow);
    horizon = ColorLerp(horizon, SUNSET_HORIZON_COLOR, glow * SUNSET_HORIZON_STRENGTH);
    sky = ColorLerp(sky, SUNSET_SKY_COLOR, glow * SUNSET_SKY_STRENGTH);

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
