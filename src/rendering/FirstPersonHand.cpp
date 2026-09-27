#include "rendering/FirstPersonHand.hpp"
#include "core/TextureManager.hpp"
#include "model/EntityModelRenderer.hpp"
#include "model/ModelLibrary.hpp"
#include "rendering/HeldItem.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

// rlgl has no depth-only clear; the platform's GL has it.
extern "C" void glClear(unsigned int mask);

namespace {
    constexpr unsigned int GL_DEPTH_BUFFER_BIT_VALUE = 0x00000100;

    constexpr const char* HAND_MODEL      = "first_person";
    constexpr const char* SWING_ANIMATION = "swing";
    // The arm's pose for what it holds - one looping Manual animation each
    // ("empty" for an empty hand, "hold_block"/"hold_tool"/"hold_item"), the
    // editor's player view previews the same.
    constexpr const char* EMPTY_ANIMATION = "empty";
    constexpr const char* HOLD_ANIMATION_PREFIX = "hold_";
    constexpr float HAND_FOV            = 70.0f;        // degrees - fixed, whatever the world's own FOV (the editor's player view uses it too)
    constexpr float MODEL_PIXEL         = 1.0f / 16.0f; // the hand model is in pixels, 16 to a block, around the eye
    constexpr float SWING_SECONDS       = 0.3f;         // 6 ticks, Minecraft's swing
    constexpr float EQUIP_SPEED         = 8.0f;         // share of the dip per second (0.4 per tick)
    constexpr float EQUIP_DROP          = 0.6f;         // blocks the hand sinks by, fully dipped
    constexpr float SWAY_FOLLOW_RATE    = 10.0f;        // how fast the arm's angles catch up with the camera's (0.5 per tick)
    constexpr float SWAY_STRENGTH       = 0.1f;         // share of the lag the arm turns by
    constexpr float BOB_RATE            = 8.0f;         // how fast bobbing fades in/out (0.4 per tick)
    constexpr float BOB_MAX             = 0.1f;         // blocks/tick of walking that bobs fully
    constexpr float WALK_DISTANCE_SCALE = 0.6f;         // Minecraft's walkDist step per block walked

    bool same_item(const ItemStack& a, const ItemStack& b) {
        if (a.empty() || b.empty()) return a.empty() == b.empty();
        return a.block == b.block && a.tool == b.tool;
    }
}

void FirstPersonHand::update(float delta_time, const ItemStack& held, Vector3 feet, bool grounded, Vector3 look)
{
    delta_time = std::clamp(delta_time, 0.0f, 0.1f);

    if (swing_time >= 0.0f) {
        swing_time += delta_time;
        if (swing_time >= SWING_SECONDS) swing_time = -1.0f;
    }

    // Switching items: down, swap at the bottom, back up.
    if (!same_item(held, shown)) {
        equip = std::min(1.0f, equip + EQUIP_SPEED * delta_time);
        if (equip >= 1.0f) shown = held;
    } else {
        shown = held; // same item - keep its count/durability current
        equip = std::max(0.0f, equip - EQUIP_SPEED * delta_time);
    }

    // Walking bob: the distance walked on the ground drives it.
    const float moved = has_last_feet ? std::hypot(feet.x - last_feet.x, feet.z - last_feet.z) : 0.0f;
    last_feet = feet;
    has_last_feet = true;
    const float per_tick = delta_time > 0.0f ? moved / delta_time / 20.0f : 0.0f;
    const float target_bob = grounded && moved < 2.0f ? std::min(BOB_MAX, per_tick) : 0.0f;
    bob += (target_bob - bob) * std::min(1.0f, BOB_RATE * delta_time);
    if (moved < 2.0f) walk_distance += moved * WALK_DISTANCE_SCALE;

    // Where it looks: yaw grows turning right, pitch looking down.
    if (Vector3LengthSqr(look) > 0.0f) {
        look = Vector3Normalize(look);
        yaw = std::atan2(look.x, -look.z) * RAD2DEG;
        pitch = -std::asin(std::clamp(look.y, -1.0f, 1.0f)) * RAD2DEG;
        if (!has_angles) {
            lagged_yaw = yaw;
            lagged_pitch = pitch;
            has_angles = true;
        }
        const float follow = std::min(1.0f, SWAY_FOLLOW_RATE * delta_time);
        lagged_yaw += wrap_degrees(yaw - lagged_yaw) * follow;
        lagged_pitch += (pitch - lagged_pitch) * follow;
    }

    // The model's own animations for this frame.
    empty_seconds = same_item(shown, held) ? empty_seconds + delta_time : 0.0f; // time in the current pose
    animator.update(delta_time, 0.0f);
    animator.clear_manual();
    animator.add_manual(shown.empty() ? std::string(EMPTY_ANIMATION) : HOLD_ANIMATION_PREFIX + std::string(held_item_slot(shown)),
                        empty_seconds);
    if (swing_time >= 0.0f) animator.add_manual(SWING_ANIMATION, swing_time);
}

void FirstPersonHand::swing()
{
    if (swing_time < 0.0f || swing_time >= SWING_SECONDS * 0.5f) swing_time = 0.0f;
}

void FirstPersonHand::draw(Color light) const
{
    const EntityModel& model = entity_model(HAND_MODEL);
    if (model.parts.empty()) return;
    const Texture2D& skin = model.skin.empty() ? Texture2D{} : TextureManager::get(model.skin);
    const ModelPose pose = animator.pose(model);

    // Its own camera: the eye at the origin looking down -Z - the space the
    // hand model is made in.
    Camera3D camera{};
    camera.position = {0, 0, 0};
    camera.target = {0, 0, -1};
    camera.up = {0, 1, 0};
    camera.fovy = HAND_FOV;
    camera.projection = CAMERA_PERSPECTIVE;

    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT_VALUE);
    BeginMode3D(camera);
    BeginShaderMode(entity_cutout_shader());
    rlPushMatrix();

    // Walking bob (GameRenderer.bobView).
    const float phase = walk_distance * PI;
    rlTranslatef(std::sin(phase) * bob * 0.5f, -std::fabs(std::cos(phase) * bob), 0.0f);
    rlRotatef(std::sin(phase) * bob * 3.0f, 0, 0, 1);
    rlRotatef(std::fabs(std::cos(phase - 0.2f) * bob) * 5.0f, 1, 0, 0);
    // Hand sway: trails the camera a little (ItemInHandRenderer).
    rlRotatef((pitch - lagged_pitch) * SWAY_STRENGTH, 1, 0, 0);
    rlRotatef(wrap_degrees(yaw - lagged_yaw) * SWAY_STRENGTH, 0, 1, 0);
    // Switching items: dipped out of view.
    rlTranslatef(0.0f, -equip * EQUIP_DROP, 0.0f);

    draw_entity_model(model, skin, pose, MODEL_PIXEL, light);

    // Whatever it holds, filling the model's slot for that kind of item.
    draw_held_item(model, pose, shown, MODEL_PIXEL, light);

    rlPopMatrix();
    EndShaderMode();
    EndMode3D();
}
