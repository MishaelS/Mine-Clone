#pragma once

#include "core/Block.hpp" // ToolKind - see its own comment for why it lives there, not here
#include "raylib.h"

#include <cstdint>
#include <optional>
#include <string>

// Non-block inventory items - tools today, nothing else yet (no armor,
// food, or dye system - those need their own prerequisite mechanics, most
// obviously a way to take damage/get hungry in the first place, that don't
// exist here yet). Unlike BlockType, an ItemType is never placed in the
// world - see ItemStack in Inventory.hpp, which holds either a BlockType or
// one of these, never both.
enum class ItemType : uint8_t {
    None, // "not a tool" - ItemStack's own sentinel, not a real item
    WoodenSword, WoodenPickaxe, WoodenShovel, WoodenAxe, WoodenHoe,
    StoneSword, StonePickaxe, StoneShovel, StoneAxe, StoneHoe,
    IronSword, IronPickaxe, IronShovel, IronAxe, IronHoe,
    GoldSword, GoldPickaxe, GoldShovel, GoldAxe, GoldHoe,
    DiamondSword, DiamondPickaxe, DiamondShovel, DiamondAxe, DiamondHoe,
    Count, // not a real item; sentinel for table sizing
};

struct ItemProperties {
    std::string display_name; // Russian, matching get_block_name()'s own language
    ToolKind tool_kind;
    int max_durability;            // "uses" before the tool breaks
    float mining_speed_multiplier; // only applied when tool_kind matches the targeted block's own category
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
