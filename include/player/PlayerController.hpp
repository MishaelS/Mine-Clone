#pragma once

#include "core/WorldSave.hpp"
#include "raylib.h"

class World;

struct PlayerInput {
    float forward = 0.0f;
    float right   = 0.0f;
    bool jump   = false;
    bool sneak  = false;
    bool sprint = false;
};

// The physical player is intentionally independent from the render camera.
// Camera3D passed here always represents the player's eyes; GameEngine may
// derive a third-person camera from it without moving the hitbox.
class PlayerController {
public:
    static constexpr float WIDTH      = 0.6f;
    static constexpr float HEIGHT     = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;

    void reset();
    void update(Camera3D& eyes, const World& world, GameMode mode,
                const PlayerInput& input, float delta_time);

    Vector3 feet_position(const Camera3D& eyes) const;
    Vector3 closest_hitbox_point(const Camera3D& eyes, Vector3 point) const;
    bool intersects_block(const Camera3D& eyes, int x, int y, int z) const;
    bool is_grounded()        const { return grounded; }
    bool is_in_water()        const { return touching_water; }
    bool is_in_lava()         const { return touching_lava; }
    bool is_touching_cactus() const { return touching_cactus; }
    bool is_head_submerged()  const { return head_submerged; }
    bool is_suffocating()     const { return suffocating; }
    float horizontal_speed()  const;

    // 0 unless this exact update() call is the frame the hitbox's feet just
    // touched ground after falling - the distance accumulated since it last
    // touched ground or water (real Minecraft resets it on either), for
    // GameEngine to turn into fall damage (Survival only). Consumes the
    // pending value so the same landing isn't charged twice if this is read
    // more than once; returns -1.0f on every other frame.
    float consume_landing_fall_distance();

private:
    Vector3 horizontal_velocity = {0.0f, 0.0f, 0.0f};
    float vertical_velocity  = 0.0f;
    float step_visual_offset = 0.0f;
    bool grounded        = false;
    bool touching_water  = false;
    bool touching_lava   = false;
    bool touching_cactus = false;
    bool head_submerged  = false; // eye position specifically, not just any part of the hitbox - see is_head_submerged()
    bool suffocating     = false;    // eye position sits inside a solid, opaque block

    // Fall-damage bookkeeping - see consume_landing_fall_distance().
    float fall_distance = 0.0f;
    bool just_landed = false;
    float landing_fall_distance = 0.0f;
};
