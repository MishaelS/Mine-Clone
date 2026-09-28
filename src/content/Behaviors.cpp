#include "content/Content.hpp"
#include "content/BlockFile.hpp"
#include "scripting/LuaScripting.hpp"
#include "world/BlockBehavior.hpp"
#include "worldgen/Structure.hpp"

#include <memory>

// What blocks do beyond their properties, written against the engine's
// block API (world/BlockBehavior.hpp) - the same calls Lua scripts and the
// model editor's behavior graphs make.

namespace content {

    namespace {
        // Grass spreading, Minecraft's rules: a grass block needs this much
        // light above it to spread, tries this many cells per random tick,
        // within 1 block sideways and 3 down .. 1 up of itself.
        constexpr int GRASS_SPREAD_MIN_LIGHT = 9;
        constexpr int GRASS_SPREAD_ATTEMPTS = 4;

        // 1-in-7 chance a sapling turns into a tree the random tick that
        // actually lands on it - real Minecraft's own sapling growth odds.
        constexpr float GROWTH_CHANCE = 1.0f / 7.0f;

        // Nothing on top of `pos` that keeps grass from living there: no
        // water/lava, no full opaque block, no slab/stairs - plants,
        // torches, glass and leaves are fine.
        bool grass_can_live(BlockApi& api, BlockPos pos)
        {
            const BlockType above = api.get_block(pos.above());
            if (above == BlockType::Water || above == BlockType::Lava) return false;
            const BlockProperties& properties = get_block_properties(above);
            if (properties.has_custom_shape) return false; // slab, stairs, bed... - covers the top
            return !properties.solid || properties.transparent;
        }

        // Grass under something that covers it dies back to dirt; uncovered,
        // well-lit grass spreads onto uncovered dirt next to it - Minecraft's
        // own grass random tick.
        class GrassSpreads : public BlockBehavior {
        public:
            void on_random_tick(BlockApi& api, BlockPos pos) override
            {
                if (!grass_can_live(api, pos)) {
                    api.set_block(pos, BlockType::Dirt);
                    return;
                }
                if (api.get_sky_light(pos.above()) < GRASS_SPREAD_MIN_LIGHT &&
                    api.get_block_light(pos.above()) < GRASS_SPREAD_MIN_LIGHT) {
                    return;
                }
                for (int i = 0; i < GRASS_SPREAD_ATTEMPTS; ++i) {
                    const BlockPos target = pos.offset(static_cast<int>(api.random() * 3.0f) - 1, static_cast<int>(api.random() * 5.0f) - 3,
                                                       static_cast<int>(api.random() * 3.0f) - 1);
                    if (api.get_block(target) != BlockType::Dirt || !grass_can_live(api, target)) continue;
                    api.set_block(target, BlockType::Grass);
                }
            }
        };

        // A sapling (any block a structure file says it "grows_from")
        // turning into its structure on a random tick. Blocked - something
        // built over the trunk since it was planted - it just waits for
        // another lucky tick, as a vanilla sapling does.
        class GrowsIntoStructure : public BlockBehavior {
        public:
            explicit GrowsIntoStructure(const StructureDefinition& structure) : structure(structure) {}

            void on_random_tick(BlockApi& api, BlockPos pos) override
            {
                if (api.random() >= GROWTH_CHANCE) return;
                api.place_structure(pos, structure);
            }

        private:
            const StructureDefinition& structure;
        };
    }

    void register_behaviors() {
        // The old scripts' behaviors go before the Lua they live in is restarted.
        block_behaviors::clear();
        scripting::start();

        block_behaviors::add(BlockType::Grass, std::make_shared<GrassSpreads>());
        for (const StructureDefinition& structure : get_structures()) {
            if (structure.grows_from) block_behaviors::add(*structure.grows_from, std::make_shared<GrowsIntoStructure>(structure));
        }
        // Each block file's "script" - read afresh, so /reload picks up a
        // block given a script (or another one) since the game started.
        for (const block_file::BlockFile& file : block_file::load_all()) {
            if (file.script.empty()) continue;
            const std::optional<BlockType> type = block_type_from_name(file.name);
            if (!type) continue; // a block file added after the game started - needs a restart
            if (std::shared_ptr<BlockBehavior> behavior = scripting::load_block_script(file.script)) {
                block_behaviors::add(*type, std::move(behavior));
            }
        }
    }

} // namespace content
