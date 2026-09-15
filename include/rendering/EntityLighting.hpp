#pragma once

#include "world/World.hpp"

#include <algorithm>
#include <cmath>

// Set once per frame by GameEngine::draw() - DayNightCycle::
// sky_light_factor(game_tick), the exact same value fed to the chunk
// mesh's own "daylightFactor" shader uniform (see Chunk.hpp's
// set_chunk_daylight()) - 1.0 at full day, DayNightCycle::
// MIN_NIGHT_SKY_LIGHT_FACTOR at full night. entity_environment_tint()
// below multiplies it into the same sky/block combine the chunk shader
// does, so a dropped item/particle/the player's own model darken at night
// in step with the terrain right next to them, not just the chunk mesh.
// A plain global (an `inline` variable - one shared definition across
// every translation unit that includes this header, C++17) rather than a
// parameter threaded through every entity renderer's own draw() call,
// mirroring the same "GameEngine sets it once per frame, everything else
// reads it implicitly" shape the chunk shader's own uniforms already use -
// just on the CPU side instead of the GPU's.
inline float g_entity_sky_light_factor = 1.0f;

inline void set_entity_daylight_factor(float sky_light_factor)
{
    g_entity_sky_light_factor = sky_light_factor;
}

// Settings > Graphics' brightness slider, mirroring Chunk.hpp's
// set_chunk_brightness() for dynamic entities - a gamma exponent applied
// (pow(light, gamma)) to the sky term ONLY, below - never to block light,
// so a dropped item sitting right next to a torch stays exactly as bright
// regardless of the slider, the same as a chunk face does (see chunk.fs's
// own comment for why block light is fully exempt, not just at its own
// maximum). 1.0 is the slider's own max (a no-op).
inline float g_entity_brightness_gamma = 1.0f;

inline void set_entity_brightness_factor(float gamma)
{
    g_entity_brightness_gamma = gamma;
}

// Trilinear light sample at an arbitrary point, not just a whole block cell
// - the same "smooth lighting" idea Chunk::build_mesh()'s own vertex_light()
// applies to chunk mesh faces (averaging the light of the cells touching a
// vertex), just evaluated at any position instead of only at block corners.
// Without this, an entity crossing a block boundary would visibly snap
// between two light levels instead of fading, since world.get_light() alone
// only ever reports one flat value per whole cell. Light values are treated
// as living at block *centers* (x+0.5, y+0.5, z+0.5), matching how a block's
// own face brightness is anchored, so the interpolation lines up with the
// terrain mesh's own lighting instead of reading half a block offset.
//
// Sky and block light are sampled (and interpolated) as two separate
// channels, same as vertex_light() does for chunk mesh faces, so only the
// sky channel ever gets g_entity_sky_light_factor applied - block light
// (torches, lava) reaches full strength on an entity exactly as it always
// has, day or night.
inline float sample_light_smooth(const World& world, Vector3 position)
{
    Vector3 p = {position.x - 0.5f, position.y - 0.5f, position.z - 0.5f};
    const int bx = static_cast<int>(std::floor(p.x));
    const int by = static_cast<int>(std::floor(p.y));
    const int bz = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(bx);
    const float fy = p.y - static_cast<float>(by);
    const float fz = p.z - static_cast<float>(bz);

    auto sample_sky = [&](int dx, int dy, int dz) {
        return static_cast<float>(world.get_sky_light(bx + dx, by + dy, bz + dz));
    };
    auto sample_block = [&](int dx, int dy, int dz) {
        return static_cast<float>(world.get_block_light(bx + dx, by + dy, bz + dz));
    };

    const float sky_x0z0 = sample_sky(0, 0, 0) * (1.0f - fx) + sample_sky(1, 0, 0) * fx;
    const float sky_x0z1 = sample_sky(0, 0, 1) * (1.0f - fx) + sample_sky(1, 0, 1) * fx;
    const float sky_x1z0 = sample_sky(0, 1, 0) * (1.0f - fx) + sample_sky(1, 1, 0) * fx;
    const float sky_x1z1 = sample_sky(0, 1, 1) * (1.0f - fx) + sample_sky(1, 1, 1) * fx;
    const float sky_y0 = sky_x0z0 * (1.0f - fz) + sky_x0z1 * fz;
    const float sky_y1 = sky_x1z0 * (1.0f - fz) + sky_x1z1 * fz;
    const float sky = sky_y0 * (1.0f - fy) + sky_y1 * fy;

    const float block_x0z0 = sample_block(0, 0, 0) * (1.0f - fx) + sample_block(1, 0, 0) * fx;
    const float block_x0z1 = sample_block(0, 0, 1) * (1.0f - fx) + sample_block(1, 0, 1) * fx;
    const float block_x1z0 = sample_block(0, 1, 0) * (1.0f - fx) + sample_block(1, 1, 0) * fx;
    const float block_x1z1 = sample_block(0, 1, 1) * (1.0f - fx) + sample_block(1, 1, 1) * fx;
    const float block_y0 = block_x0z0 * (1.0f - fz) + block_x0z1 * fz;
    const float block_y1 = block_x1z0 * (1.0f - fz) + block_x1z1 * fz;
    const float block = block_y0 * (1.0f - fy) + block_y1 * fy;

    // Raw sky/block fractions (no floor yet - see chunk.fs's own comment on
    // why it's applied last, to the max of both terms, instead of baked
    // into each one this early). Day/night and the brightness slider's own
    // gamma both apply to the sky term only; block light (torches, lava)
    // passes through untouched by either, so it reaches an entity exactly
    // as strong as it always has.
    const float sky_fraction = sky / static_cast<float>(MAX_LIGHT);
    const float block_fraction = block / static_cast<float>(MAX_LIGHT);
    const float sky_term = std::pow(std::clamp(sky_fraction * g_entity_sky_light_factor, 0.0f, 1.0f), g_entity_brightness_gamma);
    return std::max(MIN_LIGHT_FRACTION, std::max(block_fraction, sky_term));
}

// Dynamic geometry is not part of a chunk mesh, so it cannot inherit the
// per-vertex light baked by Chunk::build_mesh().  Sample the same world light
// field explicitly (smoothly - see sample_light_smooth() above) and return a
// vertex tint that every entity renderer can multiply into its own texture
// colour. Shares MIN_LIGHT_FRACTION with the chunk mesh's own faces so a
// dropped item never reads brighter than the genuinely-dark terrain sitting
// right next to it.
inline Color entity_environment_tint(const World& world, Vector3 position)
{
    const int x = static_cast<int>(std::floor(position.x));
    const int y = static_cast<int>(std::floor(position.y));
    const int z = static_cast<int>(std::floor(position.z));
    const float light = sample_light_smooth(world, position);

    // The shared fog shader handles distance through water from the camera;
    // this local colour handles partial submersion when the camera itself is
    // above the surface. Only body parts/particles actually inside a water
    // cell receive this attenuation.
    const bool submerged = world.get_block(x, y, z) == BlockType::Water;
    const float red   = light * (submerged ? 0.48f : 1.0f);
    const float green = light * (submerged ? 0.72f : 1.0f);
    const float blue  = light * (submerged ? 0.88f : 1.0f);
    return {
        static_cast<unsigned char>(255.0f * std::clamp(red,   0.0f, 1.0f)),
        static_cast<unsigned char>(255.0f * std::clamp(green, 0.0f, 1.0f)),
        static_cast<unsigned char>(255.0f * std::clamp(blue,  0.0f, 1.0f)),
        255,
    };
}

inline Color multiply_tint(Color texture_tint, Color environment_tint)
{
    return {
        static_cast<unsigned char>((static_cast<unsigned int>(texture_tint.r) * environment_tint.r) / 255u),
        static_cast<unsigned char>((static_cast<unsigned int>(texture_tint.g) * environment_tint.g) / 255u),
        static_cast<unsigned char>((static_cast<unsigned int>(texture_tint.b) * environment_tint.b) / 255u),
        texture_tint.a,
    };
}
