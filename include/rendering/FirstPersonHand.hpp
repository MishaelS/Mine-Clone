#pragma once

#include "items/Inventory.hpp"
#include "model/EntityModel.hpp"

#include "raylib.h"

// The player's own right arm at the bottom right of the screen in first
// person, with whatever it holds in its hand - a block as a small cube, an
// item as its sprite extruded one pixel thick. All of it comes from the
// model assets/models/first_person.json, made in the model editor (its
// "player's view" camera shows exactly this): the arm's parts and rest
// pose, where each kind of held item sits (its item slots - ModelPart::
// item_slot), its "swing" animation for a hit/use, and the arm's pose for
// what it holds: "empty" for an empty hand, "hold_block", "hold_tool" or
// "hold_item" holding one. On top, in code: bobbing while walking, trailing a
// little behind fast turns, and dipping out of view and back while
// switching items.
class FirstPersonHand {
public:
    // Every frame: what's selected in the hotbar, where the player stands,
    // whether it's on the ground, and where it looks.
    void update(float delta_time, const ItemStack& held, Vector3 feet, bool grounded, Vector3 look);

    // A hit, a placed block, a used item... Restarts only once a running
    // swing is past halfway, so holding the button swings steadily.
    void swing();
    bool swinging() const { return swing_time >= 0.0f; }
    // Seconds into the current swing, -1 when not swinging - the player
    // model plays its "swing" animation in step with it (PlayerRenderer).
    float swing_seconds() const { return swing_time; }

    // Draws on top of the frame in its own camera space - it has to be
    // called outside BeginMode3D/EndMode3D, and clears the depth buffer so
    // nothing in the world (a wall right in front) ever cuts into it.
    // `light` tints it like everything else at the player's eyes.
    void draw(Color light) const;

private:
    float swing_time = -1.0f; // seconds into the current swing, -1 = none
    float equip = 0.0f;       // 0 = raised .. 1 = lowered out of view
    ItemStack shown;          // what's drawn - swapped for the held item at the bottom of the dip

    // Walking bob (Minecraft's view bobbing, applied to the arm).
    float walk_distance = 0.0f;
    float bob = 0.0f;
    Vector3 last_feet = {0.0f, 0.0f, 0.0f};
    bool has_last_feet = false;

    // The model's animations: Always ones, the pose for what's held
    // ("empty"/"hold_*"), "swing" while swinging.
    EntityAnimator animator;
    float empty_seconds = 0.0f;

    // Hand sway: the arm's own view angles trail the camera's.
    float yaw = 0.0f, pitch = 0.0f;
    float lagged_yaw = 0.0f, lagged_pitch = 0.0f;
    bool has_angles = false;
};
