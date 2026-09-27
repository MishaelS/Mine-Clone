#include "content/Content.hpp"
#include "content/BlockFile.hpp"

#include <optional>
#include <stdexcept>
#include <string>

// Every block's rendering/physics properties. The plain full cubes (stone,
// planks, ores, wool, logs, leaves, glass...) each live in a file of their
// own, assets/blocks/<name>.json, made in the model editor's "Blocks" tab
// (see content/BlockFile.hpp and register_block_files() below). What's left
// here in code is what the files can't describe yet: fluids, plants, and
// the two-cell doors and beds. Everything else - full cubes and the
// single-cell shapes (slabs, stairs, trapdoors, cake, torches) - is a file.
// Face tiles are {column, row} in assets/sprites/terrain.png.

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

        // Partial-cube blocks whose geometry and collision come from
        // core/BlockShape (stairs, slabs, doors, beds, cake).
        BlockDef partial_block(B type, const char* name) {
            return block(type, name).non_solid().replaceable(false).transparent().cutout().shaped().custom_shape();
        }

        // Every assets/blocks/*.json block, registered under its own id.
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
                const BlockShapeKind kind = static_cast<BlockShapeKind>(file.shape);
                def.shape(kind);
                if (kind != BlockShapeKind::Cube) def.shaped();                                  // drawn from its boxes
                if (kind != BlockShapeKind::Cube && kind != BlockShapeKind::Torch) def.custom_shape(); // and collides with them
                if (kind == BlockShapeKind::Torch) def.attach_floor().attach_wall();
                if (file.has_cut) def.cut({file.cut_x, file.cut_y});
                if (file.item_sprite_x >= 0) def.item_sprite({file.item_sprite_x, file.item_sprite_y});
                if (file.side_inset != 0) def.side_inset(file.side_inset);
                for (int s = 0; s < MAX_BLOCK_STATES; ++s) def.state_model(s, block_file::to_state_model(file.states[static_cast<size_t>(s)]));
                // Its own model from parts: drawn from them (a cube too).
                if (!file.elements.empty() && block_file::elements_allowed(file.shape)) {
                    def.elements(block_file::to_elements(file.elements));
                    def.shaped();
                }
                auto tile = [&](int face) { return Tile{file.faces[face].tile_x, file.faces[face].tile_y}; };
                def.top(tile(0), file.faces[0].tint)
                    .bottom(tile(1), file.faces[1].tint)
                    .north(tile(2), file.faces[2].tint)
                    .south(tile(3), file.faces[3].tint)
                    .east(tile(4), file.faces[4].tint)
                    .west(tile(5), file.faces[5].tint);
            }
            // A slab's double block, once every block has a name to find.
            for (const block_file::BlockFile& file : block_file::load_all()) {
                if (file.double_block.empty()) continue;
                std::optional<BlockType> target = block_type_from_name(file.double_block);
                if (!target) throw std::runtime_error("block '" + file.name + "': no block named '" + file.double_block + "'");
                BlockDef(static_cast<BlockType>(file.id)).double_block(*target);
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

        // Partial blocks (core/BlockShape)
        partial_block(B::OakDoorLower   , "oak_door_lower"   ).shape(BlockShapeKind::Door).sound(S::Wood ).hardness(3.0f).all({1, 6});
        partial_block(B::OakDoorUpper   , "oak_door_upper"   ).shape(BlockShapeKind::Door).sound(S::Wood ).hardness(3.0f).all({1, 5});
        partial_block(B::IronDoorLower  , "iron_door_lower"  ).shape(BlockShapeKind::Door).sound(S::Metal).hardness(5.0f).tool(ToolKind::Pickaxe).all({2, 6});
        partial_block(B::IronDoorUpper  , "iron_door_upper"  ).shape(BlockShapeKind::Door).sound(S::Metal).hardness(5.0f).tool(ToolKind::Pickaxe).all({2, 5});
        partial_block(B::BedHead        , "bed_head"         ).shape(BlockShapeKind::Bed).sound(S::Wood ).hardness(0.2f).top({7, 8}).bottom({4, 0}).side({7, 9}).end({8, 9});
        partial_block(B::BedFoot        , "bed_foot"         ).shape(BlockShapeKind::Bed).sound(S::Wood ).hardness(0.2f).top({6, 8}).bottom({4, 0}).side({6, 9}).end({5, 9});

        // Everything else from assets/blocks/.
        register_block_files();
    }

} // namespace content
