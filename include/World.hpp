#pragma once

#include "raylib.h"
#include "Chunk.hpp"
#include "core/Block.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class TerrainNoise;
enum class Biome : uint8_t;

// Owns every currently-loaded chunk in the world (streamed in/out around an
// observer position, see update_chunk_states — nothing is loaded up front,
// and there's no fixed world size: any (x, z) within WORLD_BORDER_CHUNKS of
// the origin — see the .cpp — can have a chunk generated for it on demand,
// same as Minecraft's own "technically bounded, practically infinite" world
// border), and everything that needs to see across chunk borders:
// generation order, camera raycasts, and block edits (which must relight
// and re-mesh not just the edited chunk but its neighbors too).
class World {
public:
    // Loads nothing by itself — chunks appear (and disappear) as
    // update_chunk_states() is called, same as any other point in the
    // game's lifetime; construct after the window exists, since the first
    // generated chunk's mesh needs a GL context.
    explicit World(uint32_t seed);

    // Declared (and defined in World.cpp) even though it's just =default:
    // terrain_noise is a unique_ptr<TerrainNoise> with TerrainNoise only
    // forward-declared here, and the implicit destructor the compiler would
    // otherwise generate at every World-destroying call site needs
    // TerrainNoise's full definition to know how to delete it.
    ~World();

    // Draws every loaded chunk that's actually in view: skips anything the
    // camera clearly isn't looking toward (an approximate cone test, not
    // exact frustum culling — see the .cpp), and fades chunks near
    // LOADED_RADIUS's own edge into the sky (World's own distance fog, see
    // set_chunk_fog in Chunk.hpp) instead of drawing right up to a hard
    // cutoff where they'd just pop out of existence.
    void draw(const Camera3D& camera) const;

    // Debug aid: a wireframe box around every currently-loaded chunk's full
    // column (CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE), so the chunk grid
    // itself is visible regardless of terrain — toggled by GameEngine's F4.
    // Draws every loaded chunk, not just draw()'s own visible/culled set:
    // there are few enough of them (bounded by LOADED_RADIUS) that skipping
    // the cull is simpler and the cost difference isn't worth the extra
    // bookkeeping for a debug-only feature.
    void draw_chunk_borders() const;

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

    // Which biome a world-space (x, z) falls in — for the debug overlay.
    // Pure function of position (TerrainNoise's temperature/humidity
    // layers), so unlike get_block()/get_light() this doesn't need a chunk
    // there to be loaded at all.
    Biome get_biome(int x, int z) const;

    // std::nullopt if `position` isn't inside a Water block; otherwise how
    // many more Water blocks sit directly above it before the water body
    // ends (0 = already the topmost, i.e. right under the surface) — for
    // effects that scale with submersion depth rather than treating
    // "underwater" as all-or-nothing: the camera's own underwater screen
    // tint (GameEngine) and Entity's water-drag movement slowdown.
    std::optional<int> water_depth_at(Vector3 position) const;

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
    // in-range block there. Also schedules this cell and its neighbors for
    // a fluid re-check (see update_fluids()) — removing a block can open a
    // path for nearby water to flow into, or remove what was holding a
    // flow up — and schedules the cell directly above for a falling-block
    // check (see update_falling_blocks()), in case it was Sand or Gravel
    // resting on what's now gone.
    void break_block(int x, int y, int z);

    // Places a block of the given type at the given world-space
    // coordinates, then relights/remeshes exactly like break_block() does.
    // No-op if that cell is out of range or already occupied by something
    // solid — placement only ever fills empty space, it doesn't replace an
    // existing block. Also schedules a fluid re-check of the placed cell's
    // neighbors — see break_block()/update_fluids() — and, if the placed
    // block is itself Sand or Gravel, a falling-block check of its own
    // cell (see update_falling_blocks()), in case it was placed over open
    // space.
    void place_block(int x, int y, int z, BlockType type);

    // Brings every chunk within LOADED_RADIUS/ACTIVE_RADIUS chunks of
    // `observer_position` (see the .cpp) up to its correct ChunkState:
    // generates anything newly in range (world data only), flips the
    // Active/Loaded tick flag on anything that crossed that inner boundary,
    // and unloads anything that fell out of range entirely (world data and
    // GPU mesh both) — then meshes whatever that actually touched, exactly
    // once each, as a last pass (see the .cpp: generating or unloading one
    // chunk can also change how up to 8 neighbors should look, and doing
    // that immediately per chunk instead of batching it meant meshing the
    // same chunk repeatedly, once per neighbor that came or went — measured
    // at 1345 mesh rebuilds for 289 chunks' worth of initial world
    // generation). Cheap to call every tick either way — it only rescans
    // when the observer has moved into a different chunk since the last
    // call.
    //
    // Takes one position because there's one local player today, not a
    // list — see desired_state_for()'s comment for why nothing else here
    // needs to change to grow this into "one call per connected player".
    void update_chunk_states(Vector3 observer_position);

    // Finds a spawn point on dry land (never Sea or Ocean) with clear air
    // above the ground for the player to actually appear in, instead of a
    // fixed position that could just as easily land in open water or
    // (now that the world has no fixed size or starting layout) even
    // underground. Searches outward from world origin in expanding rings,
    // generating whatever chunk a promising candidate needs (via
    // update_chunk_states) to confirm it before accepting it. Practically
    // always returns on the very first candidate or two — land covers
    // roughly half the world — so this isn't the expensive search its
    // worst case suggests.
    Vector3 find_spawn_position();

    // Advances water flow by one game tick, the same way real Minecraft
    // paces it: a change (break_block/place_block, or a fluid cell that
    // just changed) schedules its neighbors for re-evaluation a few ticks
    // later (FLUID_TICK_DELAY, World.cpp) rather than resolving instantly,
    // so a flood visibly advances outward over time instead of completing
    // within one frame. Each due cell is recomputed from its current
    // neighbors (compute_fluid_level) — a plain Air cell next to water
    // becomes water at the right level, an existing FLOWING/FALLING cell
    // whose feed disappeared dries back to Air, and a SOURCE never
    // changes. Call once per tick (GameEngine::tick() does this, alongside
    // update_chunk_states()); a no-op on a tick where nothing is due.
    void update_fluids();

    // Advances Sand/Gravel gravity by one game tick, the same "schedule,
    // don't resolve instantly" shape as update_fluids(): a change
    // (break_block/place_block, or a block that just fell) schedules the
    // relevant cell(s) for re-evaluation, and each due cell falls exactly
    // one block (into whatever's below, water included — the same way
    // real Minecraft's sand/gravel isn't stopped by water, it falls
    // through and replaces it) if what's below still isn't solid, then
    // re-schedules itself for the very next tick to keep falling, or does
    // nothing once it lands. A steady one-block-per-tick fall instead of
    // Minecraft's own accelerating one, since nothing here renders a
    // falling block as its own mid-air entity the way vanilla does — it's
    // just the static block moving one cell at a time. Call once per tick
    // (GameEngine::tick() does this, alongside update_chunk_states()/
    // update_fluids()); a no-op on a tick where nothing is due.
    void update_falling_blocks();

private:
    using ChunkMap = std::unordered_map<int64_t, std::unique_ptr<Chunk>>;

    Chunk* chunk_at(int chunk_x, int chunk_z);
    const Chunk* chunk_at(int chunk_x, int chunk_z) const;

    // Rebuilds one chunk's mesh against its current 8 border neighbors.
    // No-op if there's no chunk at (chunk_x, chunk_z).
    void rebuild_mesh(int chunk_x, int chunk_z);

    // rebuild_mesh() on (chunk_x, chunk_z) and all 8 of its neighbors —
    // shared by set_block_and_rebuild(): a single block edit can affect a
    // neighbor's own AO/smooth-lighting samples one cell in, and there's
    // only ever one edit to react to per call, so meshing its neighborhood
    // immediately doesn't waste anything. update_chunk_states() (many
    // chunks appearing/disappearing per call) instead batches this same
    // 3x3-per-change footprint into one dedup'd pass at the end — see its
    // comment for why doing it per-change there would be wasteful.
    void rebuild_mesh_neighborhood(int chunk_x, int chunk_z);

    // Shared by break_block()/place_block(): writes the new block, marks
    // its chunk modified, and relights/remeshes that chunk's neighborhood.
    // No-op if there's no chunk at these coordinates.
    void set_block_and_rebuild(int x, int y, int z, BlockType type);

    // What a fluid cell's level *should* be right now, purely as a function
    // of its current neighbors — std::nullopt if nothing feeds it (it
    // should be Air). Water directly above always wins (FLUID_LEVEL_
    // FALLING); otherwise it's one more than the lowest effective level
    // among the 4 horizontal neighbors that are Water (a SOURCE or FALLING
    // neighbor counts as level 0 for this), capped at FLUID_LEVEL_MAX_FLOW.
    // Never called for a cell that's itself a SOURCE — update_fluids()
    // checks that first, since a source's level never changes.
    std::optional<uint8_t> compute_fluid_level(int x, int y, int z) const;

    // A pending re-evaluation of one cell, due once World's own fluid_tick
    // (incremented once per update_fluids() call) reaches due_tick — see
    // FLUID_TICK_DELAY.
    struct PendingFluidUpdate { int x, y, z; uint64_t due_tick; };

    // Schedules one cell for re-evaluation FLUID_TICK_DELAY ticks from now,
    // unless it's already scheduled (scheduled_fluid_cells dedupes —
    // update_fluids() itself re-schedules a change's neighbors every time
    // it runs, so without this the queue would grow without bound for any
    // long-lived flow).
    void schedule_fluid_update(int x, int y, int z);

    // schedule_fluid_update() on this cell and its 6 face neighbors —
    // called on any block change that could affect nearby fluid state
    // (break_block, place_block, or update_fluids() itself after a cell it
    // resolved actually changed).
    void schedule_fluid_neighbors(int x, int y, int z);

    std::deque<PendingFluidUpdate> pending_fluid_updates;
    std::unordered_set<int64_t> scheduled_fluid_cells; // packed (x,y,z) currently somewhere in pending_fluid_updates

    // Ticks since this World was created, incremented once per
    // update_fluids() call — its own clock rather than reusing GameEngine's
    // game_tick, so break_block()/place_block() (called from per-frame
    // input handling, not from a tick) can still schedule relative to
    // "now" without World needing the exact tick count threaded in from
    // outside.
    uint64_t fluid_tick = 0;

    // Schedules one cell for a falling-block check next tick, unless
    // already scheduled — same dedup role as scheduled_fluid_cells. A
    // no-op if this cell isn't currently Sand or Gravel, so callers
    // (break_block, update_falling_blocks() itself) can call it on any
    // cell without checking the block type first.
    void schedule_falling_check(int x, int y, int z);

    std::deque<std::array<int, 3>> pending_falling_blocks;
    std::unordered_set<int64_t> scheduled_falling_cells; // packed (x,y,z) currently somewhere in pending_falling_blocks

    // --- Chunk state transitions (see update_chunk_states) ---
    //
    // desired_state_for() is the only one of these that's pure decision-
    // making, with no rendering or GPU work: exactly the part a future
    // networked server would own instead, sending its answer to clients
    // rather than each client computing it from its own hardcoded
    // observer_position. The transition methods below it stay the same
    // either way — they just apply whatever state they're told.
    ChunkState desired_state_for(int chunk_x, int chunk_z, ChunkCoordinates observer_chunk) const;

    // Unloaded -> Loaded/Active: generates terrain + lighting (world data
    // only, no mesh — see update_chunk_states) for a chunk that doesn't
    // exist yet.
    void generate_chunk(int chunk_x, int chunk_z);

    // Loaded/Active -> Unloaded: frees the chunk's GPU mesh and block data
    // (no neighborhood remesh here — see update_chunk_states). TODO:
    // persist a modified chunk's data first — not implemented, so its
    // edits are lost once it unloads.
    void unload_chunk(int chunk_x, int chunk_z);

    // Kept separately from terrain_noise (which only exposes specific
    // named noise layers) since cave generation needs the raw seed itself
    // to derive its own per-chunk deterministic RNG — see
    // Chunk::carve_caves.
    uint32_t seed;

    std::unique_ptr<TerrainNoise> terrain_noise;
    ChunkMap chunks;

    // Which chunk update_chunk_states() last computed states around, so it
    // can skip rescanning when the observer hasn't left that chunk since —
    // nothing could have changed state if it hasn't.
    std::optional<ChunkCoordinates> last_observer_chunk;
};
