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
    Rectangle atlas_source;        // pixel-space rect within items.png (not normalized - DrawTexturePro takes pixels)
};

// Uploads items.png and fills the ItemType -> ItemProperties table. Every
// value is hardcoded right in the .cpp rather than loaded from an
// items.json the way blocks.json drives Block.cpp - 25 fixed tools isn't
// worth a data file yet. Call once after the window exists (texture loads
// need a GL context), same as Load_block_definitions().
void Load_item_definitions();

const ItemProperties& get_item_properties(ItemType type);

// items.png - the texture every ItemProperties::atlas_source indexes into.
// Valid only after Load_item_definitions().
const Texture2D& get_item_atlas_texture();

// The snake_case name (e.g. "wooden_pickaxe") player.json persists a tool
// as - never the raw enum value, which isn't safe to persist across builds
// if ItemType's own order ever changes. Same pattern as get_block_name().
const std::string& get_item_name(ItemType type);

// Reverse of get_item_name() - std::nullopt if `name` doesn't match any
// known item.
std::optional<ItemType> item_type_from_name(const std::string& name);
