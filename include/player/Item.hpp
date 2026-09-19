#pragma once

#include "core/Block.hpp" // ToolKind - see its own comment for why it lives there, not here
#include "raylib.h"

#include <cstdint>
#include <optional>
#include <string>

// Non-block inventory items - tools plus, now, plain crafting/drop
// materials (no armor, food, or dye system yet - those need their own
// prerequisite mechanics, most obviously a way to take damage/get hungry in
// the first place, that don't exist here yet). Unlike BlockType, an
// ItemType is never placed in the world - see ItemStack in Inventory.hpp,
// which holds either a BlockType or one of these, never both.
enum class ItemType : uint8_t {
    None, // "not a tool" - ItemStack's own sentinel, not a real item
    WoodenSword, WoodenPickaxe, WoodenShovel, WoodenAxe, WoodenHoe,
    StoneSword, StonePickaxe, StoneShovel, StoneAxe, StoneHoe,
    IronSword, IronPickaxe, IronShovel, IronAxe, IronHoe,
    GoldSword, GoldPickaxe, GoldShovel, GoldAxe, GoldHoe,
    DiamondSword, DiamondPickaxe, DiamondShovel, DiamondAxe, DiamondHoe,

    // Materials - the crafting/drop system's ingredients, distinct from
    // tools (see ItemCategory below): they stack up to MAX_ITEM_STACK
    // instead of always sitting alone, and never carry durability.
    Stick, Coal, IronIngot, GoldIngot, Diamond, RedstoneDust,
    Sapling, Apple, WheatSeeds,

    // Food - a Material like the ones above (stacks, no durability), plus
    // ItemProperties::heal_amount > 0 marks it edible - see PlayerHealth::
    // heal() and GameEngine's own right-click-to-eat handling. No hunger
    // system exists here, so eating restores health directly instead of a
    // separate hunger bar the way vanilla's foods do.
    GoldenApple, Soup, RawPorkchop, CookedPorkchop, RawFish, CookedFish,
    Bread, Cookie, Egg, MilkBucket,

    Count, // not a real item; sentinel for table sizing
};

// Tool: equips into the hotbar's single-slot durability system
// (ItemStack::durability, mining speed vs. a block's effective_tool).
// Material: a plain crafting ingredient/drop - stacks like a block does,
// never breaks. ItemStack::is_tool() checks this instead of just "holds an
// ItemType" so a stack of, say, Coal is never mistaken for a wielded tool.
enum class ItemCategory : uint8_t { Tool, Material };

struct ItemProperties {
    std::string display_name; // Legacy label; UI uses ui::item_display_name for the selected language.
    ItemCategory category = ItemCategory::Tool;
    ToolKind tool_kind;             // Material entries leave this ToolKind::None
    int max_durability;            // "uses" before the tool breaks - unused (0) for a Material
    float mining_speed_multiplier; // only applied when tool_kind matches the targeted block's own category
    // Mining *level* (1=Wood/Gold, 2=Stone, 3=Iron, 4=Diamond) - gates
    // BlockDropTable::resolve()'s min_tool_tier, distinct from raw speed:
    // Gold sits at tier 1 despite mining_speed_multiplier being the
    // highest, matching real Minecraft's own long-standing quirk (a Gold
    // pickaxe still can't mine Iron ore). 0 for a Material (unused).
    int tier = 0;
    Rectangle atlas_source;        // pixel-space rect within the item atlas (not normalized - DrawTexturePro takes pixels)
    int heal_amount = 0;           // half-hearts restored on eating (PlayerHealth's own unit) - 0 for anything not food
};

// Fills the ItemType -> ItemProperties table. Gameplay stats (durability,
// speed, tier, heal amount) are defined in the .cpp; the item atlas
// texture path and every sprite's tile coordinates come from
// assets/items.json, the same way blocks.json supplies terrain tile
// coordinates. Throws if items.json names an unknown item/block or leaves
// any ItemType without a sprite. Call once after the window exists
// (texture loads need a GL context), same as Load_block_definitions().
void Load_item_definitions();

const ItemProperties& get_item_properties(ItemType type);

// The item atlas texture (items.json's "atlas") every ItemProperties::
// atlas_source and get_block_item_sprite() rect indexes into. Valid only
// after Load_item_definitions().
const Texture2D& get_item_atlas_texture();

// A block that shows a flat item-atlas sprite instead of its terrain-based
// cube/cross render - both as an inventory icon and as a dropped item
// (torches, sapling, doors, bed - items.json's "block_items"). std::nullopt
// for every other block.
std::optional<Rectangle> get_block_item_sprite(BlockType type);

// The snake_case name (e.g. "wooden_pickaxe") player.json persists a tool
// as - never the raw enum value, which isn't safe to persist across builds
// if ItemType's own order ever changes. Same pattern as get_block_name().
const std::string& get_item_name(ItemType type);

// Reverse of get_item_name() - std::nullopt if `name` doesn't match any
// known item.
std::optional<ItemType> item_type_from_name(const std::string& name);
