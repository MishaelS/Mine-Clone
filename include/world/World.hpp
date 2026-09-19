#pragma once

#include "raylib.h"
#include "world/Chunk.hpp"
#include "world/ChunkWorkerPool.hpp"
#include "core/Block.hpp"
#include "core/BlockShape.hpp"
#include "core/TickMotion.hpp"
#include "player/Inventory.hpp"
#include "player/Smelting.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class TerrainNoise;
enum class Biome : uint8_t;

// Everything World needs to know before it generates a single chunk -
// passed to the constructor rather than set via setters afterward, since
// set_world()/find_spawn_position() use the World the instant it exists
// (find_spawn_position() itself calls update_chunk_states()); a
// setter-based design risks a call site constructing a World and using it
// before remembering to configure it, silently falling back to whatever a
// default would be. A constructor argument makes that impossible.
struct WorldConfig {
    uint32_t seed = 0;

    // saves/<world>/ (see core/WorldSave.hpp) - nullopt disables
    // persistence entirely (nothing is loaded from or saved to disk; every
    // chunk is always freshly generated, same as before world saving
    // existed). Set for every real, menu-created world.
    std::optional<std::string> save_directory;

    int loaded_radius_chunks = 8; // today's old hardcoded LOADED_RADIUS
    int active_radius_chunks = 4; // today's old hardcoded ACTIVE_RADIUS
    int fog_distance_blocks = 102; // today's old derived default (8 chunks * 16 blocks/chunk * 0.8)
};

// What update_chunk_states_blocking() is doing, for a loading screen - see
// World::set_load_progress_callback().
enum class WorldLoadStage : uint8_t { Terrain, Lighting, Meshes };
using WorldLoadProgress = std::function<void(WorldLoadStage stage, float progress)>;

// Owns every currently-loaded chunk in the world (streamed in/out around an
// observer position, see update_chunk_states - nothing is loaded up front,
// and there's no fixed world size: any (x, z) within WORLD_BORDER_CHUNKS of
// the origin - see the .cpp - can have a chunk generated for it on demand,
// same as Minecraft's own "technically bounded, practically infinite" world
// border), and everything that needs to see across chunk borders:
// generation order, camera raycasts, and block edits (which must relight
// and re-mesh not just the edited chunk but its neighbors too).
class World {
public:
    // Loads nothing by itself - chunks appear (and disappear) as
    // update_chunk_states() is called, same as any other point in the
    // game's lifetime; construct after the window exists, since the first
    // generated chunk's mesh needs a GL context.
    explicit World(WorldConfig config);

    // No longer trivial: flushes every still-resident modified chunk to
    // disk (config.save_directory permitting) before this World's chunk
    // map is torn down - see generate_chunk()/unload_chunk() for the other
    // two places a chunk's data reaches disk. This is the safety net for
    // "the game just quit outright" (GameEngine has no other call site
    // that remembers to save), and for any future code path that destroys
    // a World some other way.
    ~World();

    uint32_t seed() const { return config.seed; }

    // Opaque terrain only: every loaded chunk that's actually in view
    // (skips anything the camera clearly isn't looking toward - an
    // approximate cone test, not exact frustum culling, see the .cpp),
    // and sets up fog for the frame (fades chunks near the configured
    // render distance's own edge into the sky - World's own distance fog,
    // see set_chunk_fog in Chunk.hpp - instead of drawing right up to a
    // hard cutoff where they'd just pop out of existence). Call before
    // drawing anything else in the 3D scene - draw_translucent() below
    // depends on this frame's fog/visibility already being set up, and
    // needs solid entities (dropped items, falling blocks) drawn in
    // between the two so water can correctly blend over whatever's
    // actually underwater (see draw_translucent()'s own comment).
    void draw_opaque(const Camera3D& camera) const;

    // Every see-through layer (glass, ice, water, ...) from every chunk in
    // view, sorted back-to-front and alpha-blended without writing depth -
    // see the .cpp for why. Call *after* draw_opaque() and after drawing
    // any solid entity that should be correctly seen through water rather
    // than always rendering in front of it (dropped items, falling
    // blocks, particles) - water composites over the color+depth buffer
    // however it's built up by then, so anything meant to look properly
    // submerged has to already be in it before this runs.
    void draw_translucent(const Camera3D& camera) const;

    // Debug aid: a wireframe box around every currently-loaded chunk's full
    // column (CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE), so the chunk grid
    // itself is visible regardless of terrain - toggled by GameEngine's F4.
    // Draws every loaded chunk, not just draw()'s own visible/culled set:
    // there are few enough of them (bounded by the configured render distance) that skipping
    // the cull is simpler and the cost difference isn't worth the extra
    // bookkeeping for a debug-only feature.
    void draw_chunk_borders() const;

    // World-space (not chunk-local) block coordinates. Out-of-range (either
    // past the edge of the generated world, or above/below a chunk's own
    // height - there's no vertical chunk stacking yet) reads as Air.
    BlockType get_block(int x, int y, int z) const;

    // max(sky, block) light, 0..15, at world-space (x, y, z). Out-of-range
    // reads as MAX_LIGHT (open, sunlit space), same as Chunk::get_light.
    // This is the raw, time-invariant *potential* light - real Minecraft
    // never touches its own stored skylight with time of day either, only
    // how much of it actually shows right now (see get_effective_light()
    // below) - so this alone isn't what a future gameplay check (plant
    // growth, mob spawning) should key off, only what a debug readout or
    // the propagation algorithm itself needs.
    int get_light(int x, int y, int z) const;

    // The two channels get_light() maxes together, exposed separately - see
    // Chunk::get_sky_light()/get_block_light()'s own comment. Out-of-range
    // reads as MAX_LIGHT sky / 0 block (open, sunlit space has no block
    // light source of its own).
    int get_sky_light(int x, int y, int z) const;
    int get_block_light(int x, int y, int z) const;

    // Real "how lit is this right now" query - day/night-adjusted, unlike
    // get_light() above. `sky_light_factor` is DayNightCycle::
    // sky_light_factor(game_tick) - World has no clock of its own, so the
    // caller (GameEngine, already computing this same value once per frame
    // for the chunk mesh shader/entity tint) passes it in rather than this
    // taking a game_tick and reaching for DayNightCycle itself. Block light
    // (torches, lava) is returned unscaled; only the sky contribution is.
    int get_effective_light(int x, int y, int z, float sky_light_factor) const;

    // Which chunk (chunk-grid coordinates, not world-space) a world-space
    // (x, z) falls in - for the debug overlay. Doesn't check whether that
    // chunk is actually loaded.
    struct ChunkCoordinates { int x, z; };
    ChunkCoordinates chunk_coordinates(int x, int z) const;

    // Which biome a world-space (x, z) falls in - for the debug overlay.
    // Pure function of position (TerrainNoise's temperature/humidity
    // layers), so unlike get_block()/get_light() this doesn't need a chunk
    // there to be loaded at all.
    Biome get_biome(int x, int z) const;

    // Biome-gradient color of foliage in this world column. Falls back to
    // the ordinary foliage definition if the chunk is not currently loaded.
    Color get_foliage_tint(int x, int z) const;
    Color get_grass_tint(int x, int z) const;

    // std::nullopt if `position` isn't inside a Water block; otherwise how
    // many more Water blocks sit directly above it before the water body
    // ends (0 = already the topmost, i.e. right under the surface) - for
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
        float distance = 0.0f;
        // Exact world-space point the ray hit, on that same entry face -
        // used to tell which half of a cell was clicked (e.g. a trapdoor's
        // own top/bottom-of-block mount side) when the entry face alone
        // (normal) isn't enough. {0,0,0} default only matters for whatever
        // never got a real raycast() result in the first place.
        Vector3 hit_point{};
    };
    std::optional<RaycastHit> raycast(Vector3 origin, Vector3 direction, float max_distance) const;

    // Removes the block at the given world-space coordinates (sets it to
    // Air) and rebuilds everything that depends on it: that block's own
    // chunk's lighting, then that chunk's mesh plus its up to 8 border
    // neighbors' meshes (their AO/smooth-lighting samples can reach one
    // cell into the chunk that just changed). No-op if there's no solid,
    // in-range block there. Also schedules this cell and its neighbors for
    // a fluid re-check (see update_fluids()) - removing a block can open a
    // path for nearby water to flow into, or remove what was holding a
    // flow up - and schedules the cell directly above for a falling-block
    // check (see update_falling_blocks()), in case it was Sand or Gravel
    // resting on what's now gone.
    // Removes and returns the broken block so the caller can spawn its item
    // drop. Bedrock and empty/non-solid cells return std::nullopt.
    std::optional<BlockType> break_block(int x, int y, int z);

    // Places a block of the given type at the given world-space
    // coordinates, then relights/remeshes exactly like break_block() does.
    // No-op if that cell is out of range or already occupied by something
    // solid - placement only ever fills empty space, it doesn't replace an
    // existing block. Also schedules a fluid re-check of the placed cell's
    // neighbors - see break_block()/update_fluids() - and, if the placed
    // block is itself Sand or Gravel, a falling-block check of its own
    // cell (see update_falling_blocks()), in case it was placed over open
    // space.
    // True only when the block was actually placed.
    bool place_block(int x, int y, int z, BlockType type);

    // Replaces an existing oak slab with its full-block material. Used by
    // placement when the player adds the missing slab half to the same
    // cell, Minecraft-style.
    bool combine_oak_slab(int x, int y, int z);

    // Atomically places both halves of a door: `lower_type` (OakDoorLower
    // or IronDoorLower) at (x, y, z), its matching upper half directly
    // above. Requires a solid block below (same "needs support" class as
    // Torch/OakSapling in place_block()'s own precondition chain). Rolls
    // the lower half back via break_block() if the upper half can't be
    // placed (blocked, out of range, ...), so a door can never end up
    // half-placed. Both halves are given the same `facing`; hinge side is
    // fixed left for now (see BlockInstanceState's own comment).
    bool place_door(int x, int y, int z, BlockType lower_type, HorizontalDirection facing);

    // Atomically places a bed: BedFoot at (x, y, z), BedHead (the pillow
    // end) one cell away in `facing`'s direction - toward the placing
    // player, since `facing` comes from direction_facing_player() - rolled
    // back via break_block() if the head cell can't be placed.
    bool place_bed(int x, int y, int z, HorizontalDirection facing);

    // Places a single Chest at (x, y, z), then checks its 4 horizontal
    // neighbors for another single Chest with matching facing to merge
    // into a large/double chest - refused (this chest stays single) if
    // either diagonal neighbor of the resulting pair is already part of a
    // different large chest. See core/BlockShape.hpp's ChestPart/
    // BlockStateBits::MULTIBLOCK_PART_MASK for how the pairing itself is
    // stored; inventories are never moved (World::chest_inventory() stays
    // keyed per-position), only the pairing flag changes.
    bool place_chest(int x, int y, int z, HorizontalDirection facing);

    // Unconditionally overwrites every cell in the box from (min_x, min_y,
    // min_z) to (max_x, max_y, max_z) inclusive with `type` - for chat
    // commands (/setblock, as a 1x1x1 box, and /fill) that need to replace
    // whatever's already there, unlike place_block()'s "only ever fills
    // empty space" rule meant for the player's own right-click placement.
    // Writes every cell first, then relights/remeshes each touched chunk
    // exactly once at the end - critical for a large box: doing that full
    // relight+remesh pass after every single cell instead (what looping
    // place_block()-style calls would do) turns a big /fill into a
    // multi-second freeze. Cells outside the world's own Y range or outside
    // a chunk that isn't currently loaded are silently skipped. Returns how
    // many cells were actually written.
    int command_fill_region(int min_x, int min_y, int min_z, int max_x, int max_y, int max_z, BlockType type);

    // Same batching idea as command_fill_region(), for /clone: reads the
    // whole source box (min_x..max_x, min_y..max_y, min_z..max_z) before
    // writing anything back - safe even when the destination overlaps the
    // source, which a same-world "shift this build over" clone commonly
    // does - then writes it to a same-sized box whose lowest corner is
    // (dest_x, dest_y, dest_z). Returns how many cells were actually
    // written.
    int command_clone_region(int min_x, int min_y, int min_z, int max_x, int max_y, int max_z,
                              int dest_x, int dest_y, int dest_z);

    // One block of a Structure being grown at runtime (see GameEngine::
    // update_sapling_growth) - world-space, arbitrary position, unlike
    // Chunk::set_block()'s chunk-local one StructureGenerator::place()
    // uses at world-generation time. Same replace rule as that function:
    // an Air target always accepts it, a Foliage target only when
    // `allow_foliage_overwrite` is set (StructureReplaceRule::
    // AirOrFoliage) - anything else silently does nothing, so the caller
    // doesn't need its own pre-check. No fluid/falling scheduling (unlike
    // place_block()) - a tree's own blocks never need either.
    void place_structure_block(int x, int y, int z, BlockType type, bool allow_foliage_overwrite);

    // Brings every chunk within config.loaded_radius_chunks/active_radius_chunks chunks of
    // `observer_position` (see the .cpp) up to its correct ChunkState.
    // Async: anything newly in range that isn't generated yet is only
    // queued for ChunkWorkerPool here (see pending_generation) - the chunk
    // doesn't actually exist, and nothing about it is meshed, until a later
    // integrate_worker_results() call picks up the finished result.
    // Anything that fell out of range entirely is still unloaded
    // immediately (cheap - see unload_chunk()), and everything whose
    // neighborhood might now look different (a chunk that just finished
    // generating, one that just unloaded, or plain state-flag flips)
    // schedules a background remesh via request_remesh() instead of
    // rebuilding inline. Cheap to call every tick either way - it only
    // rescans when the observer has moved into a different chunk since the
    // last call; the actual work of draining finished background results
    // happens once a frame in integrate_worker_results() instead, not here
    // (see its own comment for why that split matters).
    //
    // Takes one position because there's one local player today, not a
    // list - see desired_state_for()'s comment for why nothing else here
    // needs to change to grow this into "one call per connected player".
    void update_chunk_states(Vector3 observer_position);

    // The fully synchronous version of update_chunk_states() - generates,
    // lights, and meshes everything newly in range inline, on the calling
    // thread, before returning, exactly like update_chunk_states() itself
    // did before background streaming existed. Used only for the two call
    // sites that need the world fully populated the instant this returns
    // (find_spawn_position(), and GameEngine::start_singleplayer_world()'s
    // "resume where the player left off" path) - the steady-state, called-
    // every-tick path uses the async update_chunk_states() above instead.
    // Safe to call with no locking of its own only because both of those
    // callers run before the game's tick loop (and so ChunkWorkerPool)
    // ever starts dispatching background work.
    void update_chunk_states_blocking(Vector3 observer_position);

    // Called from inside update_chunk_states_blocking() (and so from
    // find_spawn_position()) as it goes - after every chunk generated,
    // around relighting, after every mesh built - with overall progress
    // 0..1 for that pass, so a loading screen can draw a frame while the
    // load still blocks the main thread. Empty (the default) = no reports.
    void set_load_progress_callback(WorldLoadProgress callback) { load_progress = std::move(callback); }

    // Applies a live change to config.loaded_radius_chunks/fog_distance_
    // blocks (Settings' render/fog distance sliders) - GameEngine::tick()
    // calls this once a tick whenever a world exists, so a change made
    // from the pause menu takes effect immediately instead of only on the
    // next world started (see GameEngine::start_singleplayer_world(),
    // which is where these first come from). A no-op call when neither
    // value actually changed, so this is cheap to call unconditionally.
    // fog_distance_blocks needs nothing further - draw_opaque() already
    // reads config.fog_distance_blocks fresh every frame - but
    // loaded_radius_chunks also resets last_observer_chunk, forcing the
    // very next update_chunk_states() to rescan even if the observer
    // hasn't left their current chunk since the last call (that alone is
    // what decides its own early-out, so a changed radius would otherwise
    // sit unapplied until the player actually moved).
    void set_view_distance(int loaded_radius_chunks, int fog_distance_blocks);

    // Drains a small, bounded number of finished background results
    // (ChunkWorkerPool::drain_gen_results()/drain_mesh_results()) and
    // integrates them: a finished generation result gets inserted into
    // World::chunks and has its ChunkState set, then triggers a background
    // remesh of its own coordinate and every neighbor that may have been
    // waiting on it (see request_remesh()); a finished mesh result gets
    // uploaded to the GPU (Chunk::upload_mesh_data() - the only GL call in
    // this whole pipeline, which is exactly why it has to happen here, on
    // the main thread) unless the chunk it was built for has since been
    // unloaded, in which case it's simply discarded. Call exactly once per
    // rendered frame (GameEngine::run(), after the fixed-timestep tick
    // loop, not from inside tick() itself) - tick() can run several times
    // in one frame after a stall (see MAX_TICKS_PER_FRAME), and draining
    // this once per *tick* instead of once per *frame* would let a stall
    // multiply this call's own small per-call budget right on top of the
    // stall that just happened.
    void integrate_worker_results();

    // Finds a spawn point on dry land (never Sea or Ocean) with clear air
    // above the ground for the player to actually appear in, instead of a
    // fixed position that could just as easily land in open water or
    // (now that the world has no fixed size or starting layout) even
    // underground. Searches outward from world origin in expanding rings,
    // generating whatever chunk a promising candidate needs (via
    // update_chunk_states_blocking) to confirm it before accepting it.
    // Practically always returns on the very first candidate or two - land
    // covers roughly half the world - so this isn't the expensive search
    // its worst case suggests.
    Vector3 find_spawn_position();

    // Advances water flow by one game tick, the same way real Minecraft
    // paces it: a change (break_block/place_block, or a fluid cell that
    // just changed) schedules its neighbors for re-evaluation a few ticks
    // later (FLUID_TICK_DELAY, World.cpp) rather than resolving instantly,
    // so a flood visibly advances outward over time instead of completing
    // within one frame. Each due cell is recomputed from its current
    // neighbors (compute_fluid_level) - a plain Air cell next to water
    // becomes water at the right level, an existing FLOWING/FALLING cell
    // whose feed disappeared dries back to Air, and a SOURCE never
    // changes. Call once per tick (GameEngine::tick() does this, alongside
    // update_chunk_states()); a no-op on a tick where nothing is due.
    void update_fluids();

    // Advances Sand/Gravel gravity by one game tick: a change (break_block/
    // place_block, or a block a falling entity just vacated) schedules the
    // relevant cell(s) for a support check next tick, the same "schedule,
    // don't resolve instantly" shape update_fluids() uses; a due cell
    // that's actually unsupported is removed from the grid and becomes its
    // own falling entity (falling_blocks below) instead of the static
    // block just hopping down one cell - real per-tick gravity (see
    // FALLING_BLOCK_GRAVITY_PER_TICK in the .cpp: same 0.04 blocks/tick^2,
    // 0.98 drag, "not slowed by water or lava" numbers real Minecraft's
    // own FallingBlockEntity uses), so a long drop accelerates and covers
    // more ground per tick the longer it falls, instead of a flat one
    // cell/tick crawl. Every entity already in flight also advances one
    // tick here, landing (writing itself back into the grid, relighting/
    // remeshing) once its underside reaches something solid or the world
    // floor. Call once per tick (GameEngine::tick(), alongside
    // update_chunk_states()/update_fluids()); a no-op tick this does
    // nothing on is still cheap - see the .cpp.
    void update_falling_blocks();

    // Draws every in-flight falling-block entity as a full textured cube
    // (rendering/BlockMesh.hpp's draw_block_cube(), the same per-block
    // atlas/shading a chunk mesh or a dropped item uses) at its
    // interpolated position - `tick_alpha` is GameEngine::draw()'s own
    // tick_accumulator / TICK_DURATION, see TickMotion's own comment.
    // Call from inside the same BeginMode3D block World::draw() runs in.
    void draw_falling_blocks(float tick_alpha) const;

    // Which way a directional block (Furnace/Workbench/Dispenser/Pumpkin/
    // JackOLantern) at this position is facing - see Chunk::get_orientation()'s
    // own comment. South (its blocks.json-authored default) for any
    // position that was never explicitly set, including one in an unloaded
    // chunk.
    HorizontalDirection get_block_orientation(int x, int y, int z) const;
    void set_block_orientation(int x, int y, int z, HorizontalDirection direction);

    // Extra per-instance state for a shaped/multi-block BlockType (door/
    // trapdoor open, trapdoor half, door hinge, chest pairing, cake bites) -
    // see Chunk::get_block_state()'s own comment and core/BlockShape.hpp's
    // BlockStateBits. 0 (every field's "nothing special" value) for any
    // position that was never explicitly set, including one in an unloaded
    // chunk.
    uint16_t get_block_state(int x, int y, int z) const;
    void set_block_state(int x, int y, int z, uint16_t packed);

    // This cell's current collision box list, in WORLD space (already
    // offset by x,y,z). Fast path: an ordinary solid, non-custom-shape
    // block (the overwhelming majority) returns one full unit-cube box
    // computed inline, at the same cost as a plain BlockProperties::solid
    // check - no BlockShape lookup, no orientation/block_state read. Empty
    // for a non-solid, non-custom-shape cell (air, torch, rail). The real
    // per-shape list, built from this cell's own facing/block_state, only
    // for a block_has_custom_shape() type - see core/BlockShape.hpp. Used
    // by PlayerController's collision resolution and (in future shaped-
    // mesh work) Chunk::build_mesh_data().
    BlockShapeBoxes collision_boxes_at(int x, int y, int z) const;

    // This cell's current selection/raycast box list, in WORLD space. It is
    // separate from collision: a torch or plant can be targeted without
    // blocking movement, while custom-shape blocks use their placed shape.
    BlockShapeBoxes outline_boxes_at(int x, int y, int z) const;

    // A Chest block's own 27-slot storage, keyed by its world position -
    // created empty the first time a given position is looked up (opening
    // a chest that's never been opened before), so InventoryHud can read
    // and mutate it directly by reference. WorldSave persists every
    // position this returns a reference into (see all_chest_inventories())
    // whenever the world is saved, and GameEngine restores each one by
    // writing straight into the reference this returns on load.
    std::array<ItemStack, INVENTORY_STORAGE_SIZE>& chest_inventory(int x, int y, int z);

    // Removes and returns whatever chest_inventory() had stored at this
    // position (an all-empty array if it was never opened) - called when a
    // Chest block is broken, so its contents can be spilled as dropped
    // items instead of silently vanishing.
    std::array<ItemStack, INVENTORY_STORAGE_SIZE> take_chest_inventory(int x, int y, int z);

    // One entry per chest position chest_inventory() has ever been called
    // for (including a now-empty one - a chest a player opened and took
    // everything back out of), for WorldSave::save_chests() to write out
    // wholesale. ChestPosKey stays private; this is the one sanctioned way
    // to enumerate the map without exposing it.
    struct ChestSnapshot {
        int x, y, z;
        std::array<ItemStack, INVENTORY_STORAGE_SIZE> slots;
    };
    std::vector<ChestSnapshot> all_chest_inventories() const;

    // A Furnace/LitFurnace block's own slots and burn/cook progress, keyed
    // by its world position - created empty on first look-up (opening it),
    // same as chest_inventory(). InventoryHud reads/mutates it by
    // reference; update_furnaces() advances it every game tick whether or
    // not its screen is open.
    FurnaceState& furnace_state(int x, int y, int z);

    // Removes and returns a furnace's state (empty if it never had any) -
    // called when the block is broken, so its items can be spilled.
    FurnaceState take_furnace_state(int x, int y, int z);

    // Every furnace position furnace_state() has ever created, for
    // WorldSave::save_furnaces() - same snapshot shape as
    // all_chest_inventories().
    struct FurnaceSnapshot {
        int x, y, z;
        FurnaceState state;
    };
    std::vector<FurnaceSnapshot> all_furnace_states() const;

    // One game tick of smelting for every furnace in a loaded chunk (see
    // tick_furnace()), switching its block between Furnace and LitFurnace
    // (keeping its facing) whenever it lights up or burns out, same as
    // vanilla. A stale entry whose block is no longer a furnace (replaced
    // by a command) is dropped. Call once per tick from GameEngine::tick().
    void update_furnaces();

    // Every currently loaded chunk's own (chunk_x, chunk_z) - for
    // GameEngine's random-tick dispatcher (update_random_ticks()), which
    // needs to pick a few random block positions inside each one every
    // game tick, the same way real Minecraft's own random ticks do. Same
    // "return a snapshot instead of exposing the map itself" shape as
    // all_chest_inventories() above.
    std::vector<std::pair<int, int>> loaded_chunk_coordinates() const;

private:
    struct ChestPosKey {
        int x, y, z;
        bool operator==(const ChestPosKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct ChestPosKeyHash {
        size_t operator()(const ChestPosKey& key) const {
            size_t h = std::hash<int>()(key.x);
            h = h * 31 + std::hash<int>()(key.y);
            h = h * 31 + std::hash<int>()(key.z);
            return h;
        }
    };
    std::unordered_map<ChestPosKey, std::array<ItemStack, INVENTORY_STORAGE_SIZE>, ChestPosKeyHash> chest_storage;
    // Same position key as chest_storage - see furnace_state().
    std::unordered_map<ChestPosKey, FurnaceState, ChestPosKeyHash> furnace_storage;

    WorldLoadProgress load_progress;

    // shared_ptr, not unique_ptr: a chunk a ChunkWorkerPool mesh job is
    // still reading (as the target or as a neighbor) must stay alive even
    // if World::unload_chunk() erases it from this map while that job is
    // in flight - MeshJobInput carries its own shared_ptr copies for
    // exactly this reason. See make_chunk() (ChunkWorkerPool.hpp) for why a
    // chunk's *destruction* still needs a custom deleter on top of that,
    // not just shared_ptr's own refcounting.
    using ChunkMap = std::unordered_map<int64_t, std::shared_ptr<Chunk>>;

    Chunk* chunk_at(int chunk_x, int chunk_z);
    const Chunk* chunk_at(int chunk_x, int chunk_z) const;

    // The same camera-facing cone test draw_opaque()/draw_translucent()
    // both need - shared here rather than each recomputing its own
    // slightly-different copy. Recomputed by both calls rather than cached
    // for one frame; cheap enough (see draw_opaque()'s own comment) that
    // doing it twice a frame isn't worth the extra state to avoid.
    std::vector<const Chunk*> compute_visible_chunks(const Camera3D& camera) const;

    // Same lookup as chunk_at(), but returns the shared_ptr itself (nullptr
    // if unloaded) instead of a raw pointer - used only where a caller
    // needs to keep the Chunk alive independently of World::chunks, i.e.
    // request_remesh() resolving a MeshJobInput's 9 chunk references.
    std::shared_ptr<Chunk> chunk_shared_at(int chunk_x, int chunk_z) const;

    // Rebuilds one chunk's mesh against its current 8 border neighbors,
    // synchronously and inline, on the calling thread - the same one-shot
    // meshing update_chunk_states() itself used to do for every touched
    // chunk before background streaming existed. Used only by
    // update_chunk_states_blocking()'s own final pass now; the steady-state
    // path schedules a background mesh job via request_remesh() instead.
    // No-op if there's no chunk at (chunk_x, chunk_z).
    void rebuild_mesh(int chunk_x, int chunk_z);

    // rebuild_mesh_neighborhood() on (chunk_x, chunk_z) and all 8 of its
    // neighbors - shared by set_block_and_rebuild(): a single block edit
    // can affect a neighbor's own AO/smooth-lighting samples one cell in.
    // Schedules a background remesh (request_remesh()) for each of the 9,
    // rather than rebuilding any of them inline - a block edit's visual
    // update lands a frame or two later once those jobs complete and
    // integrate, the same small latency any other background-streamed
    // geometry now has.
    void rebuild_mesh_neighborhood(int chunk_x, int chunk_z);

    // Recomputes raw sky/block light for the union of each changed chunk
    // plus its 8 neighbors, with propagation allowed across chunk borders.
    // The outer edge is seeded from still-existing light just outside the
    // relit area, so removing/placing a block near one border does not
    // accidentally erase light arriving from an unrelated chunk beyond it.
    void relight_chunks_around(const std::vector<std::pair<int, int>>& centers);
    void relight_chunk_neighborhood(int chunk_x, int chunk_z);

    // True if every one of (chunk_x, chunk_z)'s own 3x3 neighborhood
    // (itself included) is either present in World::chunks or permanently
    // out of the loaded world (past WORLD_BORDER_CHUNKS, or simply outside
    // config.loaded_radius_chunks and so never requested) - false if any of
    // them is still mid-generation (in `generating`), meaning a mesh job
    // dispatched right now would read a neighbor that doesn't exist yet
    // instead of correctly reading it as not-yet-loaded. request_remesh()
    // checks this before dispatching; the cascade in
    // integrate_worker_results() (re-calling request_remesh() on every
    // neighbor once a generation result lands) is what retries a
    // coordinate this initially turned away once its neighborhood actually
    // does become ready.
    bool remesh_neighborhood_ready(int chunk_x, int chunk_z) const;

    // Schedules a background mesh job for (chunk_x, chunk_z) via
    // worker_pool, unless one is already in flight for it (`meshing`
    // dedupes - in that case this coordinate is instead marked in
    // `remesh_pending`, so integrate_worker_results() re-requests it once
    // the in-flight job lands, since that job may have started reading
    // before whatever change just triggered this call) or its neighborhood
    // isn't ready yet (remesh_neighborhood_ready()) or it isn't loaded at
    // all (nothing to mesh). This is the one place World ever asks for a
    // chunk to be (re)meshed - update_chunk_states(), set_block_and_rebuild()
    // (via rebuild_mesh_neighborhood()), update_fluids(), and
    // update_falling_blocks() all funnel through this instead of meshing
    // inline.
    void request_remesh(int chunk_x, int chunk_z);

    // Pushes a small nearest-first batch from pending_generation into the
    // worker pool. This keeps chunk streaming smooth when a chunk-boundary
    // crossing makes a whole new ring eligible at once.
    void dispatch_pending_generation_jobs();

    // Shared by break_block()/place_block(): writes the new block, marks
    // its chunk modified, and relights/remeshes that chunk's neighborhood.
    // No-op if there's no chunk at these coordinates.
    void set_block_and_rebuild(int x, int y, int z, BlockType type);

    // What a fluid cell's level *should* be right now, purely as a function
    // of its current neighbors - std::nullopt if nothing feeds it (it
    // should be Air). Water directly above always wins (FLUID_LEVEL_
    // FALLING); otherwise it's one more than the lowest effective level
    // among the 4 horizontal neighbors that are Water (a SOURCE or FALLING
    // neighbor counts as level 0 for this), capped at FLUID_LEVEL_MAX_FLOW.
    // Never called for a cell that's itself a SOURCE - update_fluids()
    // checks that first, since a source's level never changes.
    std::optional<uint8_t> compute_fluid_level(int x, int y, int z) const;

    // A pending re-evaluation of one cell, due once World's own fluid_tick
    // (incremented once per update_fluids() call) reaches due_tick - see
    // FLUID_TICK_DELAY.
    struct PendingFluidUpdate { int x, y, z; uint64_t due_tick; };

    // Schedules one cell for re-evaluation FLUID_TICK_DELAY ticks from now,
    // unless it's already scheduled (scheduled_fluid_cells dedupes -
    // update_fluids() itself re-schedules a change's neighbors every time
    // it runs, so without this the queue would grow without bound for any
    // long-lived flow).
    void schedule_fluid_update(int x, int y, int z);

    // schedule_fluid_update() on this cell and its 6 face neighbors -
    // called on any block change that could affect nearby fluid state
    // (break_block, place_block, or update_fluids() itself after a cell it
    // resolved actually changed).
    void schedule_fluid_neighbors(int x, int y, int z);

    std::deque<PendingFluidUpdate> pending_fluid_updates;
    std::unordered_set<int64_t> scheduled_fluid_cells; // packed (x,y,z) currently somewhere in pending_fluid_updates

    // Ticks since this World was created, incremented once per
    // update_fluids() call - its own clock rather than reusing GameEngine's
    // game_tick, so break_block()/place_block() (called from per-frame
    // input handling, not from a tick) can still schedule relative to
    // "now" without World needing the exact tick count threaded in from
    // outside.
    uint64_t fluid_tick = 0;

    // Schedules one cell for a falling-block check next tick, unless
    // already scheduled - same dedup role as scheduled_fluid_cells. A
    // no-op if this cell isn't currently Sand or Gravel, so callers
    // (break_block, update_falling_blocks() itself) can call it on any
    // cell without checking the block type first.
    void schedule_falling_check(int x, int y, int z);

    std::deque<std::array<int, 3>> pending_falling_blocks;
    std::unordered_set<int64_t> scheduled_falling_cells; // packed (x,y,z) currently somewhere in pending_falling_blocks

    // One block, removed from the grid, free-falling under its own tick
    // physics until it lands - see update_falling_blocks(). X/Z never
    // change once it starts (same as vanilla: sand/gravel falls straight
    // down its own column, no horizontal drift), so only Y needs to be a
    // continuous TickMotion; X/Z stay whatever cell it fell from.
    struct FallingBlock {
        TickMotion motion; // .current/.previous.y is the block's center height; .x/.z fixed at spawn
        float velocity_y = 0.0f;
        BlockType type = BlockType::Air;
    };
    std::vector<FallingBlock> falling_blocks;

    // --- Chunk state transitions (see update_chunk_states) ---
    //
    // desired_state_for() is the only one of these that's pure decision-
    // making, with no rendering or GPU work: exactly the part a future
    // networked server would own instead, sending its answer to clients
    // rather than each client computing it from its own hardcoded
    // observer_position. The transition methods below it stay the same
    // either way - they just apply whatever state they're told.
    ChunkState desired_state_for(int chunk_x, int chunk_z, ChunkCoordinates observer_chunk) const;

    // Unloaded -> Loaded/Active: generates terrain + lighting (world data
    // only, no mesh - see update_chunk_states) for a chunk that doesn't
    // exist yet, synchronously and inline on the calling thread. Used only
    // by update_chunk_states_blocking() now - the steady-state path queues
    // generation tickets instead, then feeds them to background workers in
    // small batches.
    void generate_chunk(int chunk_x, int chunk_z);

    // Loaded/Active -> Unloaded: frees the chunk's GPU mesh and block data
    // (no neighborhood remesh here - see update_chunk_states). Persists the
    // chunk to disk first if it was ever modified after generation (see
    // Chunk::is_modified) and this World has a save_directory - an
    // untouched chunk doesn't need saving at all, since generation
    // (terrain + caves) is a deterministic function of (seed, chunk_x,
    // chunk_z) and regenerates identically next time. Safe to call even
    // while a background mesh job is still reading this chunk as a
    // neighbor - erasing it from World::chunks doesn't destroy it out from
    // under that job, since the job holds its own shared_ptr reference (see
    // ChunkMap's own comment).
    void unload_chunk(int chunk_x, int chunk_z);

    // saves/<world>/chunks/<chunk_x>_<chunk_z>.chunk - only meaningful
    // when config.save_directory is set.
    std::string chunk_file_path(int chunk_x, int chunk_z) const;

    WorldConfig config;

    std::unique_ptr<TerrainNoise> terrain_noise;

    // Owns every background worker thread this World uses for generation
    // and meshing - see ChunkWorkerPool's own comment. Declared (and so
    // destroyed) before `chunks`: not load-bearing by itself (World's
    // destructor explicitly calls worker_pool->shutdown() as its very first
    // statement, before either member's automatic teardown could run at
    // all - see ~World()), but keeping the pool's own declaration ahead of
    // the data it touches still reads as the right order.
    std::unique_ptr<ChunkWorkerPool> worker_pool;

    ChunkMap chunks;

    // Chunk-key coordinates (see chunk_key()) with a background job
    // currently in flight - generation dispatch/request_remesh() dedupe
    // against these instead of ever submitting a second job for the same
    // coordinate while one's still running, and integrate_worker_results()
    // erases from them once that coordinate's result comes back.
    std::unordered_set<int64_t> generating;
    std::unordered_set<int64_t> meshing;

    // Not-yet-submitted chunk generation tickets, sorted nearest-first
    // around last_observer_chunk. update_chunk_states() refills this when
    // the observer enters a new chunk; dispatch_pending_generation_jobs()
    // feeds it to ChunkWorkerPool in small batches each tick.
    std::deque<ChunkCoord> pending_generation;

    // Chunk-key coordinates whose data changed again while a mesh job for
    // them was already in flight (request_remesh() marks these instead of
    // submitting a second job - see its own comment on why `meshing` alone
    // isn't enough: the in-flight job may have started reading before the
    // change that triggered this call, so its result can't be assumed to
    // reflect it). integrate_worker_results() re-calls request_remesh() for
    // any coordinate found here once that job's result lands.
    std::unordered_set<int64_t> remesh_pending;

    // Which chunk update_chunk_states() last computed states around, so it
    // can skip rescanning when the observer hasn't left that chunk since -
    // nothing could have changed state if it hasn't. Also doubles as the
    // "current observer position" generation dispatch/request_remesh() pass
    // to ChunkWorkerPool's own job-priority ordering; std::nullopt only
    // before the very first update_chunk_states()/update_chunk_states_
    // blocking() call, before which nothing has been dispatched yet either.
    std::optional<ChunkCoordinates> last_observer_chunk;
};
