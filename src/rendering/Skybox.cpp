#include "rendering/Skybox.hpp"
#include "core/TextureManager.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

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

    constexpr uint32_t STAR_SALT = 0x53544152u;  // "STAR"
    constexpr uint32_t CLOUD_SALT = 0x434C4453u; // "CLDS"
    constexpr int STAR_COUNT = 720;
    constexpr float STAR_DISTANCE = 420.0f;
    constexpr float CLOUD_Y = 160.0f;
    constexpr float CLOUD_CELL = 12.0f;
    constexpr float CLOUD_EXTRA_RANGE = 48.0f;
    constexpr float CLOUD_MIN_RANGE = 144.0f;
    constexpr float CLOUD_LAYER_DROP = 1.15f;
    constexpr float CLOUD_SPEED_BLOCKS_PER_TICK = 0.018f;
    constexpr float CLOUD_NOISE_SCALE = 0.065f;
    constexpr float CLOUD_THRESHOLD = 0.56f;

    struct Star {
        Vector3 direction{};
        float size = 1.0f;
        unsigned char alpha = 255;
    };

    std::vector<Star> cached_stars;
    uint32_t cached_star_seed = std::numeric_limits<uint32_t>::max();

    uint32_t mix32(uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    float sky_noise01(uint32_t seed, int x, int z, uint32_t salt)
    {
        uint32_t h = seed ^ salt;
        h ^= static_cast<uint32_t>(x) * 0x9E3779B9u;
        h ^= static_cast<uint32_t>(z) * 0x85EBCA6Bu;
        return static_cast<float>(mix32(h)) / static_cast<float>(std::numeric_limits<uint32_t>::max());
    }

    float smooth(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    float value_noise(uint32_t seed, float x, float z, uint32_t salt)
    {
        int x0 = static_cast<int>(std::floor(x));
        int z0 = static_cast<int>(std::floor(z));
        float tx = smooth(x - static_cast<float>(x0));
        float tz = smooth(z - static_cast<float>(z0));

        float a = sky_noise01(seed, x0,     z0,     salt);
        float b = sky_noise01(seed, x0 + 1, z0,     salt);
        float c = sky_noise01(seed, x0,     z0 + 1, salt);
        float d = sky_noise01(seed, x0 + 1, z0 + 1, salt);
        float ab = a + (b - a) * tx;
        float cd = c + (d - c) * tx;
        return ab + (cd - ab) * tz;
    }

    float sky_fbm(uint32_t seed, float x, float z, uint32_t salt)
    {
        float sum = 0.0f;
        float amplitude = 0.55f;
        float total = 0.0f;
        float frequency = 1.0f;
        for (int octave = 0; octave < 4; ++octave) {
            sum += value_noise(seed, x * frequency, z * frequency, salt + static_cast<uint32_t>(octave) * 1013u) * amplitude;
            total += amplitude;
            amplitude *= 0.52f;
            frequency *= 2.0f;
        }
        return total > 0.0f ? sum / total : 0.0f;
    }

    float night_visibility(float celestial_angle)
    {
        float sun_height = std::cos(celestial_angle * 2.0f * PI);
        float t = std::clamp((-sun_height - 0.05f) / 0.45f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float day_visibility(float celestial_angle)
    {
        float sun_height = std::cos(celestial_angle * 2.0f * PI);
        float t = std::clamp((sun_height + 0.1f) / 0.65f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void rebuild_stars(uint32_t seed)
    {
        if (cached_star_seed == seed) return;
        cached_star_seed = seed;
        cached_stars.clear();
        cached_stars.reserve(STAR_COUNT);

        for (int i = 0; i < STAR_COUNT; ++i) {
            float azimuth = sky_noise01(seed, i, 0, STAR_SALT) * 2.0f * PI;
            float y = 0.10f + sky_noise01(seed, i, 1, STAR_SALT) * 0.88f;
            float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
            Vector3 direction{
                std::cos(azimuth) * radius,
                y,
                std::sin(azimuth) * radius,
            };
            float brightness = 0.45f + sky_noise01(seed, i, 2, STAR_SALT) * 0.55f;
            float size = 0.45f + sky_noise01(seed, i, 3, STAR_SALT) * 0.85f;
            cached_stars.push_back(Star{
                direction,
                size,
                static_cast<unsigned char>(std::round(150.0f + brightness * 105.0f)),
            });
        }
    }

    void draw_star_quad(Vector3 camera_position, const Star& star, float visibility)
    {
        unsigned char alpha = static_cast<unsigned char>(std::round(static_cast<float>(star.alpha) * visibility));
        if (alpha == 0) return;

        Vector3 center = Vector3Add(camera_position, Vector3Scale(star.direction, STAR_DISTANCE));
        Vector3 right = Vector3CrossProduct({0.0f, 1.0f, 0.0f}, star.direction);
        if (Vector3LengthSqr(right) < 0.0001f) right = {1.0f, 0.0f, 0.0f};
        right = Vector3Scale(Vector3Normalize(right), star.size * 0.5f);
        Vector3 up = Vector3Scale(Vector3Normalize(Vector3CrossProduct(star.direction, right)), star.size * 0.5f);

        Vector3 p0 = Vector3Subtract(Vector3Subtract(center, right), up);
        Vector3 p1 = Vector3Add(Vector3Subtract(center, up), right);
        Vector3 p2 = Vector3Add(Vector3Add(center, right), up);
        Vector3 p3 = Vector3Add(Vector3Subtract(center, right), up);
        rlColor4ub(255, 255, 255, alpha);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }

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

void draw_seeded_stars(Vector3 camera_position, uint32_t world_seed, float celestial_angle)
{
    float visibility = night_visibility(celestial_angle);
    if (visibility <= 0.01f) return;

    rebuild_stars(world_seed);

    rlSetTexture(0);
    rlDisableBackfaceCulling();
    rlDisableDepthTest();

    rlBegin(RL_QUADS);
    for (const Star& star : cached_stars) {
        draw_star_quad(camera_position, star, visibility);
    }
    rlEnd();

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableBackfaceCulling();
}

void draw_seeded_clouds(Vector3 camera_position, uint32_t world_seed, uint64_t game_tick, float celestial_angle,
                        int render_distance_blocks, int cloud_volume)
{
    float day = day_visibility(celestial_angle);
    float tick_offset = static_cast<float>(game_tick) * CLOUD_SPEED_BLOCKS_PER_TICK;
    float cloud_range = std::max(CLOUD_MIN_RANGE, static_cast<float>(render_distance_blocks) + CLOUD_EXTRA_RANGE);
    int layers = std::clamp(cloud_volume, 1, 5);
    int min_x = static_cast<int>(std::floor((camera_position.x + tick_offset - cloud_range) / CLOUD_CELL));
    int max_x = static_cast<int>(std::floor((camera_position.x + tick_offset + cloud_range) / CLOUD_CELL));
    int min_z = static_cast<int>(std::floor((camera_position.z - cloud_range) / CLOUD_CELL));
    int max_z = static_cast<int>(std::floor((camera_position.z + cloud_range) / CLOUD_CELL));

    unsigned char base_alpha = static_cast<unsigned char>(std::round(72.0f + day * 76.0f));
    Color shadow_color{
        static_cast<unsigned char>(145 + static_cast<int>(day * 55.0f)),
        static_cast<unsigned char>(148 + static_cast<int>(day * 55.0f)),
        static_cast<unsigned char>(154 + static_cast<int>(day * 55.0f)),
        static_cast<unsigned char>(base_alpha * 0.72f),
    };
    Color top_color{
        static_cast<unsigned char>(205 + static_cast<int>(day * 50.0f)),
        static_cast<unsigned char>(210 + static_cast<int>(day * 45.0f)),
        static_cast<unsigned char>(215 + static_cast<int>(day * 40.0f)),
        base_alpha,
    };

    rlSetTexture(0);
    rlDisableBackfaceCulling();
    rlDisableDepthTest();

    rlBegin(RL_QUADS);
    for (int z = min_z; z <= max_z; ++z) {
        for (int x = min_x; x <= max_x; ++x) {
            float density = sky_fbm(world_seed,
                                    static_cast<float>(x) * CLOUD_NOISE_SCALE,
                                    static_cast<float>(z) * CLOUD_NOISE_SCALE,
                                    CLOUD_SALT);
            if (density < CLOUD_THRESHOLD) continue;

            float strength = std::clamp((density - CLOUD_THRESHOLD) / (1.0f - CLOUD_THRESHOLD), 0.0f, 1.0f);
            unsigned char alpha = static_cast<unsigned char>(std::round(static_cast<float>(top_color.a) * (0.45f + strength * 0.55f)));
            unsigned char shadow_alpha = static_cast<unsigned char>(std::round(static_cast<float>(shadow_color.a) * (0.45f + strength * 0.55f)));

            float x0 = static_cast<float>(x) * CLOUD_CELL - tick_offset;
            float z0 = static_cast<float>(z) * CLOUD_CELL;
            float x1 = x0 + CLOUD_CELL;
            float z1 = z0 + CLOUD_CELL;
            float top_y = CLOUD_Y;

            for (int layer = layers - 1; layer >= 1; --layer) {
                float y = top_y - static_cast<float>(layer) * CLOUD_LAYER_DROP;
                float layer_fade = 1.0f - static_cast<float>(layer) / static_cast<float>(layers) * 0.42f;
                unsigned char layer_alpha = static_cast<unsigned char>(std::round(static_cast<float>(shadow_alpha) * layer_fade));
                rlColor4ub(shadow_color.r, shadow_color.g, shadow_color.b, layer_alpha);
                rlVertex3f(x0, y, z1);
                rlVertex3f(x1, y, z1);
                rlVertex3f(x1, y, z0);
                rlVertex3f(x0, y, z0);
            }

            rlColor4ub(top_color.r, top_color.g, top_color.b, alpha);
            rlVertex3f(x0, top_y, z0);
            rlVertex3f(x1, top_y, z0);
            rlVertex3f(x1, top_y, z1);
            rlVertex3f(x0, top_y, z1);
        }
    }
    rlEnd();

    rlDrawRenderBatchActive();
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
