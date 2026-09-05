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

    // FastNoise2's Perlin is a ScalableGenerator: it silently divides every
    // input coordinate by its own "feature scale" (default 100, i.e. an
    // extra hidden x0.01 on frequency) before sampling — on top of whatever
    // frequency noise()/fractal()'s own caller already multiplied in.
    // Locking scale to 1 here makes this node do no scaling of its own, so
    // noise()/fractal()'s `x`/`y` are the only place frequency is
    // controlled, matching what every caller in this project already
    // assumes. Found by chasing a real bug: World Generation's biome
    // temperature/humidity noise (sampled with noise(), not fractal())
    // came back nearly constant (+/-0.007 instead of the expected +/-1)
    // because of this — terrain height (sampled with fractal(), whose
    // source is this same node) was silently affected the exact same way,
    // just harder to notice since a ~100x-too-large wavelength still looks
    // like a plausible, if very gentle, slope within one loaded area.
    perlin->SetScale(1.0f);

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
