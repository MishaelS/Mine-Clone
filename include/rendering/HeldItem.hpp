#pragma once

#include "items/Inventory.hpp"
#include "model/EntityModel.hpp"

#include "raylib.h"

// Which of a model's item slots (ModelPart::item_slot) a held stack goes
// in: "block" for a block drawn as a cube, "tool" for a tool, "item" for
// any other item or flat block sprite (a torch).
const char* held_item_slot(const ItemStack& stack);

// `stack` in `model`'s slot for it, in `pose`, at the current matrix (the
// model's own space, `scale` world units per model pixel): a block filling
// the slot's box, an item's sprite extruded across it. Nothing for an empty
// stack or a model without that slot. Draw it where transparent pixels are
// cut out (the world's entity shader, or entity_cutout_shader()).
void draw_held_item(const EntityModel& model, const ModelPose& pose, const ItemStack& stack, float scale, Color light);
