#include "core/TerrainNoise.hpp"

TerrainNoise::TerrainNoise(uint32_t seed)
    // Distinct seeds so the three layers don't sample identical patterns —
    // small, deterministic offsets are enough: FastNoise2's permutation
    // table already looks completely different for any two distinct seeds,
    // however close together.
    : height_noise(seed)
    , temperature_noise(seed + 1)
    , humidity_noise(seed + 2)
{
}

float TerrainNoise::height(float world_x, float world_z, int octaves, float persistence) const
{
    return height_noise.fractal(world_x, world_z, octaves, persistence);
}

Biome TerrainNoise::biome(float world_x, float world_z) const
{
    float temperature = temperature_noise.noise(world_x * BIOME_FREQUENCY, world_z * BIOME_FREQUENCY);
    float humidity = humidity_noise.noise(world_x * BIOME_FREQUENCY, world_z * BIOME_FREQUENCY);
    return classify_biome(temperature, humidity);
}
