#pragma once

#include "model/EntityModel.hpp"

#include "raylib.h"

// Draws `model` in `pose` at the current rlgl matrix, `scale` world units
// per model pixel (1/16 = one block per 16 pixels). `skin` may be an
// invalid texture (id 0) - the cubes are then drawn plain white, so a model
// can be built before it has a skin. Every face is multiplied by `tint`
// (lighting) and a fixed per-direction shade.
void draw_entity_model(const EntityModel& model, const Texture2D& skin, const ModelPose& pose, float scale,
                       Color tint = WHITE);

// A shader that leaves a skin's transparent pixels out altogether (not
// even the depth buffer) - for drawing a model where no such shader is
// already active (the game's world shader does this itself), so the
// decoration layer's empty pixels never hide what's behind them. Loaded on
// first use; needs a GL context.
const Shader& entity_cutout_shader();

// Applies `part`'s pose to the current rlgl matrix, including every parent's
// - the transform its cubes and pivot are drawn in. For editor gizmos drawn
// on top of a part (outline, pivot marker).
void push_part_transform(const EntityModel& model, const ModelPose& pose, int part, float scale);

// A cube's own fixed rotation (ModelCube::rotation) as a matrix in world
// units - identity for an unrotated cube. raymath order (applies first to
// the vertex).
Matrix cube_rotation_matrix(const ModelCube& cube, float scale);

// Where each side of `cube` (one of `part`'s) sits on the skin (skin
// pixels), in MODEL_FACE_IDS order: the standard Minecraft box layout from
// its uv, sized like the cube is drawn (cube_draw_bounds() - so the texture
// is never stretched) or like `size` for ModelCube::stretch_texture, with
// any per-side override (ModelCube::face_uv) applied.
std::array<Rectangle, 6> cube_face_uvs(const ModelPart& part, const ModelCube& cube);
