#pragma once

#include "raylib.h"

#include <cstdint>
#include <optional>
#include <string>

// The id a Chunk stores per voxel cell. Kept tiny (1 byte) since a single
// chunk column (CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE) holds tens of
// thousands of these. Every value except Air must have a matching "name"
// entry in assets/blocks.json.
enum class BlockType : uint8_t {
    Air,
    Grass,
    Dirt,
    Foliage,
    OakLog,
    OakPlanks,
    Sand,
    Gravel,
    Clay,
    Stone,
    Cobblestone,
    CoalOre,
    IronOre,
    GoldOre,
    DiamondOre,
    RedstoneOre,
    Bedrock,
    Water,
    Workbench,
    Glass,
    Ice,

    // Keep new values appended: chunk/player saves written by older builds
    // store the earlier numeric ids in memory while loaded.
    DoubleStoneSlab,
    Bricks,
    Tnt,
    IronBlock,
    GoldBlock,
    DiamondBlock,
    Chest,
    Bookshelf,
    MossyCobblestone,
    Obsidian,
    Sponge,
    WhiteWool,
    MobSpawner,
    SnowBlock,
    SnowyGrass,
    Cactus,
    NoteBlock,
    Jukebox,
    Furnace,
    LitFurnace,
    Dispenser,
    Netherrack,
    SoulSand,
    Glowstone,
    Piston,
    StickyPiston,
    SpruceLog,
    BirchLog,
    Pumpkin,
    JackOLantern,
    SpruceFoliage,
    BirchFoliage,
    LapisBlock,
    LapisOre,
    Sandstone,
    BlackWool,
    GrayWool,
    RedWool,
    PinkWool,
    GreenWool,
    LimeWool,
    BrownWool,
    YellowWool,
    BlueWool,
    LightBlueWool,
    PurpleWool,
    MagentaWool,
    CyanWool,
    OrangeWool,
    LightGrayWool,
    Lava,
    Count, // not a real block; sentinel for table/array sizing
};

enum class BlockSoundGroup : uint8_t {
    None,
    Grass,
    Dirt,
    Gravel,
    Stone,
    Wood,
    Sand,
    Snow,
    Glass,
    Cloth,
    Foliage,
    Metal,
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

// Minecraft's own fixed per-face directional shading: a flat multiplier per
// cube face direction, independent of any actual light source or AO - it's
// what makes a uniformly-lit cube still read as three-dimensional (see
// http://greyminecraftcoder.blogspot.com/2014/12/lighting-18.html: top full
// brightness, bottom halved, north/south 0.8, east/west 0.6). Indexed the
// same as BlockFace - the single source of truth Chunk.cpp's mesh brightness
// and every standalone block-cube render (dropped items, falling blocks)
// both multiply in, instead of each keeping its own hand-tuned copy.
constexpr float FACE_DIRECTION_SHADE[6] = {1.0f, 0.5f, 0.8f, 0.8f, 0.6f, 0.6f};

// Which tool a block is efficiently mined with - see BlockProperties::
// effective_tool and GameEngine.cpp's break_seconds_required(). Lives here,
// not in player/Item.hpp, because "what this block needs" is a property of
// the block, not of any particular tool; ItemProperties::tool_kind (Item.hpp)
// reuses this same enum for "what kind of tool this item is".
enum class ToolKind : uint8_t { None, Sword, Pickaxe, Shovel, Axe, Hoe };

// Everything Mesh Generation needs to know about a BlockType, looked up once
// per face while building a chunk's mesh (not stored per-block). Loaded from
// assets/blocks.json by Load_block_definitions().
struct BlockProperties {
    bool solid;        // occludes neighbor faces, blocks movement
    bool transparent;  // doesn't block light or occlude neighbors (air, later: glass/water)
    int  luminance;    // 0-15, block light emitted by this block (0 = none)

    // Drawn in its own pass, after every opaque block in the whole world,
    // with alpha blending on and depth *write* off (still depth *tested*,
    // so solid terrain in front of it still correctly hides it) - see
    // Chunk::build_mesh/draw_water() and World::draw(). Also changes face
    // culling: two adjacent blocks of the same translucent type (e.g. two
    // water blocks) don't draw the face between them, same as Minecraft
    // doesn't render the water-water (or glass-glass) boundary inside a
    // solid body of it - only transparent-to-different-block boundaries do.
    bool translucent;

    // Alpha-tested texture (leaves, later flowers): transparent texels are
    // discarded, visible texels render in the depth-writing opaque pass.
    // Independent from `transparent`, which describes light/face occlusion.
    bool cutout;

    // Most equal neighboring blocks hide their shared face. Leaves keep
    // those faces so a thicker crown naturally becomes darker inside.
    bool cull_same_faces;

    // Resolves into a variant set in assets/audio/sounds.json.
    BlockSoundGroup sound_group;

    // Seconds to break bare-handed - see GameEngine.cpp's
    // break_seconds_required(). A tool whose kind matches effective_tool
    // divides this by its own mining_speed_multiplier (Item.hpp).
    float hardness;

    // Which tool kind gets that speed bonus; ToolKind::None if no tool
    // helps (dirt-soft or unbreakable-by-tool-choice blocks alike).
    ToolKind effective_tool;

    // Relative to water = 1.0 - a dropped block/item of this type falls
    // faster the denser it is, and sinks in water above ~1.0, floats to
    // the surface below it (see DroppedItem::tick_physics()). Not a real
    // Minecraft mechanic (vanilla items all fall/sink identically - see
    // the comment on "density" in blocks.json) - this project's own
    // addition.
    float density;

    // UV rectangle (0..1) within get_block_atlas_texture(), indexed by
    // BlockFace - every block's faces share one atlas texture, so a whole
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
// its tile within assets/sprites/terrain.png - a fixed 16x16 grid of 16px
// tiles, shared by every block, no per-sprite packing needed - plus an
// optional "color" tint (see BlockProperties::texture_tints).
// Call once after the window exists (texture loads need a GL context).
void Load_block_definitions();

const BlockProperties& get_block_properties(BlockType type);

// The blocks.json "name" a BlockType was loaded from (e.g. "oak_planks"),
// for display purposes (the debug overlay's "Looking at" line). "air" for
// BlockType::Air, which has no blocks.json entry of its own.
const std::string& get_block_name(BlockType type);

// Reverse of get_block_name() - std::nullopt if `name` doesn't match any
// blocks.json entry. Used to deserialize a block by its stable, human-
// readable name (e.g. a saved player's hotbar) instead of its raw
// BlockType enum value, which isn't safe to persist across builds if the
// enum's own order ever changes.
std::optional<BlockType> block_type_from_name(const std::string& name);

// assets/sprites/terrain.png - the single texture every BlockProperties::
// texture_uvs rectangle indexes into. Valid only after Load_block_definitions().
const Texture2D& get_block_atlas_texture();

// Moves a tile rectangle half a texel inward on every edge. Mesh UVs that
// end exactly on an atlas-tile boundary can sample the neighboring tile,
// most visibly as wrong-colored seams on biome-tinted grass with bilinear
// filtering. Particle extraction still uses the original, full tile.
Rectangle get_sample_safe_block_uv(Rectangle uv);

// The raw (0..1) UV rectangle for terrain.png's (column, row) tile - the
// same lookup blocks.json's own "x"/"y" face coordinates resolve through,
// exposed for the handful of things that need an atlas tile that isn't a
// block face: the block-breaking crack overlay (row 15, columns 0-9 - see
// ui::block_breaking_overlay()).
Rectangle block_atlas_tile_uv(int column, int row);
