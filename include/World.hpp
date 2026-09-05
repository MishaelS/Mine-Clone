#pragma once

#include "raylib.h"
#include "Chunk.hpp"
#include "core/Block.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

class PerlinNoise;

// Owns every currently-loaded chunk in the world (streamed in/out around an
// observer position, see update_chunk_states — nothing is loaded up front),
// and everything that needs to see across chunk borders: generation order,
// camera raycasts, and block edits (which must relight and re-mesh not just
// the edited chunk but its neighbors too).
class World {
public:
    // `size_in_blocks` must be a multiple of CHUNK_SIZE; it's rounded down
    // to the nearest one otherwise. Loads nothing by itself — chunks appear
    // (and disappear) as update_chunk_states() is called, same as any other
    // point in the game's lifetime; construct after the window exists,
    // since the first generated chunk's mesh needs a GL context.
    World(int size_in_blocks, uint32_t seed);

    // Declared (and defined in World.cpp) even though it's just =default:
    // terrain_noise is a unique_ptr<PerlinNoise> with PerlinNoise only
    // forward-declared here, and the implicit destructor the compiler would
    // otherwise generate at every World-destroying call site needs
    // PerlinNoise's full definition to know how to delete it.
    ~World();

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

    // Brings every chunk within LOADED_RADIUS/ACTIVE_RADIUS chunks of
    // `observer_position` (see the .cpp) up to its correct ChunkState:
    // generates and meshes anything newly in range, flips the Active/Loaded
    // tick flag on anything that crossed that inner boundary, and unloads
    // anything that fell out of range entirely. Cheap to call every tick —
    // it only rescans when the observer has moved into a different chunk
    // since the last call.
    //
    // Takes one position because there's one local player today, not a
    // list — see desired_state_for()'s comment for why nothing else here
    // needs to change to grow this into "one call per connected player".
    void update_chunk_states(Vector3 observer_position);

private:
    using ChunkMap = std::unordered_map<int64_t, std::unique_ptr<Chunk>>;

    Chunk* chunk_at(int chunk_x, int chunk_z);
    const Chunk* chunk_at(int chunk_x, int chunk_z) const;

    // Rebuilds one chunk's mesh against its current 8 border neighbors.
    // No-op if there's no chunk at (chunk_x, chunk_z).
    void rebuild_mesh(int chunk_x, int chunk_z);

    // rebuild_mesh() on (chunk_x, chunk_z) and all 8 of its neighbors —
    // shared by set_block_and_rebuild() (a block edit can affect a
    // neighbor's own AO/smooth-lighting samples one cell in) and
    // load_chunk()/unload_chunk() (a chunk appearing or disappearing
    // changes whether its neighbors' border faces should be culled).
    void rebuild_mesh_neighborhood(int chunk_x, int chunk_z);

    // Shared by break_block()/place_block(): writes the new block, marks
    // its chunk modified, and relights/remeshes that chunk's neighborhood.
    // No-op if there's no chunk at these coordinates.
    void set_block_and_rebuild(int x, int y, int z, BlockType type);

    // --- Chunk state transitions (see update_chunk_states) ---
    //
    // desired_state_for() is the only one of these that's pure decision-
    // making, with no rendering or GPU work: exactly the part a future
    // networked server would own instead, sending its answer to clients
    // rather than each client computing it from its own hardcoded
    // observer_position. The transition methods below it stay the same
    // either way — they just apply whatever state they're told.
    ChunkState desired_state_for(int chunk_x, int chunk_z, ChunkCoordinates observer_chunk) const;

    // Unloaded -> Loaded/Active: generates terrain + lighting (world data)
    // for a chunk that doesn't exist yet, then meshes it and its
    // neighborhood (client/rendering work).
    void load_chunk(int chunk_x, int chunk_z);

    // Loaded/Active -> Unloaded: frees the chunk's GPU mesh and block data,
    // then remeshes its neighborhood. TODO: persist a modified chunk's data
    // first — not implemented, so its edits are lost once it unloads.
    void unload_chunk(int chunk_x, int chunk_z);

    int chunks_per_axis;
    std::unique_ptr<PerlinNoise> terrain_noise;
    ChunkMap chunks;

    // Which chunk update_chunk_states() last computed states around, so it
    // can skip rescanning when the observer hasn't left that chunk since —
    // nothing could have changed state if it hasn't.
    std::optional<ChunkCoordinates> last_observer_chunk;
};
