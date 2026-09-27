#include "content/Content.hpp"

// Every non-block item. One line per ItemType - see content/Content.hpp for
// how to add an item. Sprites are {column, row} in assets/sprites/items.png.

namespace content {

namespace {
    using I = ItemType;
    using B = BlockType;
    using T = ToolKind;

    // Vanilla Beta's own tool stats: durability ("uses" before breaking)
    // and mining speed relative to a bare hand (1x). Gold is the fastest
    // material but by far the most fragile - that tradeoff is the entire
    // reason it exists instead of just being worse iron - and ranks at the
    // bottom tier alongside Wood (a Gold pickaxe can't mine Iron ore).
    constexpr ToolMaterial WOOD_STATS    = {  60,  2.0f, ToolTier::Wood   };
    constexpr ToolMaterial STONE_STATS   = { 132,  4.0f, ToolTier::Stone  };
    constexpr ToolMaterial IRON_STATS    = { 251,  6.0f, ToolTier::Iron   };
    constexpr ToolMaterial GOLD_STATS    = {  33, 12.0f, ToolTier::Gold   };
    constexpr ToolMaterial DIAMOND_STATS = {1562,  8.0f, ToolTier::Diamond};
}

void register_items()
{
    // Tools: the atlas has one row per tool kind, one column per material.
    tool(I::WoodenSword    , "wooden_sword"    , T::Sword  , WOOD_STATS   , { 0,  4});
    tool(I::StoneSword     , "stone_sword"     , T::Sword  , STONE_STATS  , { 1,  4});
    tool(I::IronSword      , "iron_sword"      , T::Sword  , IRON_STATS   , { 2,  4});
    tool(I::DiamondSword   , "diamond_sword"   , T::Sword  , DIAMOND_STATS, { 3,  4});
    tool(I::GoldSword      , "gold_sword"      , T::Sword  , GOLD_STATS   , { 4,  4});

    tool(I::WoodenShovel   , "wooden_shovel"   , T::Shovel , WOOD_STATS   , { 0,  5});
    tool(I::StoneShovel    , "stone_shovel"    , T::Shovel , STONE_STATS  , { 1,  5});
    tool(I::IronShovel     , "iron_shovel"     , T::Shovel , IRON_STATS   , { 2,  5});
    tool(I::DiamondShovel  , "diamond_shovel"  , T::Shovel , DIAMOND_STATS, { 3,  5});
    tool(I::GoldShovel     , "gold_shovel"     , T::Shovel , GOLD_STATS   , { 4,  5});

    tool(I::WoodenPickaxe  , "wooden_pickaxe"  , T::Pickaxe, WOOD_STATS   , { 0,  6});
    tool(I::StonePickaxe   , "stone_pickaxe"   , T::Pickaxe, STONE_STATS  , { 1,  6});
    tool(I::IronPickaxe    , "iron_pickaxe"    , T::Pickaxe, IRON_STATS   , { 2,  6});
    tool(I::DiamondPickaxe , "diamond_pickaxe" , T::Pickaxe, DIAMOND_STATS, { 3,  6});
    tool(I::GoldPickaxe    , "gold_pickaxe"    , T::Pickaxe, GOLD_STATS   , { 4,  6});

    tool(I::WoodenAxe      , "wooden_axe"      , T::Axe    , WOOD_STATS   , { 0,  7});
    tool(I::StoneAxe       , "stone_axe"       , T::Axe    , STONE_STATS  , { 1,  7});
    tool(I::IronAxe        , "iron_axe"        , T::Axe    , IRON_STATS   , { 2,  7});
    tool(I::DiamondAxe     , "diamond_axe"     , T::Axe    , DIAMOND_STATS, { 3,  7});
    tool(I::GoldAxe        , "gold_axe"        , T::Axe    , GOLD_STATS   , { 4,  7});

    tool(I::WoodenHoe      , "wooden_hoe"      , T::Hoe    , WOOD_STATS   , { 0,  8});
    tool(I::StoneHoe       , "stone_hoe"       , T::Hoe    , STONE_STATS  , { 1,  8});
    tool(I::IronHoe        , "iron_hoe"        , T::Hoe    , IRON_STATS   , { 2,  8});
    tool(I::DiamondHoe     , "diamond_hoe"     , T::Hoe    , DIAMOND_STATS, { 3,  8});
    tool(I::GoldHoe        , "gold_hoe"        , T::Hoe    , GOLD_STATS   , { 4,  8});

    // Materials
    material(I::Stick       , "stick"        , { 5,  3});
    material(I::Coal        , "coal"         , { 7,  0});
    material(I::IronIngot   , "iron_ingot"   , { 7,  1});
    material(I::GoldIngot   , "gold_ingot"   , { 6,  1});
    material(I::Diamond     , "diamond"      , { 7,  3});
    material(I::RedstoneDust, "redstone_dust", { 8,  3});
    material(I::Sapling     , "sapling"      , {14,  2});
    material(I::WheatSeeds  , "wheat_seeds"  , { 9,  0});
    material(I::Wheat       , "wheat"        , { 9,  1});

    // Food - heal amount in half-hearts (2 per heart): roughly vanilla's
    // hunger-point values, applied straight to health since there's no
    // hunger bar here (see Item.hpp's own comment).
    food(I::Apple         , "apple"          ,  4, {10,  0});
    food(I::GoldenApple   , "golden_apple"   , 10, {11,  0});
    food(I::Soup          , "soup"           ,  6, { 8,  4});
    food(I::RawPorkchop   , "raw_porkchop"   ,  3, { 7,  5});
    food(I::CookedPorkchop, "cooked_porkchop",  8, { 8,  5});
    food(I::RawFish       , "raw_fish"       ,  2, { 9,  5});
    food(I::CookedFish    , "cooked_fish"    ,  5, {10,  5});
    food(I::Bread         , "bread"          ,  5, { 9,  2});
    food(I::Cookie        , "cookie"         ,  2, {12,  5});
    food(I::Egg           , "egg"            ,  2, {12,  0});
    food(I::MilkBucket    , "milk_bucket"    ,  6, {13,  4});

    // Blocks drawn as a flat item sprite instead of a 3D cube.
    block_item_sprite(B::Torch           , {13,  3});
    block_item_sprite(B::RedstoneTorch   , { 6,  7});
    block_item_sprite(B::LitRedstoneTorch, { 6,  6});
    block_item_sprite(B::OakSapling      , {14,  2});
    block_item_sprite(B::OakDoorLower    , {11,  2});
    block_item_sprite(B::IronDoorLower   , {12,  2});
    block_item_sprite(B::BedHead         , {13,  2});
    block_item_sprite(B::Cake            , {13,  1});
}

} // namespace content
