#pragma once

#include "raylib.h"
#include "WorldObject.hpp"
#include "Block.hpp"

#include <array>
#include <cstdint>

constexpr int CHUNK_SIZE = 16;

// A CHUNK_SIZE^3 grid of blocks, positioned in the world by WorldObject's
// position (its min corner, not its center). Rendered naively for now: one
// DrawCube per non-air block. Mesh-based rendering (one draw call, hidden-face
// culling) comes later.
class Chunk : public WorldObject {
public:
    explicit Chunk(Vector3 position = {0.0f, 0.0f, 0.0f});

    // Placeholder for World Generation: scatters random blocks instead of terrain.
    void Randomize();

    // Full sky+block light recompute via BFS flood fill. Call after the block
    // layout is set. Incremental (BFS-from-the-change-only) updates for
    // placing/breaking single blocks come with Block Interaction.
    void ComputeLighting();

    void Draw() const override;

    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);

private:
    static int Index(int x, int y, int z);

    // Chunk-local solid check for ambient occlusion; out-of-range counts as
    // not solid since there's no neighbor-chunk data yet.
    bool IsSolid(int x, int y, int z) const;

    // Chunk-local opacity check for light propagation (transparent blocks,
    // including air, let light pass through). Out-of-range counts as open.
    bool IsOpaque(int x, int y, int z) const;

    // Minecraft-style vertex AO: 0 (darkest) to 3 (no occlusion), based on the
    // two blocks sharing this face-corner's edges and the one at its diagonal.
    int VertexAO(int x, int y, int z, Vector3 normal, Vector3 corner) const;

    // Average light (0..1) of the same three neighbor cells used for AO, plus
    // the cell right outside the face — the same per-vertex sampling
    // Minecraft calls "smooth lighting".
    float VertexLight(int x, int y, int z, Vector3 normal, Vector3 corner) const;

    int GetSkyLight(int x, int y, int z) const;
    void SetSkyLight(int x, int y, int z, int value);
    int GetBlockLight(int x, int y, int z) const;
    void SetBlockLight(int x, int y, int z, int value);
    // max(sky, block), bounds-checked to 0 outside the chunk.
    int GetLight(int x, int y, int z) const;

    std::array<BlockType, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> blocks;

    // Packed per-cell light: upper nibble = sky light, lower nibble = block
    // light, each 0-15.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> light{};
};
