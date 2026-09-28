#pragma once

#include "raylib.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>

// The id a Chunk stores per voxel cell. Kept tiny (1 byte) since a single
// chunk column (CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE) holds tens of
// thousands of these. Every value except Air must have a matching
// content::block() line in src/content/Blocks.cpp (see content/Content.hpp).
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
    ShortGrass,
    // Only oak has a sapling block yet (spruce/birch leaves drop the
    // ItemType::Sapling item instead). Plantable on Grass/Dirt only
    // (World::place_block) - grows into the structure whose file says it
    // "grows_from" it (content/Behaviors.cpp's GrowsIntoStructure).
    OakSapling,

    // Light sources. RedstoneTorch is the unlit state (see World::place_block - only
    // Torch/LitRedstoneTorch are directly placeable; nothing here ever
    // flips one to the other yet, since there's no redstone-signal system
    // to drive it - RedstoneTorch exists so breaking a lit one has
    // somewhere well-defined to return to, same as Furnace/LitFurnace).
    Torch,
    RedstoneTorch,
    LitRedstoneTorch,

    // Shaped blocks (BlockRenderShape::Shaped, core/BlockShape.hpp's
    // get_block_shape()) - partial-cube collision/geometry instead of a
    // plain full-cube-or-nothing block.
    OakStairs,
    OakTrapdoor,
    OakDoorLower,
    OakDoorUpper,
    IronDoorLower,
    IronDoorUpper,
    BedHead,
    BedFoot,
    Cake,
    OakSlab,
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

// Which way a placed directional block (Furnace/Workbench/Dispenser/
// Pumpkin/JackOLantern - anything whose definition gives a distinct .south()
// front-face texture) is facing, i.e. which world-facing mesh face shows
// that front texture instead of the plain "side" one - see Chunk::
// get_orientation()/set_orientation(). Values match BlockFace's own
// North/South/East/West ordering (offset by 2) so a cast between them is a
// simple, obviously-correct subtraction rather than a lookup table.
enum class HorizontalDirection : uint8_t {
    North,
    South,
    East,
    West,
};

// The (dx, dz) unit step for walking one cell in this direction - North/
// South are -Z/+Z, East/West are +X/-X, same convention CUBE_FACES (Chunk.cpp)
// and this enum's own comment use. Used anywhere a stored facing needs to
// become an actual neighbor offset: a stair/trapdoor's own shape geometry
// (core/BlockShape.hpp), and a door/bed's paired second half (World::
// place_door()/place_bed()).
struct DirectionOffset { int dx, dz; };

// The unit step along a cube face's own outward normal (Top = +Y, Bottom =
// -Y, North/South = -Z/+Z, East/West = +X/-X - see BlockFace). Used
// wherever a face has to become an actual neighbor offset: which cell a
// block is attached to (see BlockProperties::attach_*), which way it looks.
struct FaceOffset { int dx, dy, dz; };
FaceOffset block_face_offset(BlockFace face);
DirectionOffset horizontal_direction_offset(HorizontalDirection direction);

// 90 degrees clockwise as seen from above (North->East->South->West->North) -
// the large/double chest's own primary/secondary pairing rule (World::
// place_chest()) is expressed relative to this, not to the pair's own axis.
HorizontalDirection horizontal_direction_right_of(HorizontalDirection direction);

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
// not in items/Item.hpp, because "what this block needs" is a property of
// the block, not of any particular tool; ItemProperties::tool_kind (Item.hpp)
// reuses this same enum for "what kind of tool this item is".
enum class ToolKind : uint8_t { None, Sword, Pickaxe, Shovel, Axe, Hoe };

// Physical cubes use the normal six-face mesh. Cross blocks (grass and
// future flowers) are two intersecting, double-sided vertical quads.
// Shaped blocks (stairs, trapdoors, doors, beds, cake) draw whatever box
// list core/BlockShape.hpp's get_block_shape() reports for this instance,
// each box rendered as its own mini six-face cube - see
// Chunk::build_mesh_data()'s own Shaped branch.
enum class BlockRenderShape : uint8_t { Cube, Cross, Shaped };

// What shape a block is, and so how it behaves - not which block it is: any
// block of a kind gets that kind's geometry (core/BlockShape.hpp) and its
// placing/using rules (GameEngine), a stone slab exactly like an oak one.
//   Cube     - an ordinary full block
//   Slab     - half a block, bottom or top by where it's clicked; two
//              halves in one cell become its "double" block
//   Stairs   - a slab plus a step, turned toward the player
//   Trapdoor - a thin panel on the floor or ceiling that opens up
//   Cake     - eaten a slice at a time
//   Torch    - a stick on the floor or a wall, no collision
//   Door, Bed - two-cell blocks (their halves are separate blocks)
//   Cross    - a plant: two crossed planes, no collision
//   Fluid    - water, lava: flows and is drawn by the game's own fluid code
enum class BlockShapeKind : uint8_t { Cube, Slab, Stairs, Trapdoor, Cake, Torch, Door, Bed, Cross, Fluid, Count };

// A particle a block gives off now and then (BlockProperties::particles) -
// Minecraft's animateTick: a torch's flame and smoke, a lit redstone
// torch's red dust, a lit furnace's fire, leaves drifting down.
enum class BlockParticleKind : uint8_t { Flame, Smoke, Dust, Leaf };

struct BlockParticleEmitter {
    BlockParticleKind kind = BlockParticleKind::Smoke;
    // Where, in cell units, in the block's own layout: its model's (moved
    // and tilted with it on a wall - BlockStateModel), its front facing
    // south (turned with a directional block's facing).
    Vector3 at{0.5f, 0.5f, 0.5f};
    Vector3 spread{0, 0, 0}; // +- this much at random along each axis
    float chance = 1.0f;     // per animate tick of the block
    int count = 1;           // particles each time
    Color color = WHITE;     // a leaf ignores it: it takes its block's own color
    bool only_above_air = false; // leaves: only from an underside that's open
};

// A face recolored per column by the biome it's in instead of its own tint
// (which is then only its color out of the world): grass (a grass block's
// top, tall grass) or foliage (leaves) - Minecraft's biome colormap idea.
enum class BiomeTint : uint8_t { None, Grass, Foliage };

// How a block looks and is aimed at in one of its states (a torch: on the
// floor, on a wall - see shape_state_index() in core/BlockShape.hpp): its
// own hitbox, and its model moved and then tilted about a pivot. Cell units
// (0..1), laid out for a block on the north wall - on another wall the
// whole thing is turned to it (place_model_point(), place_hitbox()).
struct BlockStateModel {
    bool has_hitbox = false;                       // else the hitbox is its shape (or a full cube)
    BoundingBox hitbox{{0, 0, 0}, {1, 1, 1}};      // what the crosshair aims at and is outlined
    Vector3 offset{0, 0, 0};                       // the model moved by this...
    Vector3 pivot{0.5f, 0, 0.5f};                  // ...then turned about this point
    float angle = 0.0f;                            // degrees about X: its top leans toward +z (out of a north wall)
};
constexpr int MAX_BLOCK_STATES = 2;

// Which way a face of a model part (BlockElement) is seen from: outside
// (its normal pointing out of the part - an ordinary face), inside
// (turned round - visible from within the part) or from both sides.
enum class ElementNormal : uint8_t { Out, In, Both };

struct BlockElementFace {
    bool enabled = true;
    Rectangle uv{0, 0, 1, 1}; // the part of that side's tile it shows, 0..1 (x, y, width, height)
    ElementNormal normal = ElementNormal::Out;
};

// One box of a block's own model, like a Blockbench element: drawn from
// its enabled faces, each textured with the block's tile for that side.
// Cell units (0..1).
struct BlockElement {
    BoundingBox box{{0, 0, 0}, {1, 1, 1}};
    bool shade = true; // per-direction shading (off for a torch: evenly lit)
    std::array<BlockElementFace, 6> faces{};
};

// Everything Mesh Generation needs to know about a BlockType, looked up once
// per face while building a chunk's mesh (not stored per-block). Loaded from
// src/content/Blocks.cpp by Load_block_definitions().
// One value a placed block of some type carries besides its type - a
// crop's growth stage ("age", 0..7), wet farmland ("moist", 0..1). Declared
// per block (its file's "properties" - content/BlockFile.hpp), read and
// written by its behaviors (world/BlockBehavior.hpp). All of a block's
// values are packed into one 32-bit word per cell (Chunk::
// get_state_values()): this one's `bits` bits from `shift`, stored XORed
// with its default so a cell never written reads back every default.
struct BlockStateProperty {
    std::string name;
    int max = 1;           // values 0..max (1: a yes/no)
    int default_value = 0;
    int shift = 0;
    int bits = 1;
};

// Its value out of a cell's packed values, and `packed` with it set.
int read_state_property(const BlockStateProperty& property, uint32_t packed);
uint32_t write_state_property(const BlockStateProperty& property, uint32_t packed, int value);

struct BlockProperties {
    bool solid;        // occludes neighbor faces, blocks movement
    bool transparent;  // doesn't block light or occlude neighbors (air, later: glass/water)
    bool selectable;   // raycasts can target/break it even when it has no collision
    bool replaceable;  // placing a block may replace this cell directly
    int  luminance;    // 0-15, block light emitted by this block (0 = none)
    BlockRenderShape render_shape;

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
    // BlockDef::density()) - this project's own
    // addition.
    float density;

    // True for a block whose collision/render geometry isn't just "solid ?
    // one full unit cube : nothing" - stairs, trapdoors, doors, beds, cake
    // (see core/BlockShape.hpp's get_block_shape()). False for the
    // overwhelming majority of blocks, which never pay for a BlockShape
    // lookup at all - see World::collision_boxes_at()'s own fast path and
    // Chunk::build_mesh_data()'s BlockRenderShape::Shaped branch.
    bool has_custom_shape;

    // How far (in blocks - BlockDef::side_inset() takes texture pixels, 1 =
    // 1/16) the four side faces are drawn inward from the cell edge, top/
    // bottom untouched - vanilla's cactus model: its 14x14 top/bottom art
    // then meets the side faces exactly, and the full-width spike rows of
    // the side texture stick out past the body's edges. 0 for every
    // ordinary block. Rendering only - collision is unaffected.
    float side_inset;

    // Where this block may be mounted, vanilla's own AttachFace idea: on
    // the floor (support below), on a wall (support to the side), on the
    // ceiling (support above) - BlockDef::attach_floor()/attach_wall()/
    // attach_ceiling(). The floor and the ceiling need a box under/over
    // the cell's middle; a wall needs a whole face - a full block's side,
    // not a slab's or a door's (see World.cpp's has_full_side_support()).
    // All false for an ordinary block, which needs no support at
    // all. A block with any of these placed by the player stores which
    // face it actually ended up on (BlockInstanceState::attachment) and is
    // broken off automatically once that support goes away (World::
    // attachment_has_support()).
    bool attach_floor;
    bool attach_wall;
    bool attach_ceiling;

    // True for a block that damages on contact regardless of whether it
    // blocks movement (cactus) - generalizes what used to be a single
    // hardcoded BlockType::Cactus check in entities/Player.cpp's
    // box_touches_cactus() into a data-driven one any future block can opt
    // into by name alone.
    bool damages_on_touch;

    // Has a front (its south face's texture) that turns to face the player
    // who placed it, the other sides all showing its east face's texture -
    // furnaces, chests, workbenches, pumpkins. See block_is_directional().
    bool directional;

    // Its shape and so its behavior - see BlockShapeKind.
    BlockShapeKind shape_kind;
    // A slab's two halves in one cell turn into this block (oak slab ->
    // oak planks). Air for any other block.
    BlockType double_block;
    // Shown as this flat sprite from sprites/items.png ({column, row}) in
    // the inventory and as a dropped item instead of a small 3D copy - a
    // torch, a cake. -1 for none.
    int item_sprite_x;
    int item_sprite_y;
    // Its hitbox and model placement per state - see BlockStateModel.
    std::array<BlockStateModel, MAX_BLOCK_STATES> state_models;
    // A two-cell block (a door, a bed) is two blocks, one per cell:
    // `pair_half` says which this is - 0 the cell it's placed at (a door's
    // lower half, a bed's foot), 1 the other (the door's upper half, one
    // up; the bed's head, one along its facing) - and `partner` the other
    // half's block. Only one half is an item (`is_item`): the other can't
    // be picked or placed alone and drops that one. Air/0/true for
    // everything else.
    BlockType partner;
    uint8_t pair_half;
    bool is_item;
    // A directional block that joins a same-facing neighbor beside it into
    // one wide block (two chests into a large chest): its front and back
    // then show these wide-art halves instead - fronts [0] for the half
    // with its partner on the right of its facing (ChestPart::Primary), [1]
    // for the other; backs [2] and [3] the same way.
    bool joins_sideways;
    std::array<Rectangle, 4> joined_uvs;
    // Faces recolored by their biome (BiomeTint), indexed by BlockFace.
    std::array<BiomeTint, 6> biome_tints;
    // Can only be placed on (and stays only on) one of these - a plant's
    // soil. Empty: anywhere.
    std::vector<BlockType> placed_on;
    // What it gives off on its animate ticks (BlockParticleEmitter).
    std::vector<BlockParticleEmitter> particles;
    // The values a placed one carries (BlockStateProperty) - none for most.
    std::vector<BlockStateProperty> state_properties;
    // Its own model from parts (a torch's stick and flame) - drawn instead
    // of its shape's boxes when not empty. Rendering only: collision and
    // the hitbox stay its shape's / its state's.
    std::vector<BlockElement> elements;

    // UV rectangle (0..1) within get_block_atlas_texture(), indexed by
    // BlockFace - every block's faces share one atlas texture, so a whole
    // chunk mesh draws with a single bound texture.
    Rectangle texture_uvs[6];

    // Per-face tint, indexed by BlockFace, multiplied into the sampled texel
    // alongside AO/light shading (see Chunk::append_face). WHITE leaves the
    // tile's own colors untouched; a definition sets anything else only for a
    // tile that's deliberately colorless art meant to be recolored in code
    // (e.g. grass top), same idea as Minecraft's biome-tinted grass overlay.
    Color texture_tints[6];

    // Optional BlockDef::cut() face - the cross-section shown on a
    // partially eaten cake's open side instead of its ordinary side
    // texture (see Chunk::build_mesh_data()'s Shaped branch). Tinted the
    // same as the side faces.
    std::optional<Rectangle> cut_texture_uv;

    // Optional BlockDef::end() face - the outer end of a two-cell block
    // (a bed half's headboard or foot end), used instead of "side" on that
    // one face; "side" then covers the long sides. See core/BlockShape.hpp's
    // shaped_face_texture().
    std::optional<Rectangle> end_texture_uv;
};

// Fills the BlockType -> BlockProperties table from src/content/Blocks.cpp
// (see content/Content.hpp). Each face is a content::Tile - the (x, y) grid
// position of its tile within assets/sprites/terrain.png, a fixed 16x16
// grid of 16px tiles shared by every block - plus an optional tint (see
// BlockProperties::texture_tints). Throws if any BlockType is left
// undefined. Call once after the window exists (texture loads need a GL
// context).
// Room for every block id a uint8_t BlockType can hold: the named ones
// above plus any assets/blocks/*.json adds beyond them.
constexpr int MAX_BLOCK_TYPES = 256;

void Load_block_definitions();

const BlockProperties& get_block_properties(BlockType type);

// Every defined block but Air, by id - the named BlockTypes and any the
// block files add. Use this instead of counting up to BlockType::Count.
const std::vector<BlockType>& all_block_types();

// True for a block whose definition gives it a distinct .south()
// front-face texture (Chest/Furnace/LitFurnace/Workbench/Dispenser/Pumpkin/
// JackOLantern) - the only ones a per-instance HorizontalDirection actually
// changes anything for. Used by both Chunk::build_mesh_data() (which face
// shows that front texture) and block-placement code (whether it's worth
// calling World::set_block_orientation() at all).
bool block_is_directional(BlockType type);

// True for block_is_directional()'s 7 types, plus OakStairs/OakTrapdoor -
// blocks that need a stored HorizontalDirection for their own shape
// geometry (get_block_shape()) rather than a front-texture remap.
// Door/Bed/Chest are deliberately excluded: each sets orientation on both
// of its paired cells itself, as part of one atomic placement (World::
// place_door()/place_bed()/place_chest()), not as a GameEngine follow-up
// step the way this predicate drives for every other directional block.
bool block_needs_facing(BlockType type);

// Whether `type` may stand on `below` - a plant on its soil (BlockProperties::
// placed_on); true for any block that doesn't care.
bool block_can_stay_on(BlockType type, BlockType below);

// Its state property called `name`, or nullptr.
const BlockStateProperty* find_state_property(BlockType type, const std::string& name);

// True for a block that mounts onto something (see BlockProperties::
// attach_*) - placed through World::place_attached_block() rather than the
// plain place_block() path.
bool block_is_attachable(BlockType type);

// The content::block() name a BlockType was defined with (e.g. "oak_planks"),
// for display purposes (the debug overlay's "Looking at" line). "air" for
// BlockType::Air, which has no definition of its own.
const std::string& get_block_name(BlockType type);

// Reverse of get_block_name() - std::nullopt if `name` doesn't match any
// defined block. Used to deserialize a block by its stable, human-
// readable name (e.g. a saved player's hotbar) instead of its raw
// BlockType enum value, which isn't safe to persist across builds if the
// enum's own order ever changes.
std::optional<BlockType> block_type_from_name(const std::string& name);

// assets/sprites/terrain.png - the single texture every BlockProperties::
// texture_uvs rectangle indexes into. Valid only after Load_block_definitions().
const Texture2D& get_block_atlas_texture();

// Moves a tile rectangle a tiny sub-texel distance inward. This excludes
// the neighboring atlas tile without distorting the first/last pixels under
// point filtering. Particle extraction still uses the original full tile.
Rectangle get_sample_safe_block_uv(Rectangle uv);

// The raw (0..1) UV rectangle for terrain.png's (column, row) tile - the
// same lookup block definitions' content::Tile face coordinates resolve through,
// exposed for the handful of things that need an atlas tile that isn't a
// block face: the block-breaking crack overlay (row 15, columns 0-9 - see
// ui::block_breaking_overlay()).
Rectangle block_atlas_tile_uv(int column, int row);
