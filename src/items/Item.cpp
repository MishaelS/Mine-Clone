#include "items/Item.hpp"
#include "content/Content.hpp"
#include "core/TextureManager.hpp"

#include <array>
#include <stdexcept>

namespace {
    std::array<ItemProperties, static_cast<size_t>(ItemType::Count)> item_table;
    std::array<std::string, static_cast<size_t>(ItemType::Count)> item_names;
    std::array<std::optional<Rectangle>, MAX_BLOCK_TYPES> block_item_sprites;

    constexpr const char* ITEM_ATLAS_PATH = "sprites/items.png";
    constexpr int TILE_PIXELS = 16;

    const Texture2D* item_atlas_texture = nullptr;

    // A content::Tile -> the pixel-space rect DrawTexturePro expects,
    // range-checked against the atlas actually loaded (same strictness as
    // block faces' terrain coordinates).
    Rectangle sprite_rect(const std::string& owner, content::Tile tile) {
        if (tile.x < 0 || tile.y < 0 ||
            (tile.x + 1) * TILE_PIXELS > item_atlas_texture->width ||
            (tile.y + 1) * TILE_PIXELS > item_atlas_texture->height) {
            throw std::runtime_error("'" + owner + "': sprite tile coordinates outside the item atlas");
        }

        return {
            static_cast<float>(tile.x * TILE_PIXELS), static_cast<float>(tile.y * TILE_PIXELS),
            static_cast<float>(TILE_PIXELS), static_cast<float>(TILE_PIXELS),
        };
    }

    ItemProperties& define(ItemType type, const char* name, content::Tile sprite) {
        size_t index = static_cast<size_t>(type);
        if (type == ItemType::None || type == ItemType::Count) {
            throw std::runtime_error(std::string("item '") + name + "': not a definable ItemType");
        }

        if (!item_names[index].empty()) {
            throw std::runtime_error("ItemType defined twice: '" + item_names[index] + "' and '" + name + "'");
        }

        if (item_type_from_name(name)) {
            throw std::runtime_error(std::string("duplicate item name '") + name + "'");
        }

        item_names[index] = name;
        item_table[index] = ItemProperties{};
        item_table[index].atlas_source = sprite_rect(name, sprite);
        return item_table[index];
    }
}

namespace content {

void tool(ItemType type, const char* name, ToolKind kind, const ToolMaterial& material, Tile sprite)
{
    ItemProperties& properties = define(type, name, sprite);
    properties.category = ItemCategory::Tool;
    properties.tool_kind = kind;
    properties.max_durability = material.durability;
    properties.mining_speed_multiplier = material.mining_speed;
    properties.tier = material.tier;
}

// Plain crafting/drop material - no durability, no mining-speed bonus,
// stacks like a block instead of sitting alone in its own slot (see
// ItemCategory::Material).
void material(ItemType type, const char* name, Tile sprite)
{
    ItemProperties& properties = define(type, name, sprite);
    properties.category = ItemCategory::Material;
    properties.tool_kind = ToolKind::None;
    properties.max_durability = 0;
    properties.mining_speed_multiplier = 1.0f;
}

// Same as material() plus how many half-hearts eating it restores - see
// PlayerHealth::heal() and GameEngine's own right-click-to-eat handling.
void food(ItemType type, const char* name, int heal_amount, Tile sprite)
{
    material(type, name, sprite);
    item_table[static_cast<size_t>(type)].heal_amount = heal_amount;
}

void block_item_sprite(BlockType type, Tile sprite)
{
    block_item_sprites[static_cast<size_t>(type)] = sprite_rect(get_block_name(type), sprite);
}

} // namespace content

void Load_item_definitions()
{
    item_atlas_texture = &TextureManager::get(ITEM_ATLAS_PATH);

    content::register_items();

    for (size_t i = 1; i < item_names.size(); ++i) {
        if (item_names[i].empty()) {
            throw std::runtime_error("src/content/Items.cpp: definition missing for ItemType id " + std::to_string(i));
        }
    }
}

std::optional<Rectangle> get_block_item_sprite(BlockType type)
{
    return block_item_sprites[static_cast<size_t>(type)];
}

const ItemProperties& get_item_properties(ItemType type)
{
    return item_table[static_cast<size_t>(type)];
}

const Texture2D& get_item_atlas_texture()
{
    return *item_atlas_texture;
}

const std::string& get_item_name(ItemType type)
{
    return item_names[static_cast<size_t>(type)];
}

std::optional<ItemType> item_type_from_name(const std::string& name)
{
    // Small fixed set (a few dozen real items) - a linear scan over
    // item_names is simpler than keeping a second lazily-built map in sync
    // with it, and this only runs while loading content or a save file,
    // not per-frame.
    for (size_t i = 1; i < item_names.size(); ++i) {
        if (item_names[i] == name) return static_cast<ItemType>(i);
    }
    return std::nullopt;
}
