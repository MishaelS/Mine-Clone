#include "content/Content.hpp"

// Every block's rendering/physics properties. One line per BlockType - see
// content/Content.hpp for the full BlockDef API and how to add a block.
// Face tiles are {column, row} in assets/sprites/terrain.png.

namespace content {

    namespace {
        using B = BlockType;
        using S = BlockSoundGroup;

        // Deliberately colorless art recolored in code - same idea as
        // Minecraft's biome-tinted grass/leaves overlay. Alpha = opacity for
        // translucent blocks.
        constexpr Color GRASS_TINT         = {124, 189, 107, 255};
        constexpr Color OAK_LEAVES_TINT    = { 82, 180,  82, 255};
        constexpr Color SPRUCE_LEAVES_TINT = { 97, 153,  97, 255};
        constexpr Color BIRCH_LEAVES_TINT  = {128, 167,  85, 255};
        constexpr Color WATER_TINT         = {255, 255, 255, 200};
        constexpr Color ICE_TINT           = {255, 255, 255, 210};

        // Presets for the common kinds of block - each still returns a BlockDef,
        // so anything else can be chained on after it.

        // Leaves: see-through, and keep the faces between two leaf blocks so a
        // thick crown darkens toward its middle.
        BlockDef leaves(B type, const char* name, Tile tile, Color tint) {
            return block(type, name).sound(S::Foliage).transparent().cutout().keep_same_faces().all(tile, tint);
        }

        BlockDef wool(B type, const char* name, Tile tile) {
            return block(type, name).sound(S::Cloth).all(tile);
        }

        // Grass tufts, saplings, flowers: two crossed quads, no collision,
        // overwritten by placing a block into their cell.
        BlockDef plant(B type, const char* name) {
            return block(type, name).sound(S::Grass).non_solid().transparent().cutout().cross().density(0.1f);
        }

        // Torch-style light sources, mounted on a floor or wall.
        BlockDef torch(B type, const char* name) {
            return block(type, name).sound(S::Wood).non_solid().replaceable(false).transparent().cutout().shaped()
                .attach_floor().attach_wall().hardness(0.0f).density(0.1f);
        }

        // Partial-cube blocks whose geometry and collision come from
        // core/BlockShape (stairs, slabs, doors, beds, cake).
        BlockDef partial_block(B type, const char* name) {
            return block(type, name).non_solid().replaceable(false).transparent().cutout().shaped().custom_shape();
        }
    }

    void register_blocks() {
        // Terrain
        block(B::Grass                  , "grass"            ).sound(S::Grass).top({0, 0}, GRASS_TINT).bottom({2, 0}).side({3, 0});
        block(B::SnowyGrass             , "snowy_grass"      ).sound(S::Snow).top({2, 4}).bottom({2, 0}).side({4, 4});
        block(B::Dirt                   , "dirt"             ).sound(S::Dirt).hardness(0.5f).all({2, 0});
        block(B::Sand                   , "sand"             ).sound(S::Sand).hardness(0.5f).all({2, 1});
        block(B::Gravel                 , "gravel"           ).sound(S::Gravel).hardness(0.5f).all({3, 1});
        block(B::Clay                   , "clay"             ).sound(S::Gravel).all({8, 4});
        block(B::Stone                  , "stone"            ).hardness(1.5f).all({1, 0});
        block(B::Cobblestone            , "cobblestone"      ).hardness(1.5f).all({0, 1});
        block(B::MossyCobblestone       , "mossy_cobblestone").all({4, 2});
        block(B::Sandstone              , "sandstone"        ).top({0, 11}).bottom({0, 13}).side({0, 12});
        block(B::Bedrock                , "bedrock"          ).all({1, 1});
        block(B::Obsidian               , "obsidian"         ).hardness(50.0f).all({5, 2});
        block(B::SnowBlock              , "snow_block"       ).sound(S::Snow).all({2, 4});
        block(B::Ice                    , "ice"              ).sound(S::Glass).transparent().translucent().all({3, 4}, ICE_TINT);
        block(B::Netherrack             , "netherrack"       ).all({7, 6});
        block(B::SoulSand               , "soul_sand"        ).sound(S::Sand).all({8, 6});
        block(B::Glowstone              , "glowstone"        ).sound(S::Glass).luminance(15).all({9, 6});

        // Fluids
        block(B::Water                  , "water"            ).sound(S::None).non_solid().not_selectable().transparent().translucent().all({13, 12}, WATER_TINT);
        block(B::Lava                   , "lava"             ).sound(S::None).non_solid().not_selectable().luminance(15).all({13, 14});

        // Ores and mineral blocks
        block(B::CoalOre                , "coal_ore"         ).hardness(3.0f) .all({ 2,  2});
        block(B::IronOre                , "iron_ore"         ).hardness(3.0f) .all({ 1,  2});
        block(B::GoldOre                , "gold_ore"         ).hardness(3.0f) .all({ 0,  2});
        block(B::DiamondOre             , "diamond_ore"      ).hardness(3.0f) .all({ 2,  3});
        block(B::RedstoneOre            , "redstone_ore"     ).hardness(3.0f) .all({ 3,  3});
        block(B::LapisOre               , "lapis_ore"        ).hardness(3.0f) .all({ 0, 10});
        block(B::IronBlock              , "iron_block"       ).sound(S::Metal).all({ 6,  1});
        block(B::GoldBlock              , "gold_block"       ).sound(S::Metal).all({ 7,  1});
        block(B::DiamondBlock           , "diamond_block"    ).sound(S::Metal).all({ 8,  1});
        block(B::LapisBlock             , "lapis_block"      ).sound(S::Metal).all({ 0,  9});

        // Trees and plants
        block(B::OakLog                 , "oak_log"          ).sound(S::Wood).hardness(2.0f).top({5, 1}).bottom({5, 1}).side({4, 1});
        block(B::SpruceLog              , "spruce_log"       ).sound(S::Wood).hardness(2.0f).top({5, 1}).bottom({5, 1}).side({4, 7});
        block(B::BirchLog               , "birch_log"        ).sound(S::Wood).hardness(2.0f).top({5, 1}).bottom({5, 1}).side({5, 7});
        leaves(B::Foliage               , "foliage"          , {4, 3}, OAK_LEAVES_TINT);
        leaves(B::SpruceFoliage         , "spruce_foliage"   , {4, 8}, SPRUCE_LEAVES_TINT);
        leaves(B::BirchFoliage          , "birch_foliage"    , {4, 3}, BIRCH_LEAVES_TINT);
        plant(B::ShortGrass             , "short_grass"      ).hardness(0.1f).all({7, 2}, GRASS_TINT);
        plant(B::OakSapling             , "oak_sapling"      ).hardness(0.0f).all({15, 0});
        block(B::Cactus                 , "cactus"           ).sound(S::Cloth).transparent().cutout().damages_on_touch().side_inset(1).top({5, 4}).bottom({7, 4}).side({6, 4});
        block(B::Pumpkin                , "pumpkin"          ).sound(S::Wood).top({6, 6}).bottom({6, 6}).side({6, 7}).south({7, 7});
        block(B::JackOLantern           , "jack_o_lantern"   ).sound(S::Wood).luminance(15).top({6, 6}).bottom({6, 6}).side({6, 7}).south({8, 7});

        // Building blocks
        block(B::OakPlanks              , "oak_planks"       ).sound(S::Wood).hardness(2.0f).all({4, 0});
        block(B::Glass                  , "glass"            ).sound(S::Glass).transparent().all({1, 3});
        block(B::Bricks                 , "bricks"           ).all({7, 0});
        block(B::DoubleStoneSlab        , "double_stone_slab").top({6, 0}).bottom({6, 0}).side({5, 0});
        block(B::Bookshelf              , "bookshelf"        ).sound(S::Wood).top({4, 0}).bottom({4, 0}).side({3, 2});
        block(B::Sponge                 , "sponge"           ).sound(S::Grass).all({0, 3});
        block(B::Tnt                    , "tnt"              ).sound(S::Grass).top({9, 0}).bottom({10, 0}).side({8, 0});
        block(B::MobSpawner             , "mob_spawner"      ).sound(S::Metal).transparent().cutout().all({1, 4});

        // Wool
        wool(B::WhiteWool               , "white_wool"       , {0,  4});
        wool(B::BlackWool               , "black_wool"       , {1,  7});
        wool(B::GrayWool                , "gray_wool"        , {2,  7});
        wool(B::RedWool                 , "red_wool"         , {1,  8});
        wool(B::PinkWool                , "pink_wool"        , {2,  8});
        wool(B::GreenWool               , "green_wool"       , {1,  9});
        wool(B::LimeWool                , "lime_wool"        , {2,  9});
        wool(B::BrownWool               , "brown_wool"       , {1, 10});
        wool(B::YellowWool              , "yellow_wool"      , {2, 10});
        wool(B::BlueWool                , "blue_wool"        , {1, 11});
        wool(B::LightBlueWool           , "light_blue_wool"  , {2, 11});
        wool(B::PurpleWool              , "purple_wool"      , {1, 12});
        wool(B::MagentaWool             , "magenta_wool"     , {2, 12});
        wool(B::CyanWool                , "cyan_wool"        , {1, 13});
        wool(B::OrangeWool              , "orange_wool"      , {2, 13});
        wool(B::LightGrayWool           , "light_gray_wool"  , {1, 14});

        // Directional machines and containers - .south() is the front face
        // (see block_is_directional()).
        block(B::Workbench              , "workbench"        ).sound(S::Wood).top({11, 2}).bottom({4, 0}).side({12, 3}).north({11, 3}).south({11, 3});
        block(B::Chest                  , "chest"            ).sound(S::Wood).top({9, 1}).bottom({9, 1}).side({10, 1}).south({11, 1});
        block(B::Furnace                , "furnace"          ).top({14, 3}).bottom({14, 3}).side({13, 2}).south({12, 2});
        block(B::LitFurnace             , "lit_furnace"      ).luminance(13).top({14, 3}).bottom({14, 3}).side({13, 2}).south({13, 3});
        block(B::Dispenser              , "dispenser"        ).top({14, 3}).bottom({14, 3}).side({13, 2}).south({14, 2});
        block(B::NoteBlock              , "note_block"       ).sound(S::Wood).all({10, 4});
        block(B::Jukebox                , "jukebox"          ).sound(S::Wood).top({11, 4}).bottom({10, 4}).side({10, 4});
        block(B::Piston                 , "piston"           ).sound(S::Wood).top({11, 6}).bottom({13, 6}).side({12, 6}).south({11, 6});
        block(B::StickyPiston           , "sticky_piston"    ).sound(S::Wood).top({10, 6}).bottom({13, 6}).side({12, 6}).south({10, 6});

        // Light sources
        torch(B::Torch                  , "torch"             ).luminance(14).all({0, 5});
        torch(B::RedstoneTorch          , "redstone_torch"    ).all({3, 7});
        torch(B::LitRedstoneTorch       , "lit_redstone_torch").luminance(7).all({3, 6});

        // Partial blocks (core/BlockShape)
        partial_block(B::OakStairs      , "oak_stairs"       ).sound(S::Wood ).hardness(2.0f).all({4, 0});
        partial_block(B::OakSlab        , "oak_slab"         ).sound(S::Wood ).hardness(2.0f).all({4, 0});
        partial_block(B::OakTrapdoor    , "oak_trapdoor"     ).sound(S::Wood ).hardness(3.0f).all({4, 5});
        partial_block(B::OakDoorLower   , "oak_door_lower"   ).sound(S::Wood ).hardness(3.0f).all({1, 6});
        partial_block(B::OakDoorUpper   , "oak_door_upper"   ).sound(S::Wood ).hardness(3.0f).all({1, 5});
        partial_block(B::IronDoorLower  , "iron_door_lower"  ).sound(S::Metal).hardness(5.0f).tool(ToolKind::Pickaxe).all({2, 6});
        partial_block(B::IronDoorUpper  , "iron_door_upper"  ).sound(S::Metal).hardness(5.0f).tool(ToolKind::Pickaxe).all({2, 5});
        partial_block(B::BedHead        , "bed_head"         ).sound(S::Wood ).hardness(0.2f).top({7, 8}).bottom({4, 0}).side({7, 9}).end({8, 9});
        partial_block(B::BedFoot        , "bed_foot"         ).sound(S::Wood ).hardness(0.2f).top({6, 8}).bottom({4, 0}).side({6, 9}).end({5, 9});
        partial_block(B::Cake           , "cake"             ).sound(S::Cloth).hardness(0.5f).top({9, 7}).bottom({12, 7}).side({10, 7}).cut({11, 7});
    }

} // namespace content
