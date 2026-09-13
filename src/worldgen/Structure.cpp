#include "worldgen/Structure.hpp"

#include <utility>

Structure::Structure(std::vector<StructureBlock> structure_blocks)
    : blocks(std::move(structure_blocks))
{
}

Structure make_oak_tree(int trunk_height)
{
    std::vector<StructureBlock> blocks;

    // Two broad lower layers, with their corners removed, followed by a
    // compact crown. Leaves are added first so the trunk can replace the
    // center of the canopy afterward.
    for (int y = trunk_height - 3; y <= trunk_height - 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                if ((x != -2 && x != 2) || (z != -2 && z != 2)) {
                    blocks.push_back({x, y, z, BlockType::Foliage, StructureReplaceRule::AirOnly});
                }
            }
        }
    }

    for (int y = trunk_height - 1; y <= trunk_height; ++y) {
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                if (y == trunk_height && x != 0 && z != 0) continue;
                blocks.push_back({x, y, z, BlockType::Foliage, StructureReplaceRule::AirOnly});
            }
        }
    }

    for (int y = 0; y < trunk_height; ++y) {
        blocks.push_back({0, y, 0, BlockType::OakLog, StructureReplaceRule::AirOrFoliage});
    }

    return Structure(std::move(blocks));
}
