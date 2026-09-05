#pragma once

#include <cstdint>
#include <string>

// A handful of biomes in the spirit of Minecraft Beta 1.7.3's original
// biome set (Plains/Forest/Desert, plus a rockier highland type standing in
// for Beta's Taiga/hillier regions) — trimmed to what this project actually
// has matching blocks for for now (no snow, no jungle wood, ...).
enum class Biome : uint8_t {
    Plains,
    Forest,
    Desert,
    Hills,
};

// Classifies a biome the same way Beta 1.7.3 did: a lookup on a
// temperature/humidity pair rather than the terrain height itself, so a
// biome is a property of *where* you are, independent of how tall the
// ground happens to be there. Both roughly in [-1, 1] (see TerrainNoise).
Biome classify_biome(float temperature, float humidity);

// Display name for the debug overlay.
const std::string& get_biome_name(Biome biome);
