#include "model/EntityModelRenderer.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <array>
#include <vector>

namespace {
    // The rotation part of a transform (for turning normals).
    Matrix without_translation(Matrix m)
    {
        m.m12 = m.m13 = m.m14 = 0.0f;
        return m;
    }

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
            {u + d        , v    , w, d},             // top
            {u + d + w    , v    , w, d},         // bottom
            {u + d + w + d, v + d, w, h}, // back
            {u + d        , v + d, w, h},         // front
            {u + d + w    , v + d, d, h},     // -X
            {u            , v + d, d, h},             // +X
        }};
    }

    void draw_cube(const ModelPart& part, const ModelCube& cube, const Texture2D& skin, float skin_w, float skin_h,
                   float scale, Color tint) {
        const bool textured = skin.id != 0;
        const std::array<Rectangle, 6> uvs = cube_face_uvs(cube);
        const bool rotated = cube.rotation.x != 0.0f || cube.rotation.y != 0.0f || cube.rotation.z != 0.0f;
        const Matrix turn = cube_rotation_matrix(cube, scale);
        constexpr float inset = 1.0f / 1024.0f; // stay just inside each face's pixels
        const BoundingBox bounds = cube_draw_bounds(part, cube);
        const Vector3 origin = bounds.min, size = Vector3Subtract(bounds.max, bounds.min);
        for (int f = 0; f < 6; ++f) {
            const Face& face = FACES[f];
            rlColor4ub(static_cast<unsigned char>(tint.r * face.shade), static_cast<unsigned char>(tint.g * face.shade),
                       static_cast<unsigned char>(tint.b * face.shade), tint.a);
            const Vector3 normal = rotated ? Vector3Transform(face.normal, without_translation(turn)) : face.normal;
            rlNormal3f(normal.x, normal.y, normal.z);
            const Rectangle r = uvs[f];
            const float u0 = (r.x + inset) / skin_w, u1 = (r.x + r.width - inset) / skin_w;
            const float v0 = (r.y + inset) / skin_h, v1 = (r.y + r.height - inset) / skin_h;
            const float u[4] = {u0, u1, u1, u0};
            const float v[4] = {v0, v0, v1, v1};
            for (int i = 0; i < 4; ++i) {
                if (textured) rlTexCoord2f(u[i], v[i]);
                Vector3 vertex = {(origin.x + face.corners[i].x * size.x) * scale,
                                  (origin.y + face.corners[i].y * size.y) * scale,
                                  (origin.z + face.corners[i].z * size.z) * scale};
                if (rotated) vertex = Vector3Transform(vertex, turn);
                rlVertex3f(vertex.x, vertex.y, vertex.z);
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

    // `overlays`: this pass draws only the decoration-layer cubes
    // (ModelCube::overlay), otherwise only the rest.
    void draw_part(const EntityModel& model, const Texture2D& skin, const ModelPose& pose, int index, float scale,
                   Color tint, bool overlays, int depth) {
        if (depth > 64) return; // a part made (accidentally) its own ancestor - never recurse forever
        const ModelPart& part = model.parts[index];
        rlPushMatrix();
        apply_part_pose(part, index < static_cast<int>(pose.size()) ? pose[index] : PartPose{}, scale);

        rlSetTexture(skin.id);
        rlBegin(RL_QUADS);
        for (const ModelCube& cube : part.cubes) {
            if (cube.overlay != overlays) continue;
            draw_cube(part, cube, skin, static_cast<float>(model.skin_width), static_cast<float>(model.skin_height), scale, tint);
        }
        rlEnd();
        rlSetTexture(0);

        for (size_t child = 0; child < model.parts.size(); ++child) {
            if (model.parts[child].parent == part.name && static_cast<int>(child) != index) {
                draw_part(model, skin, pose, static_cast<int>(child), scale, tint, overlays, depth + 1);
            }
        }
        rlPopMatrix();
    }
}

void draw_entity_model(const EntityModel& model, const Texture2D& skin, const ModelPose& pose, float scale, Color tint)
{
    auto draw_roots = [&](bool overlays) {
        for (size_t i = 0; i < model.parts.size(); ++i) {
            // Roots: no parent, or a parent name that doesn't exist (so a
            // typo never makes a part vanish).
            const std::string& parent = model.parts[i].parent;
            if (parent.empty() || model.find_part(parent) < 0) {
                draw_part(model, skin, pose, static_cast<int>(i), scale, tint, overlays, 0);
            }
        }
    };
    draw_roots(false);

    // The decoration layer after everything under it, both sides of its
    // faces: through its see-through pixels the inside of its far side
    // shows, like Minecraft's own hat/jacket layer.
    bool any_overlay = false;
    for (const ModelPart& part : model.parts) {
        for (const ModelCube& cube : part.cubes) any_overlay = any_overlay || cube.overlay;
    }
    if (!any_overlay) return;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    draw_roots(true);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

const Shader& entity_cutout_shader()
{
    static const Shader shader = LoadShaderFromMemory(nullptr, R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main()
{
    vec4 color = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (color.a < 0.1) discard;
    finalColor = color;
}
)");
    return shader;
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

Matrix cube_rotation_matrix(const ModelCube& cube, float scale)
{
    if (cube.rotation.x == 0.0f && cube.rotation.y == 0.0f && cube.rotation.z == 0.0f) return MatrixIdentity();
    const Vector3 o = Vector3Scale(cube.rotation_origin, scale);
    Matrix m = MatrixTranslate(-o.x, -o.y, -o.z);
    m = MatrixMultiply(m, MatrixRotate({1, 0, 0}, cube.rotation.x * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotate({0, 1, 0}, cube.rotation.y * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotate({0, 0, 1}, cube.rotation.z * DEG2RAD));
    return MatrixMultiply(m, MatrixTranslate(o.x, o.y, o.z));
}

std::array<Rectangle, 6> cube_face_uvs(const ModelCube& cube)
{
    std::array<Rectangle, 6> faces = box_uv(cube);
    for (int f = 0; f < 6; ++f) {
        if (cube.face_uv[f]) {
            faces[f].x = cube.face_uv[f]->x;
            faces[f].y = cube.face_uv[f]->y;
        }
    }
    return faces;
}
