#pragma once

#include "core/Biome.hpp"
#include "core/PerlinNoise.hpp"

#include <cstdint>

// Every noise layer World Generation samples per world column, bundled
// together since they all need to agree on the same seed. Beta 1.7.3-style
// layering: a temperature and a humidity map (both very low frequency, so
// biomes span whole regions rather than flickering block to block) give
// compute_biome_weights() its inputs the same way Beta's own Whittaker-
// diagram table worked, independent of terrain height; a continentalness
// map (even lower frequency) separates dry land from water the same way; a
// coast-roughness map (smaller-scale than continentalness, so it varies a
// few times along one coastline rather than only picking one character for
// the whole coast) then decides, independently of how far offshore we are,
// whether a given stretch of that water is a shallow, calm Sea or jumps
// straight to a deep, "wild" Ocean; Chunk::generate_terrain then blends
// every biome's own height range/amplitude by the resulting weights, so
// crossing a border changes terrain gradually instead of at a seam, and
// adds a river map on top that carves a winding channel through the blend
// independent of all of that.
class TerrainNoise {
public:
    explicit TerrainNoise(uint32_t seed);

    // Multi-octave (fractal) height noise, roughly in [-1, 1] - the same
    // shape as before, just factored out so it can be scaled/offset
    // differently per biome instead of by one fixed amplitude everywhere.
    float height(float world_x, float world_z, int octaves, float persistence = 0.5f) const;

    // How much each biome (including Sea and Ocean) influences a world column -
    // Chunk::generate_terrain blends every biome's own height range/
    // surface blocks by these instead of picking just one, so a border
    // changes gradually rather than at a cliff.
    BiomeWeights biome_weights(float world_x, float world_z) const;

    // dominant_biome(biome_weights(...)) - for the debug overlay, which
    // just wants one name, not a blend.
    Biome biome(float world_x, float world_z) const;

    // Raw river noise, roughly in [-1, 1] - Chunk::generate_terrain carves
    // a channel wherever this is close to 0 (see RIVER_WIDTH there),
    // tracing this noise's zero-contour the way a real river's course
    // winds rather than following a straight or grid-aligned line.
    float river(float world_x, float world_z) const;

    // Small-scale noise, roughly in [-1, 1] - Chunk::generate_terrain turns
    // a patch of underwater sand into clay wherever this crosses above a
    // threshold, the high frequency keeping each patch small ("small
    // chunks of clay"), same idea as real Minecraft's shallow-water clay
    // deposits.
    float clay(float world_x, float world_z) const;

    // Small-scale noise, roughly in [-1, 1] - Chunk::generate_terrain turns
    // a patch of river-bed sand into gravel wherever this crosses above a
    // threshold, same patchy-deposit idea as clay() above, just for a
    // river's own bed instead of a beach.
    float gravel(float world_x, float world_z) const;

private:
    // Wavelength ~900 blocks: biomes need to span whole regions, not
    // flicker chunk to chunk, so this samples much lower frequency than
    // height()'s own detail octaves do.
    static constexpr float BIOME_FREQUENCY = 1.0f / 900.0f;

    // Even lower frequency than biomes - sea/ocean/continents are the
    // largest-scale feature this generates.
    static constexpr float CONTINENT_FREQUENCY = 1.0f / 1800.0f;

    // Higher frequency than biomes, so a river's course actually winds
    // noticeably within one biome region instead of only turning once
    // every biome-sized area.
    static constexpr float RIVER_FREQUENCY = 1.0f / 300.0f;

    // Higher frequency than continentalness, so whether a stretch of coast
    // reads as calm (Sea) or wild (Ocean) changes a few times along one
    // coastline instead of the entire coastline sharing one character.
    static constexpr float COAST_FREQUENCY = 1.0f / 250.0f;

    // Wavelength ~12 blocks - small enough that a patch crossing the clay
    // threshold only spans a handful of blocks, not a whole beach.
    static constexpr float CLAY_FREQUENCY = 1.0f / 12.0f;

    // Same idea as CLAY_FREQUENCY, for gravel patches in a river bed.
    static constexpr float GRAVEL_FREQUENCY = 1.0f / 10.0f;

    PerlinNoise height_noise;
    PerlinNoise temperature_noise;
    PerlinNoise humidity_noise;
    PerlinNoise continent_noise;
    PerlinNoise river_noise;
    PerlinNoise coast_noise;
    PerlinNoise clay_noise;
    PerlinNoise gravel_noise;
};
