#pragma once

#include <cstdint>

class Chunk;
class Structure;
class TerrainNoise;

// Selects deterministic structure origins for one chunk and places their
// templates. The same world seed always produces the same structures,
// independent of background worker order.
class StructureGenerator {
public:
    explicit StructureGenerator(uint32_t world_seed) : world_seed(world_seed) {}

    void generate(Chunk& chunk, const TerrainNoise& noise, int chunk_x, int chunk_z) const;

private:
    bool place(Chunk& chunk, const Structure& structure, int origin_x, int origin_y, int origin_z) const;

    uint32_t world_seed;
};
