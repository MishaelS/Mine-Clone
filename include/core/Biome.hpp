#pragma once

#include <cstdint>
#include <string>

// A handful of biomes in the spirit of Minecraft Beta 1.7.3's original
// biome set (Plains/Forest/Desert/Ocean, plus a rockier highland type
// standing in for Beta's Taiga/hillier regions) - trimmed to what this
// project actually has matching blocks for for now (no snow, no jungle
// wood, ...).
enum class Biome : uint8_t {
    Plains,
    Forest,
    Desert,
    Hills,
    Ocean,
    Sea,
};

// How much each biome influences a given world column, each in [0, 1] and
// summing to 1 - a soft partition rather than classify_biome()'s old hard
// pick, so terrain generation can *blend* every biome's own height range
// across a border (see Chunk::generate_terrain) instead of the two sides
// meeting at a cliff. dominant_biome() collapses this back into one Biome,
// for whatever (display, surface block choice) can't be blended the same
// way. Sea and Ocean are two bands of the same land-vs-water axis
// (continentalness) rather than independent - Sea is the shallow, calm band
// close to shore, Ocean the deep band far from land.
struct BiomeWeights {
    float plains, forest, desert, hills, ocean, sea;
};

// temperature/humidity: Beta 1.7.3's own Whittaker-diagram-style axes,
// roughly in [-1, 1] (see TerrainNoise). continentalness: large-scale
// land-vs-water noise, also roughly [-1, 1] - very negative means far out
// to sea, positive means dry land, independent of temperature/humidity
// (water exists at every temperature). coast_roughness: an independent,
// smaller-scale [-1, 1] noise deciding whether a given stretch of
// coastline is calm (buffered by a shallow Sea band before the water gets
// deep) or "wild" (jumps straight to deep Ocean right at the shore) - see
// its use below for why continentalness alone can't tell those apart.
BiomeWeights compute_biome_weights(float temperature, float humidity, float continentalness, float coast_roughness);

// The single biome with the largest weight - for the debug overlay, and
// for anything else (surface block choice) that fundamentally can't be a
// blend of two biomes at once.
Biome dominant_biome(const BiomeWeights& weights);

// Display name for the debug overlay.
const std::string& get_biome_name(Biome biome);
