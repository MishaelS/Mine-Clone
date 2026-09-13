#pragma once

#include "core/Block.hpp"

#include <vector>

// Controls what a structure block is allowed to replace. Keeping this on
// each block makes a Structure useful for more than trees later: ruins can
// require air, ores can replace stone, and vegetation can share foliage.
enum class StructureReplaceRule {
    AirOnly,
    AirOrFoliage,
};

struct StructureBlock {
    int x;
    int y;
    int z;
    BlockType type;
    StructureReplaceRule replace_rule;
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

// Oak tree variant with a 4-6 block trunk and a Minecraft-style layered
// canopy. The origin is the first trunk block above the ground.
Structure make_oak_tree(int trunk_height);
