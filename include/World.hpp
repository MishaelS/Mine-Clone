#pragma once

#include "raylib.h"
#include "Chunk.hpp"
#include "core/Block.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Owns every chunk in the world on a fixed grid (no streaming/unloading
// yet), and everything that needs to see across chunk borders: generation
// order, camera raycasts, and block edits (which must relight and re-mesh
// not just the edited chunk but its neighbors too).
class World {
public:
    // `size_in_blocks` must be a multiple of CHUNK_SIZE; it's rounded down
    // to the nearest one otherwise. Generates and meshes every chunk
    // immediately (needs a GL context — construct after the window exists).
    World(int size_in_blocks, uint32_t seed);

    void draw() const;

    // World-space (not chunk-local) block coordinates. Out-of-range (either
    // past the edge of the generated world, or above/below a chunk's own
    // height — there's no vertical chunk stacking yet) reads as Air.
    BlockType get_block(int x, int y, int z) const;

    // max(sky, block) light, 0..15, at world-space (x, y, z). Out-of-range
    // reads as MAX_LIGHT (open, sunlit space), same as Chunk::get_light.
    int get_light(int x, int y, int z) const;

    // Which chunk (chunk-grid coordinates, not world-space) a world-space
    // (x, z) falls in — for the debug overlay. Doesn't check whether that
    // chunk is actually loaded.
    struct ChunkCoordinates { int x, z; };
    ChunkCoordinates chunk_coordinates(int x, int z) const;

    // A solid block hit by a ray, found by stepping through the voxel grid
    // one cell at a time (Amanatides & Woo traversal) rather than sampling
    // at fixed intervals, so a fast-moving thin ray can't tunnel through a
    // corner or skip past a block between samples.
    struct RaycastHit {
        int x, y, z;
        Vector3 normal; // outward-facing normal of the face the ray entered through
    };
    std::optional<RaycastHit> raycast(Vector3 origin, Vector3 direction, float max_distance) const;

    // Removes the block at the given world-space coordinates (sets it to
    // Air) and rebuilds everything that depends on it: that block's own
    // chunk's lighting, then that chunk's mesh plus its up to 8 border
    // neighbors' meshes (their AO/smooth-lighting samples can reach one
    // cell into the chunk that just changed). No-op if there's no solid,
    // in-range block there.
    void break_block(int x, int y, int z);

    // Places a block of the given type at the given world-space
    // coordinates, then relights/remeshes exactly like break_block() does.
    // No-op if that cell is out of range or already occupied by something
    // solid — placement only ever fills empty space, it doesn't replace an
    // existing block.
    void place_block(int x, int y, int z, BlockType type);

private:
    Chunk* chunk_at(int chunk_x, int chunk_z);
    const Chunk* chunk_at(int chunk_x, int chunk_z) const;

    // Rebuilds one chunk's mesh against its current 8 border neighbors.
    // No-op if there's no chunk at (chunk_x, chunk_z).
    void rebuild_mesh(int chunk_x, int chunk_z);

    // Shared by break_block()/place_block(): writes the new block and
    // relights/remeshes its chunk plus that chunk's up to 8 border
    // neighbors (a block change at the chunk's edge can affect a
    // neighbor's own AO/smooth-lighting samples one cell in). No-op if
    // there's no chunk at these coordinates.
    void set_block_and_rebuild(int x, int y, int z, BlockType type);

    int chunks_per_axis;
    std::vector<std::unique_ptr<Chunk>> chunks;
};
