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

// Applies `part`'s pose to the current rlgl matrix, including every parent's
// - the transform its cubes and pivot are drawn in. For editor gizmos drawn
// on top of a part (outline, pivot marker).
void push_part_transform(const EntityModel& model, const ModelPose& pose, int part, float scale);
