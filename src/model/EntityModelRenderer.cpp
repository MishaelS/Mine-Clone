#include "model/EntityModelRenderer.hpp"

#include "rlgl.h"

#include <array>
#include <vector>

namespace {
    struct Face {
        Vector3 corners[4]; // unit cube corners, counter-clockwise from outside
        Vector3 normal;
        float shade;
    };

    // Same corner order/winding as PlayerRenderer's own player model, so a
    // skin maps onto both identically. Order: top, bottom, back (-Z),
    // front (+Z), -X, +X.
    constexpr std::array<Face, 6> FACES = {{
        {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, { 0, 1, 0}, 1.00f},
        {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, { 0,-1, 0}, 0.55f},
        {{{0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {0, 0, 0}}, { 0, 0,-1}, 0.78f},
        {{{1, 1, 1}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}}, { 0, 0, 1}, 0.86f},
        {{{0, 1, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}}, {-1, 0, 0}, 0.72f},
        {{{1, 1, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}}, { 1, 0, 0}, 0.72f},
    }};

    // Standard Minecraft box-UV layout of a w x h x d cube whose layout
    // starts at (u, v): a cross of the six faces - top/bottom on the first
    // row, the four sides below them. Same face order as FACES.
    std::array<Rectangle, 6> box_uv(const ModelCube& cube) {
        const float u = cube.uv.x, v = cube.uv.y;
        const float w = cube.size.x, h = cube.size.y, d = cube.size.z;
        return {{
            {u + d, v, w, d},             // top
            {u + d + w, v, w, d},         // bottom
            {u + d + w + d, v + d, w, h}, // back
            {u + d, v + d, w, h},         // front
            {u + d + w, v + d, d, h},     // -X
            {u, v + d, d, h},             // +X
        }};
    }

    void draw_cube(const ModelCube& cube, const Texture2D& skin, float skin_w, float skin_h, float scale, Color tint) {
        const bool textured = skin.id != 0;
        const std::array<Rectangle, 6> uvs = box_uv(cube);
        constexpr float inset = 1.0f / 1024.0f; // stay just inside each face's pixels
        for (int f = 0; f < 6; ++f) {
            const Face& face = FACES[f];
            rlColor4ub(static_cast<unsigned char>(tint.r * face.shade), static_cast<unsigned char>(tint.g * face.shade),
                       static_cast<unsigned char>(tint.b * face.shade), tint.a);
            rlNormal3f(face.normal.x, face.normal.y, face.normal.z);
            const Rectangle r = uvs[f];
            const float u0 = (r.x + inset) / skin_w, u1 = (r.x + r.width - inset) / skin_w;
            const float v0 = (r.y + inset) / skin_h, v1 = (r.y + r.height - inset) / skin_h;
            const float u[4] = {u0, u1, u1, u0};
            const float v[4] = {v0, v0, v1, v1};
            for (int i = 0; i < 4; ++i) {
                if (textured) rlTexCoord2f(u[i], v[i]);
                rlVertex3f((cube.origin.x + face.corners[i].x * cube.size.x) * scale,
                           (cube.origin.y + face.corners[i].y * cube.size.y) * scale,
                           (cube.origin.z + face.corners[i].z * cube.size.z) * scale);
            }
        }
    }

    // Rotation about the part's pivot: applied X, then Y, then Z (rlgl
    // applies the last call first).
    void apply_part_pose(const ModelPart& part, const PartPose& pose, float scale) {
        // Moved first (in the parent's space), then turned about its pivot.
        if (pose.offset.x != 0.0f || pose.offset.y != 0.0f || pose.offset.z != 0.0f) {
            rlTranslatef(pose.offset.x * scale, pose.offset.y * scale, pose.offset.z * scale);
        }
        const Vector3 rotation = pose.rotation;
        rlTranslatef(part.pivot.x * scale, part.pivot.y * scale, part.pivot.z * scale);
        if (rotation.z != 0.0f) rlRotatef(rotation.z, 0.0f, 0.0f, 1.0f);
        if (rotation.y != 0.0f) rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);
        if (rotation.x != 0.0f) rlRotatef(rotation.x, 1.0f, 0.0f, 0.0f);
        rlTranslatef(-part.pivot.x * scale, -part.pivot.y * scale, -part.pivot.z * scale);
    }

    void draw_part(const EntityModel& model, const Texture2D& skin, const ModelPose& pose, int index, float scale,
                   Color tint, int depth) {
        if (depth > 64) return; // a part made (accidentally) its own ancestor - never recurse forever
        const ModelPart& part = model.parts[index];
        rlPushMatrix();
        apply_part_pose(part, index < static_cast<int>(pose.size()) ? pose[index] : PartPose{}, scale);

        rlSetTexture(skin.id);
        rlBegin(RL_QUADS);
        for (const ModelCube& cube : part.cubes) {
            draw_cube(cube, skin, static_cast<float>(model.skin_width), static_cast<float>(model.skin_height), scale, tint);
        }
        rlEnd();
        rlSetTexture(0);

        for (size_t child = 0; child < model.parts.size(); ++child) {
            if (model.parts[child].parent == part.name && static_cast<int>(child) != index) {
                draw_part(model, skin, pose, static_cast<int>(child), scale, tint, depth + 1);
            }
        }
        rlPopMatrix();
    }
}

void draw_entity_model(const EntityModel& model, const Texture2D& skin, const ModelPose& pose, float scale, Color tint)
{
    for (size_t i = 0; i < model.parts.size(); ++i) {
        // Roots: no parent, or a parent name that doesn't exist (so a typo
        // never makes a part vanish).
        const std::string& parent = model.parts[i].parent;
        if (parent.empty() || model.find_part(parent) < 0) {
            draw_part(model, skin, pose, static_cast<int>(i), scale, tint, 0);
        }
    }
}

void push_part_transform(const EntityModel& model, const ModelPose& pose, int part, float scale)
{
    // Chain from the root down to `part`.
    std::vector<int> chain;
    for (int i = part; i >= 0 && chain.size() < 64; i = model.find_part(model.parts[i].parent)) {
        chain.push_back(i);
    }
    rlPushMatrix();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        apply_part_pose(model.parts[*it], *it < static_cast<int>(pose.size()) ? pose[*it] : PartPose{}, scale);
    }
}
