#include "core/TerrainNoise.hpp"

TerrainNoise::TerrainNoise(uint32_t seed)
    // Distinct seeds so the layers don't sample identical patterns — small,
    // deterministic offsets are enough: FastNoise2's permutation table
    // already looks completely different for any two distinct seeds,
    // however close together.
    : height_noise(seed)
    , temperature_noise(seed + 1)
    , humidity_noise(seed + 2)
    , continent_noise(seed + 3)
    , river_noise(seed + 4)
    , coast_noise(seed + 5)
    , clay_noise(seed + 6)
{
}

float TerrainNoise::height(float world_x, float world_z, int octaves, float persistence) const
{
    return height_noise.fractal(world_x, world_z, octaves, persistence);
}

BiomeWeights TerrainNoise::biome_weights(float world_x, float world_z) const
{
    float temperature = temperature_noise.noise(world_x * BIOME_FREQUENCY, world_z * BIOME_FREQUENCY);
    float humidity = humidity_noise.noise(world_x * BIOME_FREQUENCY, world_z * BIOME_FREQUENCY);
    float continentalness = continent_noise.noise(world_x * CONTINENT_FREQUENCY, world_z * CONTINENT_FREQUENCY);
    float coast_roughness = coast_noise.noise(world_x * COAST_FREQUENCY, world_z * COAST_FREQUENCY);
    return compute_biome_weights(temperature, humidity, continentalness, coast_roughness);
}

Biome TerrainNoise::biome(float world_x, float world_z) const
{
    return dominant_biome(biome_weights(world_x, world_z));
}

float TerrainNoise::river(float world_x, float world_z) const
{
    return river_noise.noise(world_x * RIVER_FREQUENCY, world_z * RIVER_FREQUENCY);
}

float TerrainNoise::clay(float world_x, float world_z) const
{
    return clay_noise.noise(world_x * CLAY_FREQUENCY, world_z * CLAY_FREQUENCY);
}
