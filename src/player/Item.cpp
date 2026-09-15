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
    // `tier` is mining *level*, not raw speed - real Minecraft's own
    // well-known quirk (see drops.json's own _notes) is that Gold ranks at
    // the bottom alongside Wood here despite being the fastest material:
    // a Gold pickaxe still can't mine Iron/Diamond/Redstone/Lapis ore.
    struct MaterialStats { int durability; float speed; int tier; };
    constexpr MaterialStats WOOD_STATS    = {60,    2.0f, 1};
    constexpr MaterialStats STONE_STATS   = {132,   4.0f, 2};
    constexpr MaterialStats IRON_STATS    = {251,   6.0f, 3};
    constexpr MaterialStats GOLD_STATS    = {33,   12.0f, 1};
    constexpr MaterialStats DIAMOND_STATS = {1562,  8.0f, 4};

    void define(ItemType type, const char* name, const char* display_name,
                ToolKind kind, MaterialStats stats, int col, int row)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Tool, kind, stats.durability, stats.speed, stats.tier, tile(col, row)};
        item_names[index] = name;
    }

    // Plain crafting/drop material - no durability, no mining-speed bonus,
    // stacks like a block instead of sitting alone in its own slot (see
    // ItemCategory::Material).
    void define_material(ItemType type, const char* name, const char* display_name, int col, int row)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Material, ToolKind::None, 0, 1.0f, 0, tile(col, row)};
        item_names[index] = name;
    }

    // Same as define_material() plus how many half-hearts eating it
    // restores (ItemProperties::heal_amount) - see PlayerHealth::heal()
    // and GameEngine's own right-click-to-eat handling.
    void define_food(ItemType type, const char* name, const char* display_name, int heal_amount, int col, int row)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Material, ToolKind::None, 0, 1.0f, 0, tile(col, row), heal_amount};
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

    // Materials - tile coordinates read directly off this project's own
    // items.png (verified by inspecting the atlas image, not guessed):
    // row 0 col 7 coal, row 1 cols 6/7 gold/iron ingot, row 3 col 5 stick,
    // row 3 col 7 diamond, row 3 col 8 redstone dust.
    define_material(ItemType::Stick      , "stick"        , "Палка"          ,  5,  3);
    define_material(ItemType::Coal       , "coal"         , "Уголь"          ,  7,  0);
    define_material(ItemType::IronIngot  , "iron_ingot"   , "Железный слиток",  7,  1);
    define_material(ItemType::GoldIngot  , "gold_ingot"   , "Золотой слиток" ,  6,  1);
    define_material(ItemType::Diamond    , "diamond"      , "Алмаз"          ,  7,  3);
    define_material(ItemType::RedstoneDust,"redstone_dust", "Редстоун"       ,  8,  3);
    define_material(ItemType::Sapling    , "sapling"      , "Саженец"        , 14,  2);
    define_material(ItemType::WheatSeeds , "wheat_seeds"  , "Семена пшеницы" ,  9,  0);

    // Food - tile coordinates read directly off this project's own
    // items.png (verified by inspecting the atlas image, not guessed).
    // heal_amount is in half-hearts (PlayerHealth's own unit, 2 per
    // heart) - roughly Beta/modern Minecraft's own hunger-point values for
    // each food, just applied straight to health since there's no hunger
    // bar here for them to restore instead (see Item.hpp's own comment).
    define_food(ItemType::Apple,          "apple",           "Яблоко",           4, 10, 0);
    define_food(ItemType::GoldenApple,    "golden_apple",    "Золотое яблоко",  10, 11, 0);
    define_food(ItemType::Soup,           "soup",            "Суп",              6,  8, 4);
    define_food(ItemType::RawPorkchop,    "raw_porkchop",    "Сырая свинина",    3,  7, 5);
    define_food(ItemType::CookedPorkchop, "cooked_porkchop", "Жареная свинина",  8,  8, 5);
    define_food(ItemType::RawFish,        "raw_fish",        "Сырая рыба",       2,  9, 5);
    define_food(ItemType::CookedFish,     "cooked_fish",     "Жареная рыба",     5, 10, 5);
    define_food(ItemType::Bread,          "bread",           "Хлеб",             5,  9, 2);
    define_food(ItemType::Cookie,         "cookie",          "Печенье",          2, 12, 5);
    define_food(ItemType::Egg,            "egg",             "Яйцо",             2, 12, 0);
    define_food(ItemType::MilkBucket,     "milk_bucket",     "Ведро молока",     6, 13, 4);
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
