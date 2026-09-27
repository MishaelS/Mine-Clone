#pragma once

#include "raylib.h"

#include <optional>
#include <vector>

class World;

namespace ai {

    // A block cell a mob's feet stand in along a path.
    struct PathNode {
        int x = 0, y = 0, z = 0;

        Vector3 center() const { return {x + 0.5f, static_cast<float>(y), z + 0.5f}; }
        bool operator==(const PathNode& other) const { return x == other.x && y == other.y && z == other.z; }
    };

    struct Path {
        std::vector<PathNode> nodes; // from the first step after the start to the end
        bool reaches_target = false; // false: ends as close as it could get
    };

    // Who the path is for: how many cells tall it is and what it avoids -
    // Minecraft's WalkNodeEvaluator settings.
    struct PathSettings {
        int height_cells = 2;
        int max_drop = 3;           // deepest step down it takes, blocks
        bool avoid_water = true;    // water costs WATER_COST extra instead of plain walking
        int max_visited_nodes = 400;
    };

    // A* over block cells: walking on anything solid, stepping up one block,
    // down at most max_drop, floating across water; diagonals only where both
    // sides are open, so it doesn't cut corners. If `target` itself can't be
    // reached, the path ends at the closest cell it found (Minecraft's partial
    // paths) - nullopt only when there's no way to move at all.
    std::optional<Path> find_path(const World& world, Vector3 from_feet, Vector3 target, const PathSettings& settings);

    // Whether a mob `height_cells` tall can stand with its feet in (x, y, z):
    // room for it, and solid ground (or water) under it.
    bool can_stand_at(const World& world, int x, int y, int z, int height_cells);

    // The cell nearest (x, around_y, z) vertically that a mob can stand in,
    // looking `range` blocks up and down.
    std::optional<PathNode> standing_cell_near(const World& world, int x, int around_y, int z, int height_cells, int range);

} // namespace ai
