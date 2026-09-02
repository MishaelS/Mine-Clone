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

    Texture2D textures[6]; // indexed by BlockFace
};

// Parses assets/blocks.json and fills the BlockType -> BlockProperties table.
// Call once after the window exists (texture loads need a GL context).
void Load_block_definitions();

const BlockProperties& get_block_properties(BlockType type);
