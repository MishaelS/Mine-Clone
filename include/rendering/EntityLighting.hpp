#pragma once

#include "world/World.hpp"

#include <algorithm>
#include <cmath>

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
inline float sample_light_smooth(const World& world, Vector3 position)
{
    Vector3 p = {position.x - 0.5f, position.y - 0.5f, position.z - 0.5f};
    const int bx = static_cast<int>(std::floor(p.x));
    const int by = static_cast<int>(std::floor(p.y));
    const int bz = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(bx);
    const float fy = p.y - static_cast<float>(by);
    const float fz = p.z - static_cast<float>(bz);

    auto sample = [&](int dx, int dy, int dz) {
        return static_cast<float>(world.get_light(bx + dx, by + dy, bz + dz));
    };

    const float x0z0 = sample(0, 0, 0) * (1.0f - fx) + sample(1, 0, 0) * fx;
    const float x0z1 = sample(0, 0, 1) * (1.0f - fx) + sample(1, 0, 1) * fx;
    const float x1z0 = sample(0, 1, 0) * (1.0f - fx) + sample(1, 1, 0) * fx;
    const float x1z1 = sample(0, 1, 1) * (1.0f - fx) + sample(1, 1, 1) * fx;
    const float y0 = x0z0 * (1.0f - fz) + x0z1 * fz;
    const float y1 = x1z0 * (1.0f - fz) + x1z1 * fz;
    const float light = y0 * (1.0f - fy) + y1 * fy;

    return std::max(MIN_LIGHT_FRACTION, light / static_cast<float>(MAX_LIGHT));
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
