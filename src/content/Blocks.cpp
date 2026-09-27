#include "content/Content.hpp"
#include "content/BlockFile.hpp"

// Every block's rendering/physics properties. The plain full cubes (stone,
// planks, ores, wool, logs, leaves, glass...) each live in a file of their
// own, assets/blocks/<name>.json, made in the model editor's "Blocks" tab
// (see content/BlockFile.hpp and register_block_files() below). What's left
// here in code is everything that isn't just a textured cube: fluids,
// plants, torches, and the partial blocks whose shape comes from
// core/BlockShape. Face tiles are {column, row} in
// assets/sprites/terrain.png.

namespace content {

    namespace {
        using B = BlockType;
        using S = BlockSoundGroup;

        // Deliberately colorless art recolored in code - same idea as
        // Minecraft's biome-tinted grass overlay. Alpha = opacity for
        // translucent blocks.
        constexpr Color GRASS_TINT = {124, 189, 107, 255};
        constexpr Color WATER_TINT = {255, 255, 255, 200};

        // Presets for the common kinds of block - each still returns a BlockDef,
        // so anything else can be chained on after it.

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

        // Every assets/blocks/*.json full cube, registered under its own id.
        void register_block_files() {
            for (const block_file::BlockFile& file : block_file::load_all()) {
                BlockDef def = block(static_cast<BlockType>(file.id), file.name.c_str());
                def.sound(static_cast<BlockSoundGroup>(file.sound))
                    .hardness(file.hardness)
                    .tool(static_cast<ToolKind>(file.tool))
                    .density(file.density)
                    .luminance(file.luminance)
                    .replaceable(file.replaceable);
                if (!file.solid) def.non_solid();
                if (!file.selectable) def.not_selectable();
                if (file.transparent) def.transparent();
                if (file.translucent) def.translucent();
                if (file.cutout) def.cutout();
                if (file.keep_same_faces) def.keep_same_faces();
                if (file.damages_on_touch) def.damages_on_touch();
                if (file.directional) def.directional();
                if (file.side_inset != 0) def.side_inset(file.side_inset);
                auto tile = [&](int face) { return Tile{file.faces[face].tile_x, file.faces[face].tile_y}; };
                def.top(tile(0), file.faces[0].tint)
                    .bottom(tile(1), file.faces[1].tint)
                    .north(tile(2), file.faces[2].tint)
                    .south(tile(3), file.faces[3].tint)
                    .east(tile(4), file.faces[4].tint)
                    .west(tile(5), file.faces[5].tint);
            }
        }
    }

    void register_blocks() {
        // Fluids
        block(B::Water                  , "water"            ).sound(S::None).non_solid().not_selectable().transparent().translucent().all({13, 12}, WATER_TINT);
        block(B::Lava                   , "lava"             ).sound(S::None).non_solid().not_selectable().luminance(15).all({13, 14});

        // Plants
        plant(B::ShortGrass             , "short_grass"      ).hardness(0.1f).all({7, 2}, GRASS_TINT);
        plant(B::OakSapling             , "oak_sapling"      ).hardness(0.0f).all({15, 0});

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

        // Everything else - the full cubes - from assets/blocks/.
        register_block_files();
    }

} // namespace content
