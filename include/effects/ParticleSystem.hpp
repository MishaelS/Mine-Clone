#pragma once

#include "core/Block.hpp"

#include "raylib.h"

#include <cstdint>
#include <vector>

class World;

// Short-lived, purely visual block particles. The system owns no gameplay
// state: callers describe an event and it expands that event into textured
// fragments, updates them, then renders them as camera-facing quads.
class ParticleSystem {
public:
    void spawn_hit(BlockType type, Vector3 surface_position, Vector3 surface_normal);
    void spawn_destroy(BlockType type, Vector3 block_center);
    void spawn_footstep(BlockType type, Vector3 ground_position);

    // `world`, when given, stops a particle at whatever solid block it
    // would otherwise fall/fly into instead of passing straight through -
    // point collision (a particle has no size of its own for this), same
    // "check where it's about to be, not where it already is" approach
    // DroppedItem's own ground check uses. Still purely cosmetic: nothing
    // here reads game state back out, it just looks right against real
    // terrain now instead of clipping through it.
    void update(float delta_time, const World* world);
    void draw(const Camera3D& camera) const;
    void clear();

private:
    struct Particle {
        Vector3 position{};
        Vector3 velocity{};
        Rectangle texture_source{};
        Color tint = WHITE;
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 0.1f;
        float gravity = 8.0f;
    };

    float random(float minimum, float maximum);
    Rectangle texture_fragment(BlockType type, BlockFace face);
    void add(Particle particle);

    std::vector<Particle> particles;
    uint32_t random_state = 0x91E10DA5u;
};
