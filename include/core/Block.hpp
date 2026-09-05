#pragma once

#include "raylib.h"

#include <cstdint>
#include <string>

// The id a Chunk stores per voxel cell. Kept tiny (1 byte) since a single
// chunk column (CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE) holds tens of
// thousands of these. Every value except Air must have a matching "name"
// entry in assets/blocks.json.
enum class BlockType : uint8_t {
    Air,
    Grass,
    Dirt,
    OakLog,
    OakPlanks,
    Sand,
    Gravel,
    Stone,
    Cobblestone,
    CoalOre,
    IronOre,
    GoldOre,
    DiamondOre,
    RedstoneOre,
    Bedrock,
    Water,
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

    // Per-face tint, indexed by BlockFace, multiplied into the sampled texel
    // alongside AO/light shading (see Chunk::append_face). WHITE leaves the
    // tile's own colors untouched; blocks.json sets anything else only for a
    // tile that's deliberately colorless art meant to be recolored in code
    // (e.g. grass top), same idea as Minecraft's biome-tinted grass overlay.
    Color texture_tints[6];
};

// Parses assets/blocks.json and fills the BlockType -> BlockProperties table.
// Each face entry ("top"/"bottom"/"side") gives the (x, y) grid position of
// its tile within assets/sprites/terrain.png — a fixed 16x16 grid of 16px
// tiles, shared by every block, no per-sprite packing needed — plus an
// optional "color" tint (see BlockProperties::texture_tints).
// Call once after the window exists (texture loads need a GL context).
void Load_block_definitions();

const BlockProperties& get_block_properties(BlockType type);

// The blocks.json "name" a BlockType was loaded from (e.g. "oak_planks"),
// for display purposes (the debug overlay's "Looking at" line). "air" for
// BlockType::Air, which has no blocks.json entry of its own.
const std::string& get_block_name(BlockType type);

// assets/sprites/terrain.png — the single texture every BlockProperties::
// texture_uvs rectangle indexes into. Valid only after Load_block_definitions().
const Texture2D& get_block_atlas_texture();
