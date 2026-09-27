#include "rendering/BlockIcon.hpp"
#include "core/BlockShape.hpp"

#include "rlgl.h"


#include <algorithm>
#include <cmath>

namespace {
    Color shade(Color color, float brightness)
    {
        return {static_cast<unsigned char>(color.r * brightness), static_cast<unsigned char>(color.g * brightness),
                static_cast<unsigned char>(color.b * brightness), color.a};
    }

    // Keeps a UV rectangle a hair inside its tile, so rounding never
    // samples the neighboring one (as get_sample_safe_block_uv() does).
    Rectangle sample_safe(Rectangle uv, const Texture2D& atlas)
    {
        constexpr float SUB_TEXEL_INSET = 1.0f / 1024.0f;
        const float inset_u = SUB_TEXEL_INSET / static_cast<float>(atlas.width);
        const float inset_v = SUB_TEXEL_INSET / static_cast<float>(atlas.height);
        return {uv.x + std::copysign(inset_u, uv.width), uv.y + std::copysign(inset_v, uv.height),
                std::copysign(std::max(0.0f, std::fabs(uv.width) - inset_u * 2.0f), uv.width),
                std::copysign(std::max(0.0f, std::fabs(uv.height) - inset_v * 2.0f), uv.height)};
    }

    // One textured quad; `vertices` in texture order TL, TR, BR, BL.
    void atlas_quad(Rectangle uv, const Vector2 (&vertices)[4], Color tint, const Texture2D& atlas)
    {
        uv = sample_safe(uv, atlas);
        const float u[] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
        const float v[] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
        // raylib's 2D quads go TL, BL, BR, TR - front-facing with back-face
        // culling left on.
        constexpr int DRAW_ORDER[] = {0, 3, 2, 1};
        rlColor4ub(tint.r, tint.g, tint.b, tint.a);
        rlNormal3f(0.0f, 0.0f, 1.0f);
        for (int i : DRAW_ORDER) {
            rlTexCoord2f(u[i], v[i]);
            rlVertex2f(vertices[i].x, vertices[i].y);
        }
    }
}

void draw_cube_icon(Rectangle bounds, const Texture2D& atlas, const CubeIconFace& top, const CubeIconFace& left,
                    const CubeIconFace& right, float side_inset)
{
    // Orthographic isometric cube fitted inside the icon bounds. The
    // proportions mirror Minecraft's inventory block-item rendering: a
    // shallow diamond top and two taller visible side faces.
    const float center_x   = bounds.x + bounds.width  * 0.5f;
    const float top_y      = bounds.y + bounds.height * 0.04f;
    const float shoulder_y = bounds.y + bounds.height * 0.25f;
    const float middle_y   = bounds.y + bounds.height * 0.45f;
    const float lower_y    = bounds.y + bounds.height * 0.75f;
    const float bottom_y   = bounds.y + bounds.height * 0.96f;
    const float left_x     = bounds.x + bounds.width  * 0.07f;
    const float right_x    = bounds.x + bounds.width  * 0.93f;

    const Vector2 top_face[4] = {{center_x, top_y}, {right_x, shoulder_y}, {center_x, middle_y}, {left_x, shoulder_y}};
    const Vector2 left_face[4] = {{left_x, shoulder_y}, {center_x, middle_y}, {center_x, bottom_y}, {left_x, lower_y}};
    const Vector2 right_face[4] = {{center_x, middle_y}, {right_x, shoulder_y}, {right_x, lower_y}, {center_x, bottom_y}};

    constexpr float LEFT_BRIGHTNESS  = 0.72f;
    constexpr float RIGHT_BRIGHTNESS = 0.86f;

    // A side-inset block (cactus) has a transparent rim its in-world model
    // hides by pulling the sides inward; a flat isometric icon can't do
    // that, so it crops the rim out of the texture instead (top on all four
    // edges, sides left/right) - no see-through seams.
    Rectangle top_uv = top.uv, left_uv = left.uv, right_uv = right.uv;
    if (side_inset > 0.0f) {
        auto crop = [&](Rectangle uv, bool vertical) {
            const float dx = uv.width * side_inset;
            const float dy = vertical ? uv.height * side_inset : 0.0f;
            return Rectangle{uv.x + dx, uv.y + dy, uv.width - dx * 2.0f, uv.height - dy * 2.0f};
        };
        top_uv = crop(top_uv, true);
        left_uv = crop(left_uv, false);
        right_uv = crop(right_uv, false);
    }

    rlSetTexture(atlas.id);
    rlBegin(RL_QUADS);
    // Sides first, then the top, so the upper face owns their shared seam
    // even for translucent block textures.
    atlas_quad(left_uv, left_face, shade(left.tint, LEFT_BRIGHTNESS), atlas);
    atlas_quad(right_uv, right_face, shade(right.tint, RIGHT_BRIGHTNESS), atlas);
    atlas_quad(top_uv, top_face, top.tint, atlas);
    rlEnd();
    rlSetTexture(0);
}

void draw_shaped_icon(Rectangle bounds, const Texture2D& atlas, const BlockShapeBoxes& item_shape, const CubeIconFace& top,
                      const CubeIconFace& left, const CubeIconFace& right)
{
    // Same isometric view as the full-cube icon below (whose fixed
    // points this projection reproduces for a 0..1 cube), but drawn
    // per box of the block's item-form shape so a slab/stair/
    // trapdoor reads as one. Each visible face samples only its
    // matching part of the tile, oriented the way the cube icon's
    // own faces are (top: u along x, v along z; left/South: u along
    // x; right/East: u along -z; sides: v along -y) - so a full box
    // gives exactly the full-cube icon.
    auto project = [&](float px, float py, float pz) {
        return Vector2{
            bounds.x + bounds.width * (0.07f + 0.43f * (px - pz + 1.0f)),
            bounds.y + bounds.height * (0.04f + 0.205f * (px + pz) + 0.51f * (1.0f - py)),
        };
    };
    auto crop = [](Rectangle uv, float u0, float u1, float v0, float v1) {
        return Rectangle{uv.x + u0 * uv.width, uv.y + v0 * uv.height,
                         (u1 - u0) * uv.width, (v1 - v0) * uv.height};
    };

    BlockShapeBoxes shape = item_shape;
    // Painter's order: lower boxes first, then farther ones (the
    // viewer sits at +x/+z), so a stair's raised step is drawn over
    // the slab it stands on.
    std::sort(shape.boxes.begin(), shape.boxes.begin() + shape.count,
              [](const BoundingBox& a, const BoundingBox& b) {
                  if (a.min.y != b.min.y) return a.min.y < b.min.y;
                  return a.min.x + a.min.z < b.min.x + b.min.z;
              });

    rlSetTexture(atlas.id);
    rlBegin(RL_QUADS);
    for (int b = 0; b < shape.count; ++b) {
        const Vector3 lo = shape.boxes[b].min;
        const Vector3 hi = shape.boxes[b].max;
        Vector2 left_face[4] = {
            project(lo.x, hi.y, hi.z), project(hi.x, hi.y, hi.z),
            project(hi.x, lo.y, hi.z), project(lo.x, lo.y, hi.z),
        };
        Vector2 right_face[4] = {
            project(hi.x, hi.y, hi.z), project(hi.x, hi.y, lo.z),
            project(hi.x, lo.y, lo.z), project(hi.x, lo.y, hi.z),
        };
        Vector2 top_face[4] = {
            project(lo.x, hi.y, lo.z), project(hi.x, hi.y, lo.z),
            project(hi.x, hi.y, hi.z), project(lo.x, hi.y, hi.z),
        };
        atlas_quad(crop(left.uv, lo.x, hi.x, 1.0f - hi.y, 1.0f - lo.y), left_face,
                        shade(left.tint, 0.72f), atlas);
        atlas_quad(crop(right.uv, 1.0f - hi.z, 1.0f - lo.z, 1.0f - hi.y, 1.0f - lo.y), right_face,
                        shade(right.tint, 0.86f), atlas);
        atlas_quad(crop(top.uv, lo.x, hi.x, lo.z, hi.z), top_face,
                        top.tint, atlas);
    }
    rlEnd();
    rlSetTexture(0);
}
