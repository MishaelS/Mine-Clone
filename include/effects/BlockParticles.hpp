#pragma once

#include "core/Block.hpp"
#include "core/BlockShape.hpp"

#include "raylib.h"

#include <functional>

// The particles blocks give off on their animate ticks (BlockParticleEmitter
// - a torch's flame and smoke, a lit furnace's fire, falling leaves): how
// one is born, moves and looks. Pure - no world, no registry - so the game's
// ParticleSystem and the model editor's preview share it exactly.
namespace block_particles {

    // Flame, smoke and dust sprites: 8x8 cells - row 0 a dot 1..8 px
    // across (smoke and dust shrink through them), row 1 the flame. A leaf
    // is a bit of its own block's tile instead.
    constexpr const char* SHEET_PATH = "sprites/particles.png";

    struct Particle {
        BlockParticleKind kind = BlockParticleKind::Smoke;
        Vector3 position{};
        Vector3 velocity{};
        Color tint = WHITE;
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 0.1f;          // across, in blocks
        float phase = 0.0f;         // a leaf's flutter
        Rectangle atlas_source{};   // a leaf's bit of its block's tile, in terrain.png pixels
    };

    // A random number in [minimum, maximum).
    using Random = std::function<float(float, float)>;

    // Where an emitter puts one particle, in its block's 0..1 cell space:
    // its point plus `unit` (each axis -1..1) times its spread, carried
    // along with the block - its model's placement in this state (a wall
    // torch's lean), or its facing (a directional block's front).
    Vector3 emit_point(const BlockParticleEmitter& emitter, Vector3 unit, const BlockStateModel& model,
                       const BlockInstanceState& state, bool directional);

    // A new particle of `kind` at `position` (world space) in `color`.
    // `tile` is its block's side tile in terrain.png pixels - a leaf is a
    // bit of it.
    Particle make(BlockParticleKind kind, Vector3 position, Color color, Rectangle tile, const Random& random);

    // One step of `dt` seconds: drifting, rising, fluttering down. No
    // collision - a falling leaf is stopped on the ground by the caller.
    void step(Particle& particle, float dt);

    // Whether it's drawn from the sheet (else from terrain.png), whether it
    // glows (never darkened by the dark), whether it falls (and so lands).
    bool from_sheet(const Particle& particle);
    bool glows(const Particle& particle);
    bool falls(const Particle& particle);
    // Its picture right now (pixels of its texture), size and color.
    Rectangle source(const Particle& particle);
    float draw_size(const Particle& particle);
    Color draw_color(const Particle& particle);

} // namespace block_particles
