#include "core/PerlinNoise.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace {
    float fade(float t) {
        // 6t^5 - 15t^4 + 10t^3: eases interpolation so it has zero 1st and
        // 2nd derivatives at t=0 and t=1, which is what removes the visible
        // grid-aligned seams plain linear interpolation would leave.
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float lerp(float t, float a, float b) {
        return a + t * (b - a);
    }

    // Dot product of (x, y) with one of 8 unit-ish gradient directions,
    // picked by the low 3 bits of the hash.
    float grad(int hash, float x, float y) {
        switch (hash & 7) {
            case 0: return  x + y;
            case 1: return  x;
            case 2: return  x - y;
            case 3: return -y;
            case 4: return -x - y;
            case 5: return -x;
            case 6: return -x + y;
            default: return y;
        }
    }
}

PerlinNoise::PerlinNoise(uint32_t seed)
{
    std::array<int, 256> base;
    std::iota(base.begin(), base.end(), 0);

    std::mt19937 rng(seed);
    std::shuffle(base.begin(), base.end(), rng);

    for (int i = 0; i < 256; ++i) {
        permutation[i] = base[i];
        permutation[i + 256] = base[i];
    }
}

float PerlinNoise::noise(float x, float y) const
{
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    float u = fade(xf);
    float v = fade(yf);

    int aa = permutation[permutation[xi + 0] + yi + 0];
    int ab = permutation[permutation[xi + 0] + yi + 1];
    int ba = permutation[permutation[xi + 1] + yi + 0];
    int bb = permutation[permutation[xi + 1] + yi + 1];

    float x1 = lerp(u, grad(aa, xf, yf       ), grad(ba, xf - 1.0f, yf       ));
    float x2 = lerp(u, grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f));

    return lerp(v, x1, x2);
}

float PerlinNoise::fractal(float x, float y, int octaves, float persistence) const
{
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float max_amplitude = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += noise(x * frequency, y * frequency) * amplitude;
        max_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / max_amplitude;
}
