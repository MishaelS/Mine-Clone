#include "rendering/PlayerRenderer.hpp"

#include "core/TextureManager.hpp"
#include "rendering/EntityLighting.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <array>
#include <cmath>
#include <functional>

namespace {
    constexpr const char* STEVE_TEXTURE = "sprites/entities/player/Steve.png";
    constexpr float SKIN_SIZE = 64.0f;

    // Vanilla player proportions are 32 model pixels tall.  Scaling those
    // pixels to the controller's 1.8-block standing height keeps the visual
    // model and the gameplay hitbox at the same height.
    constexpr float MODEL_PIXEL = 1.8f / 32.0f;

    struct PixelRect {
        float x, y, width, height;
    };

    struct BoxSkin {
        PixelRect top;
        PixelRect bottom;
        PixelRect back;
        PixelRect front;
        PixelRect left;
        PixelRect right;
    };

    enum FaceIndex { Top, Bottom, Back, Front, Left, Right };

    struct Face {
        Vector3 corners[4];
        Vector3 normal;
    };

    // Unit cube faces, counter-clockwise from outside.  Local +Z is the
    // player's front, matching GameEngine's yaw calculation.
    constexpr std::array<Face, 6> FACES = {{
        {{{-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}}, { 0, 1, 0}},
        {{{-1,-1, 1}, {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}}, { 0,-1, 0}},
        {{{-1, 1,-1}, { 1, 1,-1}, { 1,-1,-1}, {-1,-1,-1}}, { 0, 0,-1}},
        {{{ 1, 1, 1}, {-1, 1, 1}, {-1,-1, 1}, { 1,-1, 1}}, { 0, 0, 1}},
        {{{-1, 1, 1}, {-1, 1,-1}, {-1,-1,-1}, {-1,-1, 1}}, {-1, 0, 0}},
        {{{ 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1}, { 1,-1,-1}}, { 1, 0, 0}},
    }};

    constexpr BoxSkin HEAD = {
        { 8, 0, 8, 8}, {16, 0, 8, 8}, {24, 8, 8, 8},
        { 8, 8, 8, 8}, {16, 8, 8, 8}, { 0, 8, 8, 8},
    };
    constexpr BoxSkin TORSO = {
        {20,16, 8, 4}, {28,16, 8, 4}, {32,20, 8,12},
        {20,20, 8,12}, {28,20, 4,12}, {16,20, 4,12},
    };
    constexpr BoxSkin RIGHT_ARM = {
        {44,16, 4, 4}, {48,16, 4, 4}, {52,20, 4,12},
        {44,20, 4,12}, {48,20, 4,12}, {40,20, 4,12},
    };
    constexpr BoxSkin LEFT_ARM = {
        {36,48, 4, 4}, {40,48, 4, 4}, {44,52, 4,12},
        {36,52, 4,12}, {40,52, 4,12}, {32,52, 4,12},
    };
    constexpr BoxSkin RIGHT_LEG = {
        { 4,16, 4, 4}, { 8,16, 4, 4}, {12,20, 4,12},
        { 4,20, 4,12}, { 8,20, 4,12}, { 0,20, 4,12},
    };
    constexpr BoxSkin LEFT_LEG = {
        {20,48, 4, 4}, {24,48, 4, 4}, {28,52, 4,12},
        {20,52, 4,12}, {24,52, 4,12}, {16,52, 4,12},
    };

    Rectangle uv_rect(PixelRect pixels)
    {
        // As with terrain.png, stay infinitesimally inside the selected
        // rectangle without changing the apparent width of its edge pixels.
        constexpr float inset = 1.0f / 1024.0f;
        return {
            (pixels.x + inset) / SKIN_SIZE,
            (pixels.y + inset) / SKIN_SIZE,
            (pixels.width - inset * 2.0f) / SKIN_SIZE,
            (pixels.height - inset * 2.0f) / SKIN_SIZE,
        };
    }

    void draw_part(const Texture2D& skin, Vector3 center, Vector3 size,
                   const BoxSkin& box_skin, Color environment_tint)
    {
        const PixelRect face_pixels[6] = {
            box_skin.top, box_skin.bottom, box_skin.back,
            box_skin.front, box_skin.left, box_skin.right,
        };
        const float shade[6] = {1.0f, 0.55f, 0.78f, 0.86f, 0.72f, 0.72f};

        rlSetTexture(skin.id);
        rlBegin(RL_QUADS);
        for (int face_index = 0; face_index < 6; ++face_index) {
            const Face& face = FACES[face_index];
            Rectangle uv = uv_rect(face_pixels[face_index]);
            const unsigned char red = static_cast<unsigned char>(environment_tint.r * shade[face_index]);
            const unsigned char green = static_cast<unsigned char>(environment_tint.g * shade[face_index]);
            const unsigned char blue = static_cast<unsigned char>(environment_tint.b * shade[face_index]);
            rlColor4ub(red, green, blue, 255);
            rlNormal3f(face.normal.x, face.normal.y, face.normal.z);

            const float u[4] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
            const float v[4] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
            for (int i = 0; i < 4; ++i) {
                rlTexCoord2f(u[i], v[i]);
                rlVertex3f(
                    center.x + face.corners[i].x * size.x * 0.5f,
                    center.y + face.corners[i].y * size.y * 0.5f,
                    center.z + face.corners[i].z * size.z * 0.5f);
            }
        }
        rlEnd();
        rlSetTexture(0);
    }

    // Shared geometry for both PlayerRenderer::draw() (world-lit, yaw only
    // - real gameplay forward direction has no pitch) and ::draw_flat()
    // (fixed-lit, yaw+pitch - a UI preview's own free rotation) - the only
    // difference between the two callers is how `yaw`/`pitch` get computed
    // and how `tint_for` looks up each part's color.
    void draw_model(const Texture2D& skin, Vector3 feet_position, float yaw, float pitch,
                    const std::function<Color(Vector3)>& tint_for)
    {
        const float leg_w = 4.0f * MODEL_PIXEL;
        const float limb_h = 12.0f * MODEL_PIXEL;
        const float torso_w = 8.0f * MODEL_PIXEL;
        const float torso_d = 4.0f * MODEL_PIXEL;
        const float head = 8.0f * MODEL_PIXEL;

        rlPushMatrix();
        rlTranslatef(feet_position.x, feet_position.y, feet_position.z);
        rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
        if (pitch != 0.0f) rlRotatef(pitch, 1.0f, 0.0f, 0.0f);

        const Vector3 right_leg = {-leg_w * 0.5f, limb_h * 0.5f, 0.0f};
        const Vector3 left_leg = {leg_w * 0.5f, limb_h * 0.5f, 0.0f};
        const Vector3 torso = {0.0f, limb_h + limb_h * 0.5f, 0.0f};
        const Vector3 right_arm = {-torso_w * 0.5f - leg_w * 0.5f, limb_h + limb_h * 0.5f, 0.0f};
        const Vector3 left_arm = {torso_w * 0.5f + leg_w * 0.5f, limb_h + limb_h * 0.5f, 0.0f};
        const Vector3 head_center = {0.0f, limb_h * 2.0f + head * 0.5f, 0.0f};
        draw_part(skin, right_leg, {leg_w, limb_h, leg_w}, RIGHT_LEG, tint_for(right_leg));
        draw_part(skin, left_leg, {leg_w, limb_h, leg_w}, LEFT_LEG, tint_for(left_leg));
        draw_part(skin, torso, {torso_w, limb_h, torso_d}, TORSO, tint_for(torso));
        draw_part(skin, right_arm, {leg_w, limb_h, leg_w}, RIGHT_ARM, tint_for(right_arm));
        draw_part(skin, left_arm, {leg_w, limb_h, leg_w}, LEFT_ARM, tint_for(left_arm));
        draw_part(skin, head_center, {head, head, head}, HEAD, tint_for(head_center));

        rlPopMatrix();
    }
}

void PlayerRenderer::draw(Vector3 feet_position, Vector3 forward, const World& world) const
{
    const Texture2D& skin = TextureManager::get(STEVE_TEXTURE);
    // Steve.png is a modern 64x64 classic-arm atlas.  If an invalid asset is
    // supplied, do not reinterpret its pixel coordinates as another format.
    if (skin.width != 64 || skin.height != 64) return;

    forward.y = 0.0f;
    if (Vector3LengthSqr(forward) < 0.000001f) forward = {0.0f, 0.0f, 1.0f};
    forward = Vector3Normalize(forward);
    const float yaw = std::atan2(forward.x, forward.z) * RAD2DEG;
    const float yaw_radians = yaw * DEG2RAD;

    auto tint_for = [&](Vector3 local_center) {
        Vector3 world_center = {
            feet_position.x + local_center.x * std::cos(yaw_radians) + local_center.z * std::sin(yaw_radians),
            feet_position.y + local_center.y,
            feet_position.z - local_center.x * std::sin(yaw_radians) + local_center.z * std::cos(yaw_radians),
        };
        return entity_environment_tint(world, world_center);
    };

    draw_model(skin, feet_position, yaw, 0.0f, tint_for);
}

void PlayerRenderer::draw_flat(Vector3 feet_position, float yaw_degrees, float pitch_degrees, Color tint) const
{
    const Texture2D& skin = TextureManager::get(STEVE_TEXTURE);
    if (skin.width != 64 || skin.height != 64) return;

    draw_model(skin, feet_position, yaw_degrees, pitch_degrees, [tint](Vector3) { return tint; });
}
