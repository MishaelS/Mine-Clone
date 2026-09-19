#include "player/Item.hpp"
#include "core/Json.hpp"
#include "core/TextureManager.hpp"

#include <array>
#include <stdexcept>

namespace {
    std::array<ItemProperties, static_cast<size_t>(ItemType::Count)> item_table;
    std::array<std::string, static_cast<size_t>(ItemType::Count)> item_names;
    std::array<std::optional<Rectangle>, static_cast<size_t>(BlockType::Count)> block_item_sprites;

    constexpr int TILE_PIXELS = 16;

    const Texture2D* item_atlas_texture = nullptr;

    // items.json's {"x", "y"} tile coordinates -> the pixel-space rect
    // DrawTexturePro expects, range-checked against the atlas actually
    // loaded (same strictness as blocks.json's own terrain coordinates).
    Rectangle load_sprite(const Json& sprite) {
        int col = static_cast<int>(sprite["x"].as_number(-1.0));
        int row = static_cast<int>(sprite["y"].as_number(-1.0));
        if (col < 0 || row < 0 ||
            (col + 1) * TILE_PIXELS > item_atlas_texture->width ||
            (row + 1) * TILE_PIXELS > item_atlas_texture->height) {
            throw std::runtime_error("items.json: sprite tile coordinates outside the item atlas");
        }
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

    // Sprite (atlas_source) is left empty here and filled in afterward from
    // items.json by Load_item_definitions() - stats live in code, art
    // coordinates live in data, same split blocks.json has for terrain.
    void define(ItemType type, const char* name, const char* display_name,
                ToolKind kind, MaterialStats stats)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Tool, kind, stats.durability, stats.speed, stats.tier, {}};
        item_names[index] = name;
    }

    // Plain crafting/drop material - no durability, no mining-speed bonus,
    // stacks like a block instead of sitting alone in its own slot (see
    // ItemCategory::Material).
    void define_material(ItemType type, const char* name, const char* display_name)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Material, ToolKind::None, 0, 1.0f, 0, {}};
        item_names[index] = name;
    }

    // Same as define_material() plus how many half-hearts eating it
    // restores (ItemProperties::heal_amount) - see PlayerHealth::heal()
    // and GameEngine's own right-click-to-eat handling.
    void define_food(ItemType type, const char* name, const char* display_name, int heal_amount)
    {
        size_t index = static_cast<size_t>(type);
        item_table[index] = {display_name, ItemCategory::Material, ToolKind::None, 0, 1.0f, 0, {}, heal_amount};
        item_names[index] = name;
    }

}

void Load_item_definitions()
{
    define(ItemType::WoodenSword , "wooden_sword" , "Деревянный меч", ToolKind::Sword, WOOD_STATS   );
    define(ItemType::StoneSword  , "stone_sword"  , "Каменный меч"  , ToolKind::Sword, STONE_STATS  );
    define(ItemType::IronSword   , "iron_sword"   , "Железный меч"  , ToolKind::Sword, IRON_STATS   );
    define(ItemType::DiamondSword, "diamond_sword", "Алмазный меч"  , ToolKind::Sword, DIAMOND_STATS);
    define(ItemType::GoldSword   , "gold_sword"   , "Золотой меч"   , ToolKind::Sword, GOLD_STATS   );

    define(ItemType::WoodenShovel , "wooden_shovel" , "Деревянная лопата", ToolKind::Shovel, WOOD_STATS   );
    define(ItemType::StoneShovel  , "stone_shovel"  , "Каменная лопата"  , ToolKind::Shovel, STONE_STATS  );
    define(ItemType::IronShovel   , "iron_shovel"   , "Железная лопата"  , ToolKind::Shovel, IRON_STATS   );
    define(ItemType::DiamondShovel, "diamond_shovel", "Алмазная лопата"  , ToolKind::Shovel, DIAMOND_STATS);
    define(ItemType::GoldShovel   , "gold_shovel"   , "Золотая лопата"   , ToolKind::Shovel, GOLD_STATS   );

    define(ItemType::WoodenPickaxe , "wooden_pickaxe" , "Деревянная кирка", ToolKind::Pickaxe, WOOD_STATS   );
    define(ItemType::StonePickaxe  , "stone_pickaxe"  , "Каменная кирка"  , ToolKind::Pickaxe, STONE_STATS  );
    define(ItemType::IronPickaxe   , "iron_pickaxe"   , "Железная кирка"  , ToolKind::Pickaxe, IRON_STATS   );
    define(ItemType::DiamondPickaxe, "diamond_pickaxe", "Алмазная кирка"  , ToolKind::Pickaxe, DIAMOND_STATS);
    define(ItemType::GoldPickaxe   , "gold_pickaxe"   , "Золотая кирка"   , ToolKind::Pickaxe, GOLD_STATS   );

    define(ItemType::WoodenAxe , "wooden_axe" , "Деревянный топор", ToolKind::Axe, WOOD_STATS   );
    define(ItemType::StoneAxe  , "stone_axe"  , "Каменный топор"  , ToolKind::Axe, STONE_STATS  );
    define(ItemType::IronAxe   , "iron_axe"   , "Железный топор"  , ToolKind::Axe, IRON_STATS   );
    define(ItemType::DiamondAxe, "diamond_axe", "Алмазный топор"  , ToolKind::Axe, DIAMOND_STATS);
    define(ItemType::GoldAxe   , "gold_axe"   , "Золотой топор"   , ToolKind::Axe, GOLD_STATS   );

    define(ItemType::WoodenHoe , "wooden_hoe" , "Деревянная мотыга", ToolKind::Hoe, WOOD_STATS   );
    define(ItemType::StoneHoe  , "stone_hoe"  , "Каменная мотыга"  , ToolKind::Hoe, STONE_STATS  );
    define(ItemType::IronHoe   , "iron_hoe"   , "Железная мотыга"  , ToolKind::Hoe, IRON_STATS   );
    define(ItemType::DiamondHoe, "diamond_hoe", "Алмазная мотыга"  , ToolKind::Hoe, DIAMOND_STATS);
    define(ItemType::GoldHoe   , "gold_hoe"   , "Золотая мотыга"   , ToolKind::Hoe, GOLD_STATS   );

    define_material(ItemType::Stick       , "stick"        , "Палка"          );
    define_material(ItemType::Coal        , "coal"         , "Уголь"          );
    define_material(ItemType::IronIngot   , "iron_ingot"   , "Железный слиток");
    define_material(ItemType::GoldIngot   , "gold_ingot"   , "Золотой слиток" );
    define_material(ItemType::Diamond     , "diamond"      , "Алмаз"          );
    define_material(ItemType::RedstoneDust, "redstone_dust", "Редстоун"       );
    define_material(ItemType::Sapling     , "sapling"      , "Саженец"        );
    define_material(ItemType::WheatSeeds  , "wheat_seeds"  , "Семена пшеницы" );

    // heal_amount is in half-hearts (PlayerHealth's own unit, 2 per
    // heart) - roughly Beta/modern Minecraft's own hunger-point values for
    // each food, just applied straight to health since there's no hunger
    // bar here for them to restore instead (see Item.hpp's own comment).
    define_food(ItemType::Apple,          "apple",           "Яблоко",           4);
    define_food(ItemType::GoldenApple,    "golden_apple",    "Золотое яблоко",  10);
    define_food(ItemType::Soup,           "soup",            "Суп",              6);
    define_food(ItemType::RawPorkchop,    "raw_porkchop",    "Сырая свинина",    3);
    define_food(ItemType::CookedPorkchop, "cooked_porkchop", "Жареная свинина",  8);
    define_food(ItemType::RawFish,        "raw_fish",        "Сырая рыба",       2);
    define_food(ItemType::CookedFish,     "cooked_fish",     "Жареная рыба",     5);
    define_food(ItemType::Bread,          "bread",           "Хлеб",             5);
    define_food(ItemType::Cookie,         "cookie",          "Печенье",          2);
    define_food(ItemType::Egg,            "egg",             "Яйцо",             2);
    define_food(ItemType::MilkBucket,     "milk_bucket",     "Ведро молока",     6);

    char* file_text = LoadFileText(ASSETS_PATH "items.json");
    if (file_text == nullptr) {
        throw std::runtime_error("Could not load " ASSETS_PATH "items.json");
    }
    Json root = Json::parse(file_text);
    UnloadFileText(file_text);

    std::string atlas_path = root["atlas"].as_string();
    if (atlas_path.empty()) throw std::runtime_error("items.json: missing \"atlas\" texture path");
    item_atlas_texture = &TextureManager::get(atlas_path);

    std::array<bool, static_cast<size_t>(ItemType::Count)> has_sprite{};
    for (const Json& entry : root["items"].as_array()) {
        std::string name = entry["name"].as_string();
        std::optional<ItemType> type = item_type_from_name(name);
        if (!type) throw std::runtime_error("items.json: unknown item name '" + name + "'");
        size_t index = static_cast<size_t>(*type);
        if (has_sprite[index]) throw std::runtime_error("items.json: duplicate item name '" + name + "'");
        item_table[index].atlas_source = load_sprite(entry["sprite"]);
        has_sprite[index] = true;
    }
    for (size_t i = 1; i < has_sprite.size(); ++i) {
        if (!has_sprite[i]) {
            throw std::runtime_error("items.json: sprite missing for item '" + item_names[i] + "'");
        }
    }

    for (const Json& entry : root["block_items"].as_array()) {
        std::string name = entry["block"].as_string();
        std::optional<BlockType> type = block_type_from_name(name);
        if (!type) throw std::runtime_error("items.json: unknown block name '" + name + "'");
        block_item_sprites[static_cast<size_t>(*type)] = load_sprite(entry["sprite"]);
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
    // Small fixed set (24 real items) - a linear scan over item_names is
    // simpler than keeping a second lazily-built map in sync with it, and
    // this only ever runs while loading a save file, not per-frame.
    for (size_t i = 1; i < item_names.size(); ++i) {
        if (item_names[i] == name) return static_cast<ItemType>(i);
    }
    return std::nullopt;
}
