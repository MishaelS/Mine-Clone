#pragma once

#include <array>
#include <cstdint>

// Classic (Ken Perlin) 2D gradient noise, seeded so the same seed always
// reproduces the same terrain. Used by World Generation to turn a block's
// world X/Z into a height value.
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed);

    // Single-octave noise at (x, y), roughly in [-1, 1].
    float noise(float x, float y) const;

    // Sum of `octaves` layers of noise() at doubling frequency and halving
    // amplitude each layer (fractal Brownian motion), renormalized back to
    // roughly [-1, 1]. More octaves add finer detail on top of the same
    // broad shape; used for terrain height so hills read as natural rather
    // than as a single smooth wave.
    float fractal(float x, float y, int octaves, float persistence = 0.5f) const;

private:
    // 0-255 permutation, duplicated to 512 entries so lookups never need to
    // wrap the index by hand.
    std::array<int, 512> permutation;
};
