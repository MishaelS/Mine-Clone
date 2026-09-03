#pragma once

#include "raylib.h"
#include "WorldObject.hpp"
#include "core/Block.hpp"

#include <array>
#include <cstdint>

class PerlinNoise;

constexpr int CHUNK_SIZE = 16;

// Brightest possible sky/block light level (see Chunk::get_light);
// exported so anything sampling light outside a Chunk (World, the debug
// overlay) can express its own out-of-range fallback in the same units
// rather than a bare magic number.
constexpr int MAX_LIGHT = 15;

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
    // adjacent chunk, so the 4 side neighbors are consulted too — without
    // them, every boundary face would be drawn as an (unnecessary, and
    // visibly wrong from inside solid ground) wall. Vertex AO and smooth
    // lighting sample one cell past a face too, which for a corner vertex
    // can land in a diagonal neighbor instead of a side one, so all 8
    // border chunks are taken — any may be null, at the edge of the world,
    // in which case that side reads as open/fully sunlit as before. Call
    // once after generate_terrain()/compute_lighting(), once every
    // neighbor's block data is also ready; call again after any future
    // in-place block edit.
    void build_mesh(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                     const Chunk* northwest, const Chunk* northeast,
                     const Chunk* southwest, const Chunk* southeast);

    void draw() const override;

    BlockType get_block(int x, int y, int z) const;
    void set_block(int x, int y, int z, BlockType type);

    // max(sky, block) light, 0..15. Public so a neighboring chunk's
    // build_mesh() can sample real light data across a chunk border instead
    // of guessing — bounds-checked to MAX_LIGHT (open, sunlit space)
    // outside this chunk, since there's no neighbor-chunk data to fall back
    // on here; the caller is expected to resolve cross-chunk coordinates
    // itself and call this only with this chunk's own local coordinates.
    int get_light(int x, int y, int z) const;

private:
    static int index(int x, int y, int z);

    // Chunk-local opacity check for light propagation (transparent blocks,
    // including air, let light pass through). Out-of-range counts as open.
    bool is_opaque(int x, int y, int z) const;

    int get_sky_light(int x, int y, int z) const;
    void set_sky_light(int x, int y, int z, int value);
    int get_block_light(int x, int y, int z) const;
    void set_block_light(int x, int y, int z, int value);

    std::array<BlockType, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> blocks;

    // Packed per-cell light: upper nibble = sky light, lower nibble = block
    // light, each 0-15.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> light{};

    // vertexCount == 0 (and mesh_uploaded == false) until build_mesh() runs.
    Mesh mesh{};
    bool mesh_uploaded = false;
};
