#pragma once

#include "model/EntityModel.hpp"
#include "raylib.h"

class World;

// Renders the player from assets/models/player.json (built in the model
// editor - tools/model_editor), playing its self-triggered animations -
// "idle" always, "walk" while moving (see EntityAnimator). Falls back to
// the built-in player template (make_humanoid_model()) if the file is
// missing.
class PlayerRenderer {
public:
    // `forward` is where the player looks: the head turns toward it and the
    // body follows once the head reaches its turn limit - or right away
    // while walking (see apply_head_look()/body_yaw_following_look() in
    // model/EntityModel.hpp). The body's yaw carries over between calls.
    // `sneaking` plays the model's sneaking-state animations (see
    // EntityAnimator).
    void draw(Vector3 feet_position, Vector3 forward, bool sneaking, const World& world) const;

    // Same model, but rotated by explicit yaw/pitch (degrees) instead of a
    // world-space forward vector, and lit by a single fixed `tint` instead
    // of sampling entity_environment_tint() from the world - for a UI
    // preview (the inventory screen's own player model) that has no World
    // to light from and shouldn't visually darken just because the actual
    // world happens to be dark right now, same as real Minecraft's own
    // inventory character preview always reading as evenly lit.
    // `yaw_degrees`/`pitch_degrees` (positive = down) are the look direction
    // - the head follows it, the body only past the head's own limit.
    void draw_flat(Vector3 feet_position, float yaw_degrees, float pitch_degrees, Color tint) const;

private:
    // Visual-only turning state, remembered from the last draw() call.
    mutable float body_yaw = 0.0f;
    mutable bool has_body_yaw = false;
    mutable Vector3 last_feet_position = {0.0f, 0.0f, 0.0f};
    mutable EntityAnimator animator;
};
