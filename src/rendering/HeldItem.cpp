#include "rendering/HeldItem.hpp"
#include "model/EntityModelRenderer.hpp"
#include "rendering/BlockMesh.hpp"

#include "rlgl.h"

namespace {
    // The flat sprite a stack shows as, if it isn't a cube.
    std::optional<Rectangle> held_sprite(const ItemStack& stack)
    {
        if (stack.holds_item()) return get_item_properties(stack.tool).atlas_source;
        return get_block_item_sprite(stack.block);
    }
}

const char* held_item_slot(const ItemStack& stack)
{
    if (!held_sprite(stack)) return "block";
    return stack.is_tool() ? "tool" : "item";
}

void draw_held_item(const EntityModel& model, const ModelPose& pose, const ItemStack& stack, float scale, Color light)
{
    if (stack.empty()) return;
    const std::optional<Rectangle> sprite = held_sprite(stack);
    if (!push_item_slot(model, pose, held_item_slot(stack), scale)) return;
    if (sprite) draw_extruded_sprite(get_item_atlas_texture(), *sprite, light);
    else draw_block_cube(stack.block, 255, std::nullopt, light);
    rlPopMatrix();
}
