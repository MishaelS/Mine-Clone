#include "core/Biome.hpp"

namespace {
    std::string BIOME_NAMES[] = {
        "Plains",
        "Forest",
        "Desert",
        "Hills",
    };
}

Biome classify_biome(float temperature, float humidity)
{
    // Cold regions read as rocky highlands regardless of humidity — Beta
    // 1.7.3's own table treats its coldest band (Tundra/Taiga) the same
    // way, as a temperature cutoff on its own.
    if (temperature < -0.3f) {
        return Biome::Hills;
    }
    // Hot and dry.
    if (temperature > 0.4f && humidity < -0.1f) {
        return Biome::Desert;
    }
    // Everything else that's wet enough reads as forest; the rest is the
    // default, gentlest biome.
    if (humidity > 0.3f) {
        return Biome::Forest;
    }
    return Biome::Plains;
}

const std::string& get_biome_name(Biome biome)
{
    return BIOME_NAMES[static_cast<uint8_t>(biome)];
}
