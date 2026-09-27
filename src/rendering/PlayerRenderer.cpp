#include "rendering/PlayerRenderer.hpp"

#include "core/TextureManager.hpp"
#include "model/EntityModel.hpp"
#include "model/EntityModelRenderer.hpp"
#include "rendering/EntityLighting.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr const char* PLAYER_MODEL_PATH = ASSETS_PATH "models/player.json";
    constexpr const char* SWING_ANIMATION = "swing"; // the model's Manual hit/use animation

    // The model is 32 pixels tall; scaling those to the controller's
    // 1.8-block standing height keeps the visible body and the gameplay
    // hitbox the same height.
    constexpr float MODEL_PIXEL = 1.8f / 32.0f;

    // While walking the body turns to face where the player looks, this
    // fast (fraction of the remaining angle per second); standing still it
    // only moves when the head runs out of turn (see draw()).
    constexpr float WALKING_BODY_TURN_RATE = 8.0f;
    constexpr float WALKING_SPEED = 0.5f; // blocks/second of horizontal movement that counts as walking

    // Loaded once, the first time anything draws the player - from the model
    // editor's assets/models/player.json, or the built-in player template if
    // that file is missing or unreadable, so the player never disappears.
    const EntityModel& player_model()
    {
        static const EntityModel model = load_entity_model(PLAYER_MODEL_PATH).value_or(make_humanoid_model());
        return model;
    }

    // The whole model turned to `body_yaw`, its head on toward `look_yaw`
    // and down by `pitch_down` (all degrees).
    void draw_model(Vector3 feet_position, float body_yaw, float look_yaw, float pitch_down, Color tint,
                    const EntityAnimator& animator)
    {
        const EntityModel& model = player_model();
        const Texture2D& skin = model.skin.empty() ? Texture2D{} : TextureManager::get(model.skin);
        ModelPose pose = animator.pose(model);
        apply_head_look(model, pose, look_yaw - body_yaw, pitch_down);

        rlPushMatrix();
        rlTranslatef(feet_position.x, feet_position.y, feet_position.z);
        rlRotatef(body_yaw, 0.0f, 1.0f, 0.0f);
        draw_entity_model(model, skin, pose, MODEL_PIXEL, tint);
        rlPopMatrix();
    }
}

void PlayerRenderer::draw(Vector3 feet_position, Vector3 forward, bool sneaking, const World& world, float swing_seconds) const
{
    if (Vector3LengthSqr(forward) < 0.000001f) forward = {0.0f, 0.0f, 1.0f};
    forward = Vector3Normalize(forward);
    const float look_yaw = std::atan2(forward.x, forward.z) * RAD2DEG;
    const float pitch_down = -std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * RAD2DEG;

    const float delta_time = std::max(GetFrameTime(), 0.0001f);
    const Vector3 moved = Vector3Subtract(feet_position, last_feet_position);
    // Distance walked since the last frame drives the walk animation; a jump
    // (spawn, teleport, respawn) doesn't count as a stride.
    float moved_distance = has_body_yaw ? std::sqrt(moved.x * moved.x + moved.z * moved.z) : 0.0f;
    if (moved_distance > 2.0f) moved_distance = 0.0f;
    const bool walking = moved_distance / delta_time > WALKING_SPEED;
    last_feet_position = feet_position;
    animator.update(delta_time, moved_distance, sneaking);
    animator.set_manual(swing_seconds >= 0.0f ? SWING_ANIMATION : "", swing_seconds);

    if (!has_body_yaw) {
        body_yaw = look_yaw;
        has_body_yaw = true;
    } else if (walking) {
        body_yaw += wrap_degrees(look_yaw - body_yaw) * std::min(1.0f, delta_time * WALKING_BODY_TURN_RATE);
    }
    body_yaw = body_yaw_following_look(player_model(), body_yaw, look_yaw);

    // Lit by the light around the middle of the body.
    const Color tint = entity_environment_tint(world, Vector3Add(feet_position, {0.0f, 0.9f, 0.0f}));
    draw_model(feet_position, body_yaw, look_yaw, pitch_down, tint, animator);
}

void PlayerRenderer::draw_flat(Vector3 feet_position, float yaw_degrees, float pitch_degrees, Color tint) const
{
    // The preview's body faces the viewer; only a look past the head's own
    // limit turns it.
    const float body = body_yaw_following_look(player_model(), 0.0f, yaw_degrees);
    // The preview stands still: only its "always" animations play.
    static EntityAnimator preview_animator;
    preview_animator.update(GetFrameTime(), 0.0f);
    draw_model(feet_position, body, yaw_degrees, pitch_degrees, tint, preview_animator);
}
