#pragma once

#include "core/WorldSave.hpp"
#include "raylib.h"

class World;

struct PlayerInput {
    float forward = 0.0f;
    float right = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
};

// The physical player is intentionally independent from the render camera.
// Camera3D passed here always represents the player's eyes; GameEngine may
// derive a third-person camera from it without moving the hitbox.
class PlayerController {
public:
    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;

    void reset();
    void update(Camera3D& eyes, const World& world, GameMode mode,
                const PlayerInput& input, float delta_time);

    Vector3 feet_position(const Camera3D& eyes) const;
    Vector3 closest_hitbox_point(const Camera3D& eyes, Vector3 point) const;
    bool intersects_block(const Camera3D& eyes, int x, int y, int z) const;
    bool is_grounded() const { return grounded; }
    bool is_in_water() const { return touching_water; }
    float horizontal_speed() const;

private:
    Vector3 horizontal_velocity = {0.0f, 0.0f, 0.0f};
    float vertical_velocity = 0.0f;
    bool grounded = false;
    bool touching_water = false;
};
