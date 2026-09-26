#pragma once

#include "core/Block.hpp"
#include "items/Inventory.hpp" // ItemRef
#include "items/Item.hpp"

#include "raylib.h"

#include <initializer_list>

// Every block, item, drop rule, crafting recipe and furnace recipe is
// declared in code, in src/content/ - one file per kind, one line per
// thing. This header is the whole API those files use.
//
// Adding a new block:
//   1. Append a value to BlockType (core/Block.hpp).
//   2. One content::block(...) line in src/content/Blocks.cpp.
//   3. Optionally: a when_broken(...) rule (Drops.cpp - without one the
//      block simply drops itself), recipes (Recipes.cpp), smelting/fuel
//      (Smelting.cpp), and "block.<name>" in assets/translations/*.json.
//
// Adding a new item:
//   1. Append a value to ItemType (items/Item.hpp).
//   2. One content::tool/material/food(...) line in src/content/Items.cpp.
//   3. Optionally: recipes, drops, smelting, and "item.<name>" translations.
//
// A BlockType/ItemType left without a definition throws at startup, the
// same way a missing JSON entry used to.
namespace content {

// A 16x16 tile's column/row - in sprites/terrain.png for block faces, in
// sprites/items.png for item sprites.
struct Tile { int x, y; };

// ---------------------------------------------------------------- Blocks --

// Builder returned by content::block(). Every setter can be chained in any
// order: a more specific face always wins over a broader one (.south()
// over .side() over .all()), and an explicit .hardness()/.tool()/
// .density() always wins over the defaults .sound() picks for its material
// family. Anything not called keeps the default of an ordinary opaque,
// solid, stone-sounding full cube.
class BlockDef {
public:
    explicit BlockDef(BlockType type) : type_(type) {}

    // Sound group, plus default hardness/tool/density for that material
    // family (see physical_defaults_for() in Block.cpp).
    BlockDef& sound(BlockSoundGroup group);
    BlockDef& hardness(float seconds);
    BlockDef& tool(ToolKind kind);
    BlockDef& density(float relative_to_water);
    BlockDef& luminance(int level); // 0-15

    BlockDef& non_solid();          // no collision; also replaceable unless .replaceable(false)
    BlockDef& not_selectable();     // raycasts pass through (water, lava)
    BlockDef& replaceable(bool value = true);
    BlockDef& transparent();
    BlockDef& translucent();        // alpha-blended pass - see BlockProperties::translucent
    BlockDef& cutout();             // alpha-tested texture
    BlockDef& keep_same_faces();    // don't cull faces between two of this block (leaves)
    BlockDef& damages_on_touch();
    BlockDef& side_inset(int pixels); // cactus-style inset side faces, in texture pixels

    BlockDef& cross();              // BlockRenderShape::Cross (plants)
    BlockDef& shaped();             // BlockRenderShape::Shaped (torches, stairs, doors, ...)
    BlockDef& custom_shape();       // non-cube collision - see BlockProperties::has_custom_shape

    BlockDef& attach_floor();
    BlockDef& attach_wall();
    BlockDef& attach_ceiling();

    // Face textures. `tint` is multiplied into the tile (alpha = opacity
    // for translucent blocks) - see BlockProperties::texture_tints.
    BlockDef& all(Tile tile, Color tint = WHITE);
    BlockDef& side(Tile tile, Color tint = WHITE); // north/south/east/west
    BlockDef& top(Tile tile, Color tint = WHITE);
    BlockDef& bottom(Tile tile, Color tint = WHITE);
    BlockDef& north(Tile tile, Color tint = WHITE);
    BlockDef& south(Tile tile, Color tint = WHITE); // a directional block's front
    BlockDef& east(Tile tile, Color tint = WHITE);
    BlockDef& west(Tile tile, Color tint = WHITE);
    BlockDef& cut(Tile tile);  // see BlockProperties::cut_texture_uv
    BlockDef& end(Tile tile);  // see BlockProperties::end_texture_uv

private:
    enum FacePriority : unsigned char { Unset, All, Side, Exact };
    void set_face(int face, Tile tile, Color tint, FacePriority priority);

    BlockType type_;
    FacePriority face_priority_[6] = {};
    bool hardness_set_ = false;
    bool tool_set_ = false;
    bool density_set_ = false;
    bool replaceable_set_ = false;
};

// Starts a block definition. `name` is its stable id: saves, translations
// ("block.<name>") and chat commands all refer to the block by it.
BlockDef block(BlockType type, const char* name);

// ----------------------------------------------------------------- Items --

// A tool material's shared stats - see ItemProperties.
struct ToolMaterial {
    int durability;
    float mining_speed;
    int tier; // ToolTier
};

void tool(ItemType type, const char* name, ToolKind kind, const ToolMaterial& material, Tile sprite);
void material(ItemType type, const char* name, Tile sprite);
// `heal_amount` is in half-hearts (PlayerHealth's own unit).
void food(ItemType type, const char* name, int heal_amount, Tile sprite);

// A block shown as a flat item sprite instead of a 3D cube, in the
// inventory and as a dropped item (torches, doors, bed, ...).
void block_item_sprite(BlockType type, Tile sprite);

// ----------------------------------------------------------------- Drops --

// Builder returned by content::when_broken().
class DropRule {
public:
    explicit DropRule(BlockType block) : block_(block) {}

    // Nothing drops unless broken with this tool kind of at least this
    // tier (ToolTier) - see can_harvest_block().
    DropRule& needs(ToolKind kind, int min_tier = ToolTier::None);
    DropRule& drop(ItemRef what, int count = 1);
    DropRule& drop(ItemRef what, int count_min, int count_max);
    // Probability (0..1) of the drop added just before this call.
    DropRule& chance(float probability);
    // For readability only - a rule with no drop() yields nothing.
    DropRule& drop_nothing() { return *this; }

private:
    BlockType block_;
};

// Starts the drop rule for `block`. A block without one drops itself,
// with no tool requirement.
DropRule when_broken(BlockType block);

// --------------------------------------------------------------- Recipes --

// One pattern symbol and what it stands for, e.g. {'P', BlockType::OakPlanks}.
struct Key {
    char symbol;
    ItemRef what;
};

// Shaped recipe, Minecraft-style: each string is one grid row, a space is
// an empty cell. Fully empty outer rows/columns are trimmed, so the
// pattern may sit anywhere in the grid; one wider or taller than 2 needs
// the 3x3 workbench grid.
void shaped(ItemRef output, int count, std::initializer_list<const char*> pattern, std::initializer_list<Key> keys);
void shaped(ItemRef output, std::initializer_list<const char*> pattern, std::initializer_list<Key> keys);

// Shapeless recipe: one entry per required unit, in any arrangement. More
// than 4 ingredients needs the 3x3 workbench grid.
void shapeless(ItemRef output, int count, std::initializer_list<ItemRef> ingredients);

// -------------------------------------------------------------- Smelting --

void smelt(ItemRef input, ItemRef output, int count = 1);
void fuel(ItemRef what, int burn_ticks); // 20 ticks per second

// ------------------------------------------------ Registration entry points
// Each defined in its own src/content/*.cpp and called by the matching
// Load_* function.
void register_blocks();
void register_items();
void register_drops();
void register_recipes();
void register_smelting();

} // namespace content
