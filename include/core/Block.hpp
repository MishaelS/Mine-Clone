#pragma once

#include "raylib.h"

#include <cstdint>

// The id a Chunk stores per voxel cell. Kept tiny (1 byte) since a single
// 16x16x16 chunk holds 4096 of these. Every value except Air must have a
// matching "name" entry in assets/blocks.json.
enum class BlockType : uint8_t {
    Air,
    Grass,
    Dirt,
    OakPlanks,
    OakLog,
    Sand,
    Gravel,
    Stone,
    Andesite,
    Diorite,
    Granite,
    Cobblestone,
    CoalOre,
    CopperOre,
    DiamondOre,
    EmeraldOre,
    GoldOre,
    IronOre,
    LapisOre,
    Bedrock,
    Count, // not a real block; sentinel for table/array sizing
};

// One side of a cube, in the order Mesh Generation will iterate them.
enum class BlockFace : uint8_t {
    Top,
    Bottom,
    North,
    South,
    East,
    West,
};

// Everything Mesh Generation needs to know about a BlockType, looked up once
// per face while building a chunk's mesh (not stored per-block). Loaded from
// assets/blocks.json by Load_block_definitions().
struct BlockProperties {
    bool solid;        // occludes neighbor faces, blocks movement
    bool transparent;  // doesn't block light or occlude neighbors (air, later: glass/water)
    int  luminance;    // 0-15, block light emitted by this block (0 = none)

    // UV rectangle (0..1) within get_block_atlas_texture(), indexed by
    // BlockFace — every block's faces share one atlas texture, so a whole
    // chunk mesh draws with a single bound texture.
    Rectangle texture_uvs[6];
};

// Parses assets/blocks.json and fills the BlockType -> BlockProperties table,
// packing every referenced sprite into the shared block texture atlas.
// Call once after the window exists (texture loads need a GL context).
void Load_block_definitions();

const BlockProperties& get_block_properties(BlockType type);

// The single texture every BlockProperties::texture_uvs rectangle indexes
// into. Valid only after Load_block_definitions().
const Texture2D& get_block_atlas_texture();
