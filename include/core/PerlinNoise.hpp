#pragma once

#include "FastNoise/FastNoise.h"

#include <cstdint>

// Ken Perlin gradient noise, seeded so the same seed always reproduces the
// same terrain. Backed by FastNoise2's SIMD node graph (a Perlin generator
// feeding a fractal-FBm node) rather than a hand-rolled implementation.
// Used by World Generation to turn a block's world X/Z into a height value.
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
    int seed;
    // mutable: fractal() reconfigures fractal_node's octave/gain before each
    // query (see the .cpp), which needs a non-const node - an implementation
    // detail that doesn't change PerlinNoise's own externally observable
    // behavior (same inputs still always give the same output), so the
    // public methods stay const same as the old hand-rolled version.
    mutable FastNoise::SmartNode<FastNoise::Perlin> perlin;
    mutable FastNoise::SmartNode<FastNoise::FractalFBm> fractal_node;
};
