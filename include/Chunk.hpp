#pragma once

#include "raylib.h"
#include "WorldObject.hpp"
#include "core/Block.hpp"

#include <array>
#include <cstdint>

class PerlinNoise;

constexpr int CHUNK_SIZE = 16;

// A CHUNK_SIZE^3 grid of blocks, positioned in the world by WorldObject's
// position (its min corner, not its center). Block data builds into a single
// GPU mesh (build_mesh()) with hidden faces culled out, so a whole chunk
// draws in one call instead of one draw per visible block face.
class Chunk : public WorldObject {
public:
    explicit Chunk(Vector3 position = {0.0f, 0.0f, 0.0f});
    ~Chunk() override;

    // Owns a GPU mesh; copying would double-free it, so don't.
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // Fills the chunk with terrain: a Perlin-noise height per (x, z) column,
    // stone below it, a few layers of dirt near the surface, grass on top,
    // and bedrock at y=0. `noise` is sampled at this chunk's world-space X/Z
    // so terrain height is continuous across chunk borders.
    void generate_terrain(const PerlinNoise& noise);

    // Full sky+block light recompute via BFS flood fill. Call after the block
    // layout is set. Incremental (BFS-from-the-change-only) updates for
    // placing/breaking single blocks come with Block Interaction.
    void compute_lighting();

    // Rebuilds the GPU mesh from the current block/light data, skipping any
    // face whose neighbor is opaque — hidden faces never make it into the
    // mesh at all. At the chunk's own edges, that neighbor lives in an
    // adjacent chunk, so the 4 side neighbors (any may be null, at the edge
    // of the world) are consulted too — without them, every boundary face
    // would be drawn as an (unnecessary, and visibly wrong from inside solid
    // ground) wall. Call once after generate_terrain()/compute_lighting(),
    // once every neighbor's block data is also ready; call again after any
    // future in-place block edit.
    void build_mesh(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south);

    void draw() const override;

    BlockType get_block(int x, int y, int z) const;
    void set_block(int x, int y, int z, BlockType type);

private:
    static int index(int x, int y, int z);

    // Chunk-local solid check for ambient occlusion; out-of-range counts as
    // not solid since there's no neighbor-chunk data yet.
    bool is_solid(int x, int y, int z) const;

    // Chunk-local opacity check for light propagation (transparent blocks,
    // including air, let light pass through). Out-of-range counts as open.
    bool is_opaque(int x, int y, int z) const;

    // Minecraft-style vertex AO: 0 (darkest) to 3 (no occlusion), based on the
    // two blocks sharing this face-corner's edges and the one at its diagonal.
    int vertex_ao(int x, int y, int z, Vector3 normal, Vector3 corner) const;

    // Average light (0..1) of the same three neighbor cells used for AO, plus
    // the cell right outside the face — the same per-vertex sampling
    // Minecraft calls "smooth lighting".
    float vertex_light(int x, int y, int z, Vector3 normal, Vector3 corner) const;

    int get_sky_light(int x, int y, int z) const;
    void set_sky_light(int x, int y, int z, int value);
    int get_block_light(int x, int y, int z) const;
    void set_block_light(int x, int y, int z, int value);
    // max(sky, block), bounds-checked to 0 outside the chunk.
    int get_light(int x, int y, int z) const;

    std::array<BlockType, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> blocks;

    // Packed per-cell light: upper nibble = sky light, lower nibble = block
    // light, each 0-15.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> light{};

    // vertexCount == 0 (and mesh_uploaded == false) until build_mesh() runs.
    Mesh mesh{};
    bool mesh_uploaded = false;
};
