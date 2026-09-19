#include "core/BlockShape.hpp"

namespace {
    // Real door/trapdoor panel thickness (3/16 of a block, same as vanilla).
    constexpr float PANEL_THICKNESS = 0.1875f;

    BlockShapeBoxes stairs_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 2;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 1.0f}}; // bottom slab, always present
        // Quarter step, on the footprint half `facing` points toward -
        // verify empirically against a placed stair from every approach
        // angle; flip the four cases below (a one-line change) if it reads
        // backwards, not a redesign.
        switch (state.facing) {
            case HorizontalDirection::South: result.boxes[1] = {{0.0f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::North: result.boxes[1] = {{0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.5f}}; break;
            case HorizontalDirection::East:  result.boxes[1] = {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::West:  result.boxes[1] = {{0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 1.0f}}; break;
        }
        return result;
    }

    BlockShapeBoxes trapdoor_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 1;
        if (!state.open) {
            result.boxes[0] = state.top_half
                ? BoundingBox{{0.0f, 1.0f - PANEL_THICKNESS, 0.0f}, {1.0f, 1.0f, 1.0f}}
                : BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, PANEL_THICKNESS, 1.0f}};
            return result;
        }
        // Swings up to vertical, flush against the wall on the hinge side -
        // opposite the stored facing (facing points from the hinge toward
        // the open room). Verify empirically, flip if backwards.
        switch (state.facing) {
            case HorizontalDirection::South: result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, PANEL_THICKNESS}}; break;
            case HorizontalDirection::North: result.boxes[0] = {{0.0f, 0.0f, 1.0f - PANEL_THICKNESS}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::East:  result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {PANEL_THICKNESS, 1.0f, 1.0f}}; break;
            case HorizontalDirection::West:  result.boxes[0] = {{1.0f - PANEL_THICKNESS, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}; break;
        }
        return result;
    }

    // Shared by all 4 door BlockTypes (oak/iron x lower/upper) - only the
    // texture differs between them, the shape logic is identical.
    BlockShapeBoxes door_shape(const BlockInstanceState& state)
    {
        bool facing_is_z = state.facing == HorizontalDirection::North || state.facing == HorizontalDirection::South;
        BlockShapeBoxes result;
        result.count = 1;
        if (!state.open) {
            // Closed: thin along the facing axis (centered), full width
            // along the perpendicular axis - blocks travel straight through
            // the doorway regardless of which side the hinge is on, same as
            // a real closed door.
            result.boxes[0] = facing_is_z
                ? BoundingBox{{0.0f, 0.0f, 0.5f - PANEL_THICKNESS * 0.5f}, {1.0f, 1.0f, 0.5f + PANEL_THICKNESS * 0.5f}}
                : BoundingBox{{0.5f - PANEL_THICKNESS * 0.5f, 0.0f, 0.0f}, {0.5f + PANEL_THICKNESS * 0.5f, 1.0f, 1.0f}};
            return result;
        }
        // Open: rotated 90 degrees onto the perpendicular axis, flush
        // against the hinge-side edge - hinge_right is always false this
        // pass (fixed left hinge, see BlockInstanceState's own comment), so
        // that's always the low (0) end for now.
        if (facing_is_z) {
            result.boxes[0] = state.hinge_right
                ? BoundingBox{{1.0f - PANEL_THICKNESS, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}
                : BoundingBox{{0.0f, 0.0f, 0.0f}, {PANEL_THICKNESS, 1.0f, 1.0f}};
        } else {
            result.boxes[0] = state.hinge_right
                ? BoundingBox{{0.0f, 0.0f, 1.0f - PANEL_THICKNESS}, {1.0f, 1.0f, 1.0f}}
                : BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, PANEL_THICKNESS}};
        }
        return result;
    }

    BlockShapeBoxes bed_shape()
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5625f, 1.0f}}; // low mattress, no facing dependency
        return result;
    }

    BlockShapeBoxes cake_shape(const BlockInstanceState& state)
    {
        constexpr float MARGIN = 1.0f / 16.0f;
        constexpr float SLICE_WIDTH = 2.0f / 16.0f; // real Minecraft's own per-bite width
        float eaten = state.bite_count * SLICE_WIDTH;

        BlockShapeBoxes result;
        result.count = 1;
        BoundingBox box{{MARGIN, 0.0f, MARGIN}, {1.0f - MARGIN, 0.5f, 1.0f - MARGIN}};
        // Shrinks from whichever edge `facing` points to - the direction
        // the player was facing on the first bite (see Chunk::
        // build_mesh_data()'s Shaped branch / GameEngine's eat-cake
        // handling), not vanilla's own fixed world direction - a
        // deliberate simplification, not a functional gap.
        switch (state.facing) {
            case HorizontalDirection::South: box.min.z += eaten; break;
            case HorizontalDirection::North: box.max.z -= eaten; break;
            case HorizontalDirection::East:  box.min.x += eaten; break;
            case HorizontalDirection::West:  box.max.x -= eaten; break;
        }
        result.boxes[0] = box;
        return result;
    }
}

bool block_has_custom_shape(BlockType type)
{
    return get_block_properties(type).has_custom_shape;
}

BlockShapeBoxes get_block_shape(BlockType type, const BlockInstanceState& state)
{
    switch (type) {
        case BlockType::OakStairs:   return stairs_shape(state);
        case BlockType::OakTrapdoor: return trapdoor_shape(state);
        case BlockType::OakDoorLower:
        case BlockType::OakDoorUpper:
        case BlockType::IronDoorLower:
        case BlockType::IronDoorUpper:
            return door_shape(state);
        case BlockType::BedHead:
        case BlockType::BedFoot:
            return bed_shape();
        case BlockType::Cake:
            return cake_shape(state);
        default:
            return BlockShapeBoxes{};
    }
}
