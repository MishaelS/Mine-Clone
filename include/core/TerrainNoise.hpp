#pragma once

#include "core/Biome.hpp"
#include "core/PerlinNoise.hpp"

#include <cstdint>

// Every noise layer World Generation samples per world column, bundled
// together since they all need to agree on the same seed. Beta 1.7.3-style
// layering: a temperature and a humidity map (both very low frequency, so
// biomes span whole regions rather than flickering block to block) select
// a Biome the same way Beta's own Whittaker-diagram table did, independent
// of terrain height; Chunk::generate_terrain then samples height() with
// that biome's own base height/amplitude, so each biome gets its own
// characteristic terrain in addition to its own surface blocks.
class TerrainNoise {
public:
    explicit TerrainNoise(uint32_t seed);

    // Multi-octave (fractal) height noise, roughly in [-1, 1] — the same
    // shape as before, just factored out so it can be scaled/offset
    // differently per biome instead of by one fixed amplitude everywhere.
    float height(float world_x, float world_z, int octaves, float persistence = 0.5f) const;

    // Which biome a world column falls in — Chunk::generate_terrain uses
    // this to pick that biome's height range and surface blocks; World
    // exposes the same classification (World::get_biome) for the debug
    // overlay.
    Biome biome(float world_x, float world_z) const;

private:
    // Wavelength ~600 blocks: biomes need to span whole regions, not
    // flicker chunk to chunk, so this samples much lower frequency than
    // height()'s own detail octaves do.
    static constexpr float BIOME_FREQUENCY = 1.0f / 600.0f;

    PerlinNoise height_noise;
    PerlinNoise temperature_noise;
    PerlinNoise humidity_noise;
};
