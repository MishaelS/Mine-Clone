#pragma once

#include "core/Block.hpp"

#include "raylib.h"

#include <array>
#include <cstdint>
#include <vector>

// Per-shape collision/render geometry for a block whose footprint isn't
// simply "solid ? one full unit cube : nothing" - stairs, trapdoors, doors,
// beds, cake. Local tile space: (0,0,0) is this cell's own min corner,
// (1,1,1) its max corner - the same space World::collision_boxes_at()
// offsets by (x,y,z) for collision, and Chunk::build_mesh_data()'s
// BlockRenderShape::Shaped branch scales/offsets for its mesh geometry, so
// one box list drives both systems with no risk of them disagreeing.
struct BlockShapeBoxes {
    // No shaped block needs more than 3 boxes (torch: its two crossed pairs
    // of side planes plus the cap; stairs 2; everything else 1) -
    // fixed-capacity, so reading a shape never allocates.
    static constexpr int MAX_BOXES = 3;
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

// A placed block's BlockInstanceState from its stored facing (Chunk::
// get_orientation()) and packed block_state bits (Chunk::get_block_state()).
BlockInstanceState unpack_block_state(HorizontalDirection facing, uint16_t packed);

// True only for the handful of BlockTypes get_block_shape() below actually
// has a case for - a plain solid or plain non-solid block never pays for a
// BlockShape lookup at all, see World::collision_boxes_at()'s own fast
// path. Mirrors (and is driven by) BlockProperties::has_custom_shape.
bool block_has_custom_shape(BlockType type);

// This shaped block's current box list, in local 0..1 tile space - its
// render geometry (BlockRenderShape::Shaped), and also its collision when
// block_has_custom_shape() is true. A torch is render-only: shaped here,
// but not custom_shape in blocks.json, so it still has no collision.
// Empty for a type with no shape.
BlockShapeBoxes get_block_shape(BlockType type, const BlockInstanceState& state);

// Vanilla's second shape: the outline/selection shape - what the crosshair
// ray hits (World::raycast()), what the black target outline and the
// breaking cracks are drawn around. Separate from collision, same as
// vanilla: a torch or a sapling has none of the latter but still has a
// small box to aim at. In order: blocks.json's own "outline" boxes (static
// shapes - torch, plants, cactus); else, for a custom_shape block, its
// collision shape (get_block_shape() - it depends on facing/open/half/
// bites); else one full cube. Local 0..1 cell space.
BlockShapeBoxes get_outline_shape(BlockType type, const BlockInstanceState& state);

// The line segments of `shape`'s outline, as vanilla draws the targeted
// block's frame: the edges of the boxes' union, so two touching boxes
// (a stair's slab and step) read as one L-shaped outline with no line
// across the seam between them. Same coordinate space as the boxes.
struct OutlineEdge { Vector3 from, to; };
std::vector<OutlineEdge> outline_edges(const BlockShapeBoxes& shape);

// Whether some box of `shape` has a face on the plane `plane` (a
// coordinate along `face`'s axis, in `shape`'s own space) facing back
// against `face` - i.e. lying flush on the other side of it - that fully
// covers `rect` (the covered face's own extent on the two other axes, in
// the same space). Used to drop a mesh face nobody can see: one box of a
// block hidden under another of the same block, or a face flush against
// an opaque neighbor (Chunk::build_mesh_data()).
bool shape_covers_face(const BoundingBox* boxes, int box_count, BlockFace face, float plane, const BoundingBox& rect);

// The shape a shaped block is drawn with when it isn't placed in the world
// - its inventory icon and its dropped-item entity - so a slab or stair
// reads as one instead of as a full cube of its texture. Default state
// (bottom half, closed, no bites), facing North so a stair's raised step
// sits at the back of the inventory's isometric view, same as vanilla.
BlockShapeBoxes get_item_shape(BlockType type);

// The part of a 16x16 tile that one face of a partial box actually covers
// (vanilla's own UV cropping): a slab's 8px-tall side samples only the
// matching 8 rows of the tile instead of squashing the whole tile into it,
// so texels keep their native size on every shaped block. `box` is in
// local 0..1 cell space; `tile` is any UV rect (normalized or pixel - the
// crop is proportional). A full 0..1 box returns `tile` unchanged, and each
// face's axis/flip matches an ordinary cube face's own texture orientation
// (Chunk.cpp's unit_cube_faces() / BlockMesh.cpp's FACES corner order), so
// a cropped face lines up with the same texels a full block would show at
// that spot. Shared by the chunk mesh, dropped-item cubes and inventory
// icons so all three agree.
Rectangle crop_tile_to_box(Rectangle tile, BlockFace face, const BoundingBox& box);

// One face of a placed shaped block, fully textured: which tile it uses
// (a cake's "cut" face once bitten, a bed half's "end" face on its outer
// end - see BlockProperties), cropped to the box (crop_tile_to_box()), and
// oriented - a door's broad faces and a bed's long sides mirrored
// (negative width) so their art reads the right way round, a bed's top
// turned to follow its facing.
struct ShapedFaceTexture {
    Rectangle uv{};
    // Quarter turns to apply by cycling the face's four corners (v1<-v2<-
    // v3<-v4) before texturing: same quad, same winding, texture rotated
    // 90 degrees per step. Only ever set for a full-footprint face (a bed's
    // top), where rotating doesn't disturb the crop.
    int quarter_turns = 0;
    // Not drawn at all - a bed half's face toward its own other half, which
    // would otherwise show through the gaps between the legs.
    bool hidden = false;
    // Drawn this far (in blocks) inward along the face's own normal -
    // rendering only, collision keeps the full box. A bed's underside sits
    // at the top of its frame rather than on the floor under the legs.
    float inset = 0.0f;
    // No per-direction shading (FACE_DIRECTION_SHADE) - vanilla's torch
    // model ("shade": false), so a torch reads evenly lit from every side.
    bool flat_shade = false;
};
ShapedFaceTexture shaped_face_texture(BlockType type, const BlockInstanceState& state,
                                      const BlockProperties& properties, BlockFace face, const BoundingBox& box);
