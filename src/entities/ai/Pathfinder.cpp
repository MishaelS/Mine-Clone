#include "entities/ai/Pathfinder.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>

namespace ai {

    namespace {
        // Extra cost of a step, on top of the distance walked - Minecraft's
        // path-type "malus" values.
        constexpr float WATER_COST    = 8.0f;   // land animals swim only when there's no other way
        constexpr float DANGER_COST   = 8.0f;   // right next to a cactus
        constexpr float JUMP_COST     = 0.5f;   // stepping up a block
        constexpr float DROP_COST     = 0.25f;  // per block stepped down
        constexpr float DIAGONAL_COST = 1.41421356f;
        constexpr int MAX_WATER_RISE = 16;  // how far up a water column find its surface

        bool is_water(const World& world, int x, int y, int z) {
            return world.get_block(x, y, z) == BlockType::Water;
        }

        // Nothing in the cell to bump into - and nothing sticking up into it
        // from below, like a fence's 1.5-block-tall box.
        bool cell_free(const World& world, int x, int y, int z) {
            if (!world.is_column_loaded(x, z)) return false;
            if (world.get_block(x, y, z) == BlockType::Lava) return false;
            if (world.collision_boxes_at(x, y, z).count > 0) return false;
            const BlockShapeBoxes below = world.collision_boxes_at(x, y - 1, z);
            for (int i = 0; i < below.count; ++i) {
                if (below.boxes[i].max.y > static_cast<float>(y) + 0.01f) return false;
            }
            return true;
        }

        bool column_free(const World& world, int x, int y, int z, int height_cells) {
            for (int i = 0; i < height_cells; ++i) {
                if (!cell_free(world, x, y + i, z)) return false;
            }
            return true;
        }

        bool has_floor(const World& world, int x, int y, int z) {
            return world.collision_boxes_at(x, y - 1, z).count > 0;
        }

        float danger_cost(const World& world, int x, int y, int z) {
            static constexpr int SIDES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& side : SIDES) {
                if (world.get_block(x + side[0], y, z + side[1]) == BlockType::Cactus) return DANGER_COST;
            }
            return 0.0f;
        }

        // A swimming mob floats at the top of the water, so a path through water
        // runs along its surface.
        int water_surface(const World& world, int x, int y, int z, int height_cells) {
            for (int i = 0; i < MAX_WATER_RISE && is_water(world, x, y + 1, z) &&
                            column_free(world, x, y + 1, z, height_cells); ++i) {
                ++y;
            }
            return y;
        }

        uint64_t key_of(const PathNode& node) {
            return (static_cast<uint64_t>(node.x + (1 << 23)) & 0xFFFFFFu) << 36 |
                (static_cast<uint64_t>(node.z + (1 << 23)) & 0xFFFFFFu) << 12 |
                (static_cast<uint64_t>(node.y + 2048) & 0xFFFu);
        }

        struct Step {
            PathNode node;
            float cost;
        };

        // Where a mob standing in `from` can get to in one step.
        void neighbours(const World& world, const PathNode& from, const PathSettings& settings, std::vector<Step>& out) {
            out.clear();
            const int h = settings.height_cells;
            auto step_cost = [&](const PathNode& to, float base) {
                float cost = base + danger_cost(world, to.x, to.y, to.z);
                if (settings.avoid_water && is_water(world, to.x, to.y, to.z)) cost += WATER_COST;
                return cost;
            };

            static constexpr int STRAIGHT[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& dir : STRAIGHT) {
                const int nx = from.x + dir[0], nz = from.z + dir[1];
                if (column_free(world, nx, from.y, nz, h)) {
                    if (is_water(world, nx, from.y, nz)) {
                        const PathNode to{nx, water_surface(world, nx, from.y, nz, h), nz};
                        out.push_back({to, step_cost(to, 1.0f)});
                        continue;
                    }
                    if (has_floor(world, nx, from.y, nz)) {
                        const PathNode to{nx, from.y, nz};
                        out.push_back({to, step_cost(to, 1.0f)});
                        continue;
                    }
                    // Open air ahead: step down onto whatever is below, if it
                    // isn't too far a drop.
                    for (int drop = 1; drop <= settings.max_drop; ++drop) {
                        const int y = from.y - drop;
                        if (!cell_free(world, nx, y, nz)) break;
                        if (is_water(world, nx, y, nz) || has_floor(world, nx, y, nz)) {
                            const PathNode to{nx, y, nz};
                            out.push_back({to, step_cost(to, 1.0f + DROP_COST * static_cast<float>(drop))});
                            break;
                        }
                    }
                } else if (cell_free(world, from.x, from.y + h, from.z) && column_free(world, nx, from.y + 1, nz, h) &&
                        has_floor(world, nx, from.y + 1, nz)) {
                    // A block in the way with room above it (and above us to
                    // jump): hop up onto it.
                    const PathNode to{nx, from.y + 1, nz};
                    out.push_back({to, step_cost(to, 1.0f + JUMP_COST)});
                }
            }

            // Diagonals: only on the level, and only with both sides open - so
            // a path never clips a corner the mob's body would catch on.
            static constexpr int DIAGONAL[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            for (const auto& dir : DIAGONAL) {
                const int nx = from.x + dir[0], nz = from.z + dir[1];
                if (!column_free(world, nx, from.y, from.z, h) || !column_free(world, from.x, from.y, nz, h)) continue;
                if (!column_free(world, nx, from.y, nz, h)) continue;
                if (!has_floor(world, nx, from.y, nz) && !is_water(world, nx, from.y, nz)) continue;
                if (!has_floor(world, nx, from.y, from.z) && !is_water(world, nx, from.y, from.z)) continue;
                if (!has_floor(world, from.x, from.y, nz) && !is_water(world, from.x, from.y, nz)) continue;
                const PathNode to{nx, from.y, nz};
                out.push_back({to, step_cost(to, DIAGONAL_COST)});
            }
        }

        float distance(const PathNode& node, Vector3 target) {
            const float dx = node.x + 0.5f - target.x;
            const float dy = static_cast<float>(node.y) - target.y;
            const float dz = node.z + 0.5f - target.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    bool can_stand_at(const World& world, int x, int y, int z, int height_cells) {
        return column_free(world, x, y, z, height_cells) && (has_floor(world, x, y, z) || is_water(world, x, y, z));
    }

    std::optional<PathNode> standing_cell_near(const World& world, int x, int around_y, int z, int height_cells, int range) {
        for (int d = 0; d <= range; ++d) {
            if (can_stand_at(world, x, around_y - d, z, height_cells)) return PathNode{x, around_y - d, z};
            if (d > 0 && can_stand_at(world, x, around_y + d, z, height_cells)) return PathNode{x, around_y + d, z};
        }
        return std::nullopt;
    }

    std::optional<Path> find_path(const World& world, Vector3 from_feet, Vector3 target, const PathSettings& settings) {
        // Start in the cell the feet are in - or the one above, when they
        // stand on a slab or similar partial block inside their own cell.
        PathNode start{static_cast<int>(std::floor(from_feet.x)), static_cast<int>(std::floor(from_feet.y + 0.01f)),
                    static_cast<int>(std::floor(from_feet.z))};
        if (!column_free(world, start.x, start.y, start.z, settings.height_cells) &&
            column_free(world, start.x, start.y + 1, start.z, settings.height_cells)) {
            ++start.y;
        }

        const int target_x = static_cast<int>(std::floor(target.x));
        const int target_y = static_cast<int>(std::floor(target.y + 0.01f));
        const int target_z = static_cast<int>(std::floor(target.z));
        const Vector3 target_center = {target_x + 0.5f, static_cast<float>(target_y), target_z + 0.5f};
        auto reached = [&](const PathNode& node) {
            return node.x == target_x && node.z == target_z && std::abs(node.y - target_y) <= 1;
        };

        struct Record {
            PathNode node;
            float cost;      // from the start
            int parent;      // index into records, -1 for the start
            bool closed = false;
        };
        std::vector<Record> records;
        std::unordered_map<uint64_t, int> index_of;
        using Open = std::pair<float, int>; // estimated total cost, record index
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;

        records.push_back({start, 0.0f, -1});
        index_of[key_of(start)] = 0;
        open.push({distance(start, target_center), 0});

        int best = 0;
        float best_distance = distance(start, target_center);
        int found   = -1;
        int visited = 0;
        std::vector<Step> steps;
        while (!open.empty() && visited < settings.max_visited_nodes) {
            const int current = open.top().second;
            open.pop();
            if (records[static_cast<size_t>(current)].closed) continue;
            records[static_cast<size_t>(current)].closed = true;
            ++visited;

            const PathNode node = records[static_cast<size_t>(current)].node;
            const float to_target = distance(node, target_center);
            if (to_target < best_distance) {
                best_distance = to_target;
                best = current;
            }
            if (reached(node)) {
                found = current;
                break;
            }

            neighbours(world, node, settings, steps);
            const float cost_here = records[static_cast<size_t>(current)].cost;
            for (const Step& step : steps) {
                const float cost = cost_here + step.cost;
                const uint64_t key = key_of(step.node);
                auto existing = index_of.find(key);
                if (existing != index_of.end()) {
                    Record& record = records[static_cast<size_t>(existing->second)];
                    if (record.closed || cost >= record.cost) continue;
                    record.cost = cost;
                    record.parent = current;
                    open.push({cost + distance(step.node, target_center), existing->second});
                } else {
                    const int index = static_cast<int>(records.size());
                    records.push_back({step.node, cost, current});
                    index_of[key] = index;
                    open.push({cost + distance(step.node, target_center), index});
                }
            }
        }

        const int end = found >= 0 ? found : best;
        if (end == 0 && found < 0) return std::nullopt; // couldn't get any closer than where it stands

        Path path;
        path.reaches_target = found >= 0;
        for (int i = end; i > 0; i = records[static_cast<size_t>(i)].parent) {
            path.nodes.push_back(records[static_cast<size_t>(i)].node);
        }
        std::reverse(path.nodes.begin(), path.nodes.end());
        return path;
    }

} // namespace ai
