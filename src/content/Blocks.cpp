#include "content/Content.hpp"
#include "content/BlockFile.hpp"
#include "core/BlockShape.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

// Every block's rendering/physics properties - each block lives in a file of
// its own, assets/blocks/<name>.json, made in the model editor's "Blocks"
// tab (see content/BlockFile.hpp): full cubes, the single-cell shapes
// (slabs, stairs, trapdoors, cake, torches), the two-cell doors and beds,
// plants and fluids. What a block *does* beyond that (water flowing, a
// sapling growing, a furnace smelting) stays in the game's code, keyed by
// its BlockType.

namespace content {

    namespace {
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
                // A shape made of boxes is drawn from them and collides with
                // them (a torch doesn't); a plant is two crossed planes; a
                // fluid is drawn and flows by the game's own fluid code.
                const bool boxes = kind != BlockShapeKind::Cube && kind != BlockShapeKind::Cross && kind != BlockShapeKind::Fluid;
                if (boxes) def.shaped();
                if (boxes && kind != BlockShapeKind::Torch) def.custom_shape();
                if (kind == BlockShapeKind::Cross) def.cross();
                if (kind == BlockShapeKind::Torch) def.attach_floor().attach_wall();
                if (file.has_cut) def.cut({file.cut_x, file.cut_y});
                if (file.has_end) def.end({file.end_x, file.end_y});
                if (file.joins) {
                    std::array<Tile, 4> joined;
                    for (size_t i = 0; i < joined.size(); ++i) joined[i] = {file.joined[i][0], file.joined[i][1]};
                    def.joins_sideways(joined);
                }
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
                for (int f = 0; f < 6; ++f) def.biome_tint(f, static_cast<BiomeTint>(file.faces[static_cast<size_t>(f)].biome));
                if (!file.particles.empty()) def.particles(block_file::to_emitters(file.particles));
            }
            // A slab's double block and a two-cell block's other half, once
            // every block has a name to find.
            for (const block_file::BlockFile& file : block_file::load_all()) {
                auto named = [&](const std::string& name) {
                    std::optional<BlockType> target = block_type_from_name(name);
                    if (!target) throw std::runtime_error("block '" + file.name + "': no block named '" + name + "'");
                    return *target;
                };
                BlockDef def(static_cast<BlockType>(file.id));
                if (!file.double_block.empty()) def.double_block(named(file.double_block));
                std::vector<BlockType> soil;
                for (const std::string& name : file.placed_on) soil.push_back(named(name));
                if (!soil.empty()) def.placed_on(std::move(soil));
                if (is_pair_kind(static_cast<BlockShapeKind>(file.shape)) && !file.partner.empty()) {
                    def.pair(file.half, named(file.partner), file.item);
                }
            }
        }
    }

    void register_blocks() {
        register_block_files();
    }

} // namespace content
