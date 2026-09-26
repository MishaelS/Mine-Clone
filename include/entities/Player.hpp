#pragma once

#include "entities/Entity.hpp"
#include "entities/PlayerHealth.hpp"
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

// The player as a world entity: a 0.6 x 1.8 hitbox whose Entity::position
// is its feet (bottom center) and Entity::velocity its movement in blocks/
// second, plus its Survival health.
//
// The render camera stays separate from the hitbox. The Camera3D passed to
// update_movement()/reset() always represents the player's eyes - it holds
// the look direction, and update_movement() moves it along with the body;
// GameEngine may derive a third-person camera from it without moving the
// hitbox.
class Player : public Entity {
public:
    static constexpr float WIDTH      = 0.6f;
    static constexpr float HEIGHT     = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;

    // Clears all motion and contact state and puts the hitbox under
    // `eyes` - call after anything teleports the eye camera (spawn,
    // respawn, /tp, loading a save). Health is reset separately, via
    // health().reset(), since a teleport alone doesn't heal.
    void reset(const Camera3D& eyes);

    // Moves the hitbox one frame (walking/swimming physics in Survival,
    // free flight in Creative) and carries `eyes` along with it. Named
    // distinctly from GameObject::update() - it needs the eye camera and
    // input that signature can't carry.
    void update_movement(Camera3D& eyes, const World& world, GameMode mode,
                         const PlayerInput& input, float delta_time);

    PlayerHealth& health() { return health_state; }
    const PlayerHealth& health() const { return health_state; }

    Vector3 feet_position() const { return position; }
    Vector3 closest_hitbox_point(Vector3 point) const;
    bool intersects_block(int x, int y, int z) const;
    bool is_grounded()        const { return grounded; }
    bool is_in_water()        const { return touching_water; }
    bool is_in_lava()         const { return touching_lava; }
    bool is_touching_cactus() const { return touching_cactus; }
    bool is_head_submerged()  const { return head_submerged; }
    bool is_suffocating()     const { return suffocating; }
    float horizontal_speed()  const;

    // 0 unless this exact update_movement() call is the frame the hitbox's
    // feet just touched ground after falling - the distance accumulated
    // since it last touched ground or water (real Minecraft resets it on
    // either), for GameEngine to turn into fall damage (Survival only).
    // Consumes the pending value so the same landing isn't charged twice
    // if this is read more than once; returns -1.0f on every other frame.
    float consume_landing_fall_distance();

private:
    // Where the feet are for an eye camera at `eyes` - the inverse of the
    // eye placement at the end of update_movement().
    Vector3 feet_under(const Camera3D& eyes) const;

    PlayerHealth health_state; // Survival only - see GameEngine::update_player_damage()

    float step_visual_offset = 0.0f;
    bool grounded        = false;
    bool touching_water  = false;
    bool touching_lava   = false;
    bool touching_cactus = false;
    bool head_submerged  = false; // eye position specifically, not just any part of the hitbox - see is_head_submerged()
    bool suffocating     = false; // eye position sits inside a solid, opaque block

    // Fall-damage bookkeeping - see consume_landing_fall_distance().
    float fall_distance = 0.0f;
    bool just_landed = false;
    float landing_fall_distance = 0.0f;
};
