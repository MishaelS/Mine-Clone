#include "worldgen/StructureGenerator.hpp"

#include "world/Chunk.hpp"
#include "core/Biome.hpp"
#include "core/TerrainNoise.hpp"
#include "worldgen/Structure.hpp"

#include <algorithm>
#include <cstdint>

namespace {
    // One candidate per 4x4 cell, then each structure's chance in its
    // biome (assets/structures - the oak tree's gives roughly 1-2
    // trees/chunk in Plains, 10-11 in Forest, and 3-4 in Hills).
    constexpr int CANDIDATE_CELL_SIZE  = 4;
    constexpr uint32_t SHORT_GRASS_SEED_SALT = 0x6A09E667u;

    float short_grass_chance(Biome biome)
    {
        switch (biome) {
            case Biome::Plains: return 0.30f;
            case Biome::Forest: return 0.20f;
            case Biome::Hills : return 0.10f;
            case Biome::Desert:
            case Biome::Ocean :
            case Biome::Sea   : return 0.0f;
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
    const std::vector<StructureDefinition>& structures = get_structures();

    for (int cell_z = 0; cell_z < CELLS_PER_CHUNK; ++cell_z) {
        for (int cell_x = 0; cell_x < CELLS_PER_CHUNK; ++cell_x) {
            int world_cell_x = chunk_x * CELLS_PER_CHUNK + cell_x;
            int world_cell_z = chunk_z * CELLS_PER_CHUNK + cell_z;
            uint64_t hash = candidate_hash(world_seed, world_cell_x, world_cell_z);
            const float roll = unit_float(hash);

            // One roll per cell picks at most one structure: each takes the
            // next slice of 0..1 its chance there covers (their chances add
            // up), at its own spot in the cell - one that keeps every
            // variant inside this chunk.
            float covered = 0.0f;
            for (const StructureDefinition& structure : structures) {
                if (structure.placed_on.empty()) continue;
                int min_x = std::max(cell_x * CANDIDATE_CELL_SIZE, -structure.min_x);
                int max_x = std::min((cell_x + 1) * CANDIDATE_CELL_SIZE - 1, CHUNK_SIZE - 1 - structure.max_x);
                int min_z = std::max(cell_z * CANDIDATE_CELL_SIZE, -structure.min_z);
                int max_z = std::min((cell_z + 1) * CANDIDATE_CELL_SIZE - 1, CHUNK_SIZE - 1 - structure.max_z);
                if (min_x > max_x || min_z > max_z) continue;

                int local_x = min_x + static_cast<int>((hash >> 24) % static_cast<uint64_t>(max_x - min_x + 1));
                int local_z = min_z + static_cast<int>((hash >> 32) % static_cast<uint64_t>(max_z - min_z + 1));
                int world_x = chunk_x * CHUNK_SIZE + local_x;
                int world_z = chunk_z * CHUNK_SIZE + local_z;

                Biome biome = noise.biome(static_cast<float>(world_x), static_cast<float>(world_z));
                float chance = structure.chance_in(biome);
                if (structure.tree_density) chance *= noise.params().tree_density;
                if (chance <= 0.0f) continue;
                covered += chance;
                if (roll >= covered) continue;

                int surface_y = -1;
                for (int y = CHUNK_HEIGHT - 2; y >= 0; --y) {
                    const BlockType ground = chunk.get_block(local_x, y, local_z);
                    if (std::find(structure.placed_on.begin(), structure.placed_on.end(), ground) != structure.placed_on.end() &&
                        chunk.get_block(local_x, y + 1, local_z) == BlockType::Air) {
                        surface_y = y;
                        break;
                    }
                }
                if (surface_y >= 0) {
                    const Structure& variant = structure.variants[(hash >> 40) % structure.variants.size()];
                    place(chunk, variant, local_x, surface_y + 1, local_z);
                }
                break;
            }
        }
    }

    // Ground plants are generated after trees. This lets trunks/crowns own
    // their cells first and keeps the plant pass from rejecting otherwise
    // valid trees as an obstruction. Every world column has its own stable
    // hash, so generation remains independent of chunk load order.
    for (int local_z = 0; local_z < CHUNK_SIZE; ++local_z) {
        for (int local_x = 0; local_x < CHUNK_SIZE; ++local_x) {
            int world_x = chunk_x * CHUNK_SIZE + local_x;
            int world_z = chunk_z * CHUNK_SIZE + local_z;
            Biome biome = noise.biome(static_cast<float>(world_x), static_cast<float>(world_z));
            float chance = short_grass_chance(biome) * noise.params().grass_density;
            if (chance <= 0.0f) continue;

            uint64_t hash = candidate_hash(world_seed ^ SHORT_GRASS_SEED_SALT, world_x, world_z);
            if (unit_float(hash) >= chance) continue;

            for (int y = CHUNK_HEIGHT - 2; y >= 0; --y) {
                if (chunk.get_block(local_x, y, local_z) != BlockType::Grass) continue;
                if (chunk.get_block(local_x, y + 1, local_z) == BlockType::Air) {
                    chunk.set_block(local_x, y + 1, local_z, BlockType::ShortGrass);
                }
                break;
            }
        }
    }
}

bool StructureGenerator::place(Chunk& chunk, const Structure& structure,
                               int origin_x, int origin_y, int origin_z) const
{
    // All of it inside the chunk, and every required block (a trunk) free
    // to go where it goes - else none of it. A neighbor's leaves in the way
    // are fine for a trunk that may replace foliage.
    for (const StructureBlock& block : structure.get_blocks()) {
        int x = origin_x + block.x;
        int y = origin_y + block.y;
        int z = origin_z + block.z;
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
            return false;
        }
        if (block.required && !structure_can_replace(block.replace_rule, chunk.get_block(x, y, z))) return false;
    }

    for (const StructureBlock& block : structure.get_blocks()) {
        int x = origin_x + block.x;
        int y = origin_y + block.y;
        int z = origin_z + block.z;
        if (structure_can_replace(block.replace_rule, chunk.get_block(x, y, z))) chunk.set_block(x, y, z, block.type);
    }
    return true;
}
