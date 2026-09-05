#include "core/PerlinNoise.hpp"

namespace {
    // Matches this project's old hand-rolled fractal: frequency doubles
    // every octave. FastNoise2 calls this "lacunarity" and leaves it
    // configurable per node; fixed here since nothing in this project needs
    // anything else.
    constexpr float LACUNARITY = 2.0f;
}

PerlinNoise::PerlinNoise(uint32_t seed)
    : seed(static_cast<int>(seed))
{
    perlin = FastNoise::New<FastNoise::Perlin>();

    fractal_node = FastNoise::New<FastNoise::FractalFBm>();
    fractal_node->SetSource(perlin);
    fractal_node->SetLacunarity(LACUNARITY);
}

float PerlinNoise::noise(float x, float y) const
{
    return perlin->GenSingle2D(x, y, seed);
}

float PerlinNoise::fractal(float x, float y, int octaves, float persistence) const
{
    // Cheap to set on every call (single-threaded, one call per terrain
    // column) and keeps this matching noise()/fractal()'s old per-call
    // parameters instead of baking octaves/persistence in at construction.
    fractal_node->SetOctaveCount(octaves);
    fractal_node->SetGain(persistence);
    return fractal_node->GenSingle2D(x, y, seed);
}
