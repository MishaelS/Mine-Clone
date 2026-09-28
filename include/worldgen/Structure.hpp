#pragma once

#include "core/Biome.hpp"
#include "core/Block.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

// Controls what a structure block is allowed to replace. Keeping this on
// each block makes a Structure useful for more than trees: ruins can
// require air, ores can replace stone, and vegetation can share foliage.
// Same order as structure_file::REPLACE_IDS.
enum class StructureReplaceRule {
    AirOnly,
    AirOrFoliage,
    Any,
};

// Whether a block with `rule` may be placed over `existing`.
bool structure_can_replace(StructureReplaceRule rule, BlockType existing);

struct StructureBlock {
    int x;
    int y;
    int z;
    BlockType type;
    StructureReplaceRule replace_rule;
    // Where it can't be placed, the whole structure isn't (a tree's trunk).
    bool required = false;
};

// Immutable template expressed relative to an origin block. It contains no
// world/chunk logic; StructureGenerator owns placement policy and density.
class Structure {
public:
    explicit Structure(std::vector<StructureBlock> blocks);

    const std::vector<StructureBlock>& get_blocks() const { return blocks; }

private:
    std::vector<StructureBlock> blocks;
};

// One structure the game knows - loaded from assets/structures/<name>.json
// (content/StructureFile.hpp, made in the model editor's "Structures" tab).
// The origin is the cell right above the block it stands on.
struct StructureDefinition {
    std::string name;
    std::vector<Structure> variants;       // one picked per placement
    std::vector<BlockType> placed_on;      // world generation: stands on one of these, air above
    std::array<float, 6> chance{};         // per 4x4 candidate cell, by Biome
    bool tree_density = false;             // chance scaled by WorldTypeParams::tree_density
    std::optional<BlockType> grows_from;   // a sapling that grows into it
    // Cells every variant reaches, relative to the origin.
    int min_x = 0, max_x = 0, min_z = 0, max_z = 0;

    float chance_in(Biome biome) const { return chance[static_cast<size_t>(biome)]; }
};

// Loads every structure file. Needs the block names (Load_block_definitions())
// first; a structure naming a block that doesn't exist is skipped with a
// warning. Read-only afterwards - generation threads share it.
void Load_structures();
const std::vector<StructureDefinition>& get_structures();
// The structure `type` grows into (a sapling's tree), or nullptr.
const StructureDefinition* structure_growing_from(BlockType type);
