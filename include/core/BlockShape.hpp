#pragma once

#include "core/Block.hpp"

#include "raylib.h"

#include <array>
#include <cstdint>

// Per-shape collision/render geometry for a block whose footprint isn't
// simply "solid ? one full unit cube : nothing" - stairs, trapdoors, doors,
// beds, cake. Local tile space: (0,0,0) is this cell's own min corner,
// (1,1,1) its max corner - the same space World::collision_boxes_at()
// offsets by (x,y,z) for collision, and Chunk::build_mesh_data()'s
// BlockRenderShape::Shaped branch scales/offsets for its mesh geometry, so
// one box list drives both systems with no risk of them disagreeing.
struct BlockShapeBoxes {
    // No shaped block needs more than 2 boxes (stairs: bottom slab + step
    // quarter; every other shape needs 0 or 1) - fixed-capacity, so reading
    // a shape never allocates.
    static constexpr int MAX_BOXES = 2;
    std::array<BoundingBox, MAX_BOXES> boxes{};
    int count = 0;
};

// Per-instance state a shaped block's own geometry may depend on. Reused
// across every shaped BlockType rather than one struct per type, even
// though any given type only ever reads a few of these fields - see
// get_block_shape()'s own switch. Facing reuses HorizontalDirection (the
// same sparse orientation map Chest/Furnace/Stairs/... already populate,
// see Chunk::get_orientation()) rather than a parallel direction type.
struct BlockInstanceState {
    HorizontalDirection facing = HorizontalDirection::South;
    bool open = false;         // door / trapdoor
    bool top_half = false;     // trapdoor only (a door's own upper/lower half is a distinct BlockType, not a flag)
    bool hinge_right = false;  // door only - always false for now, see BlockShape.cpp's own comment
    uint8_t bite_count = 0;    // cake only, 0-6
};

// Packs/unpacks BlockInstanceState's fields that HorizontalDirection can't
// express into Chunk's block_state sparse map (see Chunk::get_block_state()/
// set_block_state()). A position that was never explicitly set reads back
// as all-zero: closed, bottom half, hinge-left, single chest, 0 bites -
// every field's own "nothing special" value, so a plain solid/ordinary
// block (which never has a block_state entry at all) never needs special-
// casing here.
namespace BlockStateBits {
    constexpr uint16_t OPEN = 1u << 0;         // door / trapdoor
    constexpr uint16_t TOP_HALF = 1u << 1;     // trapdoor only
    constexpr uint16_t HINGE_RIGHT = 1u << 2;  // door only

    // 0 = single, 1 = primary, 2 = secondary - large/double chest pairing only.
    constexpr uint16_t MULTIBLOCK_PART_SHIFT = 3;
    constexpr uint16_t MULTIBLOCK_PART_MASK = 0b11u << MULTIBLOCK_PART_SHIFT;

    // Cake, 0-6 - see get_block_shape(BlockType::Cake, ...).
    constexpr uint16_t BITE_COUNT_SHIFT = 5;
    constexpr uint16_t BITE_COUNT_MASK = 0b111u << BITE_COUNT_SHIFT;
}

enum class ChestPart : uint8_t { Single, Primary, Secondary };

// True only for the handful of BlockTypes get_block_shape() below actually
// has a case for - a plain solid or plain non-solid block never pays for a
// BlockShape lookup at all, see World::collision_boxes_at()'s own fast
// path. Mirrors (and is driven by) BlockProperties::has_custom_shape.
bool block_has_custom_shape(BlockType type);

// This shaped block's current box list, in local 0..1 tile space. Empty
// (count == 0) for a block with no collision at all despite having custom
// render geometry (none exist yet, but the shape stays representable).
// Never called (and gives an empty result) for a type block_has_custom_shape()
// reports false for.
BlockShapeBoxes get_block_shape(BlockType type, const BlockInstanceState& state);
