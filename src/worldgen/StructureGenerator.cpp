#include "worldgen/StructureGenerator.hpp"

#include "world/Chunk.hpp"
#include "core/Biome.hpp"
#include "core/TerrainNoise.hpp"
#include "worldgen/Structure.hpp"

#include <algorithm>
#include <cstdint>

namespace {
    constexpr int CANDIDATE_CELL_SIZE = 4;
    constexpr int TREE_RADIUS = 2;
    constexpr int MIN_TREE_HEIGHT = 4;
    constexpr int TREE_HEIGHT_VARIANTS = 3;

    // One candidate per 4x4 cell, then a biome-specific chance. This gives
    // roughly 1-2 trees/chunk in Plains, 10-11 in Forest, and 3-4 in Hills.
    float tree_chance(Biome biome)
    {
        switch (biome) {
            case Biome::Forest: return 0.68f;
            case Biome::Hills:  return 0.23f;
            case Biome::Plains: return 0.09f;
            case Biome::Desert:
            case Biome::Ocean:
            case Biome::Sea:    return 0.0f;
        }
        return 0.0f;
    }

    uint64_t mix(uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    uint64_t candidate_hash(uint32_t seed, int world_cell_x, int world_cell_z)
    {
        uint64_t value = mix(seed);
        value ^= mix(static_cast<uint32_t>(world_cell_x));
        value ^= mix(static_cast<uint64_t>(static_cast<uint32_t>(world_cell_z)) << 1);
        return mix(value);
    }

    float unit_float(uint64_t hash)
    {
        return static_cast<float>(hash & 0x00ffffffULL) / static_cast<float>(0x01000000ULL);
    }
}

void StructureGenerator::generate(Chunk& chunk, const TerrainNoise& noise, int chunk_x, int chunk_z) const
{
    constexpr int CELLS_PER_CHUNK = CHUNK_SIZE / CANDIDATE_CELL_SIZE;

    for (int cell_z = 0; cell_z < CELLS_PER_CHUNK; ++cell_z) {
        for (int cell_x = 0; cell_x < CELLS_PER_CHUNK; ++cell_x) {
            int min_x = std::max(cell_x * CANDIDATE_CELL_SIZE, TREE_RADIUS);
            int max_x = std::min((cell_x + 1) * CANDIDATE_CELL_SIZE - 1, CHUNK_SIZE - TREE_RADIUS - 1);
            int min_z = std::max(cell_z * CANDIDATE_CELL_SIZE, TREE_RADIUS);
            int max_z = std::min((cell_z + 1) * CANDIDATE_CELL_SIZE - 1, CHUNK_SIZE - TREE_RADIUS - 1);
            if (min_x > max_x || min_z > max_z) continue;

            int world_cell_x = chunk_x * CELLS_PER_CHUNK + cell_x;
            int world_cell_z = chunk_z * CELLS_PER_CHUNK + cell_z;
            uint64_t hash = candidate_hash(world_seed, world_cell_x, world_cell_z);

            int local_x = min_x + static_cast<int>((hash >> 24) % static_cast<uint64_t>(max_x - min_x + 1));
            int local_z = min_z + static_cast<int>((hash >> 32) % static_cast<uint64_t>(max_z - min_z + 1));
            int world_x = chunk_x * CHUNK_SIZE + local_x;
            int world_z = chunk_z * CHUNK_SIZE + local_z;

            Biome biome = noise.biome(static_cast<float>(world_x), static_cast<float>(world_z));
            if (unit_float(hash) >= tree_chance(biome)) continue;

            int surface_y = -1;
            for (int y = CHUNK_HEIGHT - 2; y >= 0; --y) {
                if (chunk.get_block(local_x, y, local_z) == BlockType::Grass &&
                    chunk.get_block(local_x, y + 1, local_z) == BlockType::Air) {
                    surface_y = y;
                    break;
                }
            }
            if (surface_y < 0) continue;

            int trunk_height = MIN_TREE_HEIGHT + static_cast<int>((hash >> 40) % TREE_HEIGHT_VARIANTS);
            if (surface_y + trunk_height + 1 >= CHUNK_HEIGHT) continue;

            // Reject an obstructed trunk, but allow leaves to merge with a
            // neighboring tree's canopy when the forest is dense.
            bool trunk_clear = true;
            for (int y = 1; y <= trunk_height; ++y) {
                BlockType existing = chunk.get_block(local_x, surface_y + y, local_z);
                if (existing != BlockType::Air && existing != BlockType::Foliage) {
                    trunk_clear = false;
                    break;
                }
            }
            if (!trunk_clear) continue;

            Structure tree = make_oak_tree(trunk_height);
            place(chunk, tree, local_x, surface_y + 1, local_z);
        }
    }
}

bool StructureGenerator::place(Chunk& chunk, const Structure& structure,
                               int origin_x, int origin_y, int origin_z) const
{
    for (const StructureBlock& block : structure.get_blocks()) {
        int x = origin_x + block.x;
        int y = origin_y + block.y;
        int z = origin_z + block.z;
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
            return false;
        }
    }

    for (const StructureBlock& block : structure.get_blocks()) {
        int x = origin_x + block.x;
        int y = origin_y + block.y;
        int z = origin_z + block.z;
        BlockType existing = chunk.get_block(x, y, z);

        bool can_replace = existing == BlockType::Air;
        if (block.replace_rule == StructureReplaceRule::AirOrFoliage) {
            can_replace = can_replace || existing == BlockType::Foliage;
        }
        if (can_replace) chunk.set_block(x, y, z, block.type);
    }
    return true;
}
