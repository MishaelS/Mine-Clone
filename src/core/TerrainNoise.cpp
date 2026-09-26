#include "core/TerrainNoise.hpp"

#include <algorithm>
#include <cmath>

TerrainNoise::TerrainNoise(uint32_t seed, WorldType type)
    // Distinct seeds so the layers don't sample identical patterns - small,
    // deterministic offsets are enough: FastNoise2's permutation table
    // already looks completely different for any two distinct seeds,
    // however close together.
    : type(type)
    , type_params(world_type_params(type))
    , height_noise(seed)
    , temperature_noise(seed + 1)
    , humidity_noise(seed + 2)
    , continent_noise(seed + 3)
    , river_noise(seed + 4)
    , coast_noise(seed + 5)
    , clay_noise(seed + 6)
    , gravel_noise(seed + 7)
    , mountain_noise(seed + 8)
    , ridge_noise(seed + 9)
    , sky_island_noise(seed + 10)
    , sky_altitude_noise(seed + 11)
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

float TerrainNoise::gravel(float world_x, float world_z) const
{
    return gravel_noise.noise(world_x * GRAVEL_FREQUENCY, world_z * GRAVEL_FREQUENCY);
}

float TerrainNoise::mountain(float world_x, float world_z) const
{
    // Only the upper part of the noise range becomes mountains, eased in,
    // so ranges rise out of ordinary land instead of covering everything.
    float value = mountain_noise.fractal(world_x * MOUNTAIN_FREQUENCY, world_z * MOUNTAIN_FREQUENCY, 3);
    float t = std::clamp((value + 0.1f) / 0.6f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float TerrainNoise::ridge(float world_x, float world_z) const
{
    // "Ridged" noise: 1 - |noise| peaks sharply along the noise's zero line.
    float value = ridge_noise.fractal(world_x * RIDGE_FREQUENCY, world_z * RIDGE_FREQUENCY, 4);
    return 1.0f - std::min(1.0f, std::fabs(value) * 1.6f);
}

float TerrainNoise::sky_island(float world_x, float world_z) const
{
    float value = sky_island_noise.fractal(world_x * SKY_ISLAND_FREQUENCY, world_z * SKY_ISLAND_FREQUENCY, 4);
    return std::clamp((value - SKY_ISLAND_THRESHOLD) / (0.6f - SKY_ISLAND_THRESHOLD), 0.0f, 1.0f);
}

float TerrainNoise::sky_altitude(float world_x, float world_z) const
{
    return sky_altitude_noise.fractal(world_x * SKY_ALTITUDE_FREQUENCY, world_z * SKY_ALTITUDE_FREQUENCY, 2);
}
