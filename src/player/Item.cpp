#include "player/Item.hpp"
#include "core/TextureManager.hpp"

#include <array>

namespace {
    std::array<ItemProperties, static_cast<size_t>(ItemType::Count)> item_table;
    std::array<std::string, static_cast<size_t>(ItemType::Count)> item_names;

    constexpr const char* ITEM_TEXTURE_PATH = "sprites/items.png";
    constexpr int TILE_PIXELS = 16;

    const Texture2D* item_atlas_texture = nullptr;

    Rectangle tile(int col, int row) {
        return {
            static_cast<float>(col * TILE_PIXELS), static_cast<float>(row * TILE_PIXELS),
            static_cast<float>(TILE_PIXELS), static_cast<float>(TILE_PIXELS),
        };
    }

    // Vanilla Beta's own tool stats: durability ("uses" before breaking)
    // and mining speed relative to a bare hand (1x). Gold is the fastest
    // tier but by far the most fragile - that tradeoff is the entire
    // reason it exists as its own material instead of just being worse
    // iron.
    struct MaterialStats { int durability; float speed; };
    constexpr MaterialStats WOOD_STATS    = {60,    2.0f};
    constexpr MaterialStats STONE_STATS   = {132,   4.0f};
    constexpr MaterialStats IRON_STATS    = {251,   6.0f};
    constexpr MaterialStats GOLD_STATS    = {33,   12.0f};
    constexpr MaterialStats DIAMOND_STATS = {1562,  8.0f};

    void define(ItemType type, const char* name, const char* display_name,
                ToolKind kind, MaterialStats stats, int col, int row)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, kind, stats.durability, stats.speed, tile(col, row)};
        item_names[index] = name;
    }

}

void Load_item_definitions()
{
    item_atlas_texture = &TextureManager::get(ITEM_TEXTURE_PATH);

    // items.png layout (see the Beta texture atlas reference): row 4
    // swords, row 5 shovels, row 6 pickaxes, row 7 axes, row 8 hoes;
    // columns in material order Wood(0) Stone(1) Iron(2) Diamond(3) Gold(4).
    define(ItemType::WoodenSword , "wooden_sword" , "Деревянный меч", ToolKind::Sword, WOOD_STATS   , 0, 4);
    define(ItemType::StoneSword  , "stone_sword"  , "Каменный меч"  , ToolKind::Sword, STONE_STATS  , 1, 4);
    define(ItemType::IronSword   , "iron_sword"   , "Железный меч"  , ToolKind::Sword, IRON_STATS   , 2, 4);
    define(ItemType::DiamondSword, "diamond_sword", "Алмазный меч"  , ToolKind::Sword, DIAMOND_STATS, 3, 4);
    define(ItemType::GoldSword   , "gold_sword"   , "Золотой меч"   , ToolKind::Sword, GOLD_STATS   , 4, 4);

    define(ItemType::WoodenShovel , "wooden_shovel" , "Деревянная лопата", ToolKind::Shovel, WOOD_STATS   , 0, 5);
    define(ItemType::StoneShovel  , "stone_shovel"  , "Каменная лопата"  , ToolKind::Shovel, STONE_STATS  , 1, 5);
    define(ItemType::IronShovel   , "iron_shovel"   , "Железная лопата"  , ToolKind::Shovel, IRON_STATS   , 2, 5);
    define(ItemType::DiamondShovel, "diamond_shovel", "Алмазная лопата"  , ToolKind::Shovel, DIAMOND_STATS, 3, 5);
    define(ItemType::GoldShovel   , "gold_shovel"   , "Золотая лопата"   , ToolKind::Shovel, GOLD_STATS   , 4, 5);

    define(ItemType::WoodenPickaxe , "wooden_pickaxe" , "Деревянная кирка", ToolKind::Pickaxe, WOOD_STATS   , 0, 6);
    define(ItemType::StonePickaxe  , "stone_pickaxe"  , "Каменная кирка"  , ToolKind::Pickaxe, STONE_STATS  , 1, 6);
    define(ItemType::IronPickaxe   , "iron_pickaxe"   , "Железная кирка"  , ToolKind::Pickaxe, IRON_STATS   , 2, 6);
    define(ItemType::DiamondPickaxe, "diamond_pickaxe", "Алмазная кирка"  , ToolKind::Pickaxe, DIAMOND_STATS, 3, 6);
    define(ItemType::GoldPickaxe   , "gold_pickaxe"   , "Золотая кирка"   , ToolKind::Pickaxe, GOLD_STATS   , 4, 6);

    define(ItemType::WoodenAxe , "wooden_axe" , "Деревянный топор", ToolKind::Axe, WOOD_STATS   , 0, 7);
    define(ItemType::StoneAxe  , "stone_axe"  , "Каменный топор"  , ToolKind::Axe, STONE_STATS  , 1, 7);
    define(ItemType::IronAxe   , "iron_axe"   , "Железный топор"  , ToolKind::Axe, IRON_STATS   , 2, 7);
    define(ItemType::DiamondAxe, "diamond_axe", "Алмазный топор"  , ToolKind::Axe, DIAMOND_STATS, 3, 7);
    define(ItemType::GoldAxe   , "gold_axe"   , "Золотой топор"   , ToolKind::Axe, GOLD_STATS   , 4, 7);

    define(ItemType::WoodenHoe , "wooden_hoe" , "Деревянная мотыга", ToolKind::Hoe, WOOD_STATS   , 0, 8);
    define(ItemType::StoneHoe  , "stone_hoe"  , "Каменная мотыга"  , ToolKind::Hoe, STONE_STATS  , 1, 8);
    define(ItemType::IronHoe   , "iron_hoe"   , "Железная мотыга"  , ToolKind::Hoe, IRON_STATS   , 2, 8);
    define(ItemType::DiamondHoe, "diamond_hoe", "Алмазная мотыга"  , ToolKind::Hoe, DIAMOND_STATS, 3, 8);
    define(ItemType::GoldHoe   , "gold_hoe"   , "Золотая мотыга"   , ToolKind::Hoe, GOLD_STATS   , 4, 8);
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
    // Small fixed set (24 real items) - a linear scan over item_names is
    // simpler than keeping a second lazily-built map in sync with it, and
    // this only ever runs while loading a save file, not per-frame.
    for (size_t i = 1; i < item_names.size(); ++i) {
        if (item_names[i] == name) return static_cast<ItemType>(i);
    }
    return std::nullopt;
}
