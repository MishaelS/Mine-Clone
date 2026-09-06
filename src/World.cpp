#include "World.hpp"
#include "core/TerrainNoise.hpp"
#include "Skybox.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {
    // Rounds toward negative infinity, unlike C++'s truncating `/`, so a
    // world-space coordinate left of/above chunk 0 (negative x or z) still
    // maps to the chunk that actually contains it instead of off by one.
    int floor_div(int a, int b) {
        int quotient = a / b;
        int remainder = a % b;
        return (remainder != 0 && remainder < 0) ? quotient - 1 : quotient;
    }

    // World::chunks is keyed by a packed (chunk_x, chunk_z) rather than a
    // struct, so it can use std::unordered_map's default hash instead of
    // writing a custom one.
    int64_t chunk_key(int chunk_x, int chunk_z) {
        return (static_cast<int64_t>(chunk_x) << 32) | static_cast<uint32_t>(chunk_z);
    }
    std::pair<int, int> unpack_chunk_key(int64_t key) {
        return {static_cast<int>(key >> 32), static_cast<int>(static_cast<uint32_t>(key))};
    }

    // Packs one block's world-space (x, y, z) into a single key for
    // World::scheduled_fluid_cells — biased to non-negative first since a
    // bitwise packing needs every field's range to start at 0. x/z fit in
    // 25 bits (comfortably covering WORLD_BORDER_BLOCKS, defined further
    // down); y in 11 (the world is nowhere near 2000 blocks tall).
    int64_t fluid_key(int x, int y, int z) {
        constexpr int64_t BIAS_XZ = 10'000'000;
        constexpr int64_t BIAS_Y = 1000;
        int64_t bx = x + BIAS_XZ;
        int64_t by = y + BIAS_Y;
        int64_t bz = z + BIAS_XZ;
        return (bx << 36) | (by << 25) | bz;
    }

    // How far, in chunks, a chunk is drawn (LOADED_RADIUS) vs. drawn *and*
    // ticking (ACTIVE_RADIUS) around an observer — Minecraft's own render
    // vs. simulation distance split. Square (Chebyshev) radius, same shape
    // Minecraft loads in, not a circle.
    constexpr int LOADED_RADIUS = 8;
    constexpr int ACTIVE_RADIUS = 4;

    // Water flow (World::update_fluids): how many ticks after a cell is
    // scheduled before it's actually re-evaluated — the same idea as real
    // Minecraft's own liquid tick rate (5 game ticks in Java Edition), so
    // a flow visibly advances outward one step at a time instead of
    // instantly resolving the moment something changes.
    constexpr int FLUID_TICK_DELAY = 5;

    // Caps how many due fluid cells update_fluids() resolves in a single
    // tick — the same "spiral of death" caution MAX_TICKS_PER_FRAME uses
    // elsewhere (GameEngine.cpp), here against an enormous number of cells
    // all coming due on the same tick (e.g. draining a whole lake) turning
    // one tick into a multi-chunk remesh storm. Anything past this limit
    // is simply left due (its due_tick already <= fluid_tick) and picked
    // up first thing on the very next call instead of being delayed
    // further or dropped.
    constexpr int MAX_FLUID_UPDATES_PER_TICK = 64;

    // Same "spiral of death" caution as MAX_FLUID_UPDATES_PER_TICK, for
    // Sand/Gravel gravity (World::update_falling_blocks) — against, say, a
    // huge floating platform losing its support all at once.
    constexpr int MAX_FALLING_UPDATES_PER_TICK = 128;

    // Fog (see World::draw/set_chunk_fog) fully hides everything by
    // FOG_END_FRACTION of LOADED_RADIUS's own distance, not right at it —
    // so a chunk unloading at the render-distance edge does so already
    // inside the fog, never visibly. FOG_START_FRACTION is relative to that
    // (reduced) end distance, not to LOADED_RADIUS directly: fog starts
    // ramping in at FOG_END_FRACTION*FOG_START_FRACTION of LOADED_RADIUS.
    constexpr float FOG_END_FRACTION = 0.8f;
    constexpr float FOG_START_FRACTION = 0.5f;

    // Underwater fog: real Minecraft doesn't darken the water block's own
    // surface color by depth (it stays one plain color everywhere) —
    // instead, whenever the *camera's* eye is inside a fluid, it swaps in a
    // much shorter, fluid-colored fog in place of the normal render-
    // distance one, so visibility itself is what gets worse, not the
    // water's own paint job. World::draw does the same swap here whenever
    // water_depth_at(camera.position) says the camera is submerged, using
    // that same depth to pick how short/dark the fog gets: barely
    // submerged is still fairly clear (UNDERWATER_FOG_END_SHALLOW),
    // deepening toward UNDERWATER_FOG_END_DEEP/UNDERWATER_FOG_COLOR_DEEP by
    // UNDERWATER_FOG_MAX_DEPTH blocks down. fogStart is a small fraction of
    // fogEnd rather than 0 outright, so the blend into fog isn't a visible
    // hard edge right at the near plane.
    constexpr float UNDERWATER_FOG_START_FRACTION = 0.1f;
    constexpr float UNDERWATER_FOG_END_SHALLOW = 20.0f;
    constexpr float UNDERWATER_FOG_END_DEEP = 6.0f;
    constexpr Color UNDERWATER_FOG_COLOR_SHALLOW = {40, 90, 160, 255};
    constexpr Color UNDERWATER_FOG_COLOR_DEEP = {5, 15, 35, 255};
    constexpr int UNDERWATER_FOG_MAX_DEPTH = 24; // matches Ocean's own depth range, see Chunk.cpp's biome_terrain

    // Chunks within this many blocks of the camera are always drawn — the
    // view-cone test below approximates visibility by angle alone, which
    // breaks down at very close range (a chunk right next to the camera can
    // legitimately be visible well outside a "reasonable" cone), so it
    // doesn't get applied there at all.
    constexpr float ALWAYS_VISIBLE_BLOCKS = 3.0f * CHUNK_SIZE;

    // cos(70 degrees). An approximate view-cone test, not exact frustum
    // culling: exact culling needs frustum planes extracted from the
    // camera's view-projection matrix, which depends on getting raylib's
    // exact matrix convention (row- vs column-vector) right — a mismatch
    // there fails silently as chunks incorrectly popping out of view, which
    // is a much worse bug than under-culling. 70 degrees is a deliberately
    // generous margin over the actual worst case at the default 1280x720
    // window (fovy 60 => ~46 degree half-FOV horizontally, ~50 degrees to
    // the frustum's own corner) — it still culls whatever's clearly behind
    // or well to the side of the camera, just not as tightly as the exact
    // frustum would.
    constexpr float VIEW_CONE_COS = 0.342f;

    // Approximates whether a chunk (by its column footprint in the X/Z
    // plane — chunks span the whole world height, so Y never narrows this)
    // is worth drawing from the camera's position/facing. Never a false
    // negative by a wide margin (see VIEW_CONE_COS) — the goal is skipping
    // what's clearly not on screen, not a tight match to it.
    bool chunk_in_view(Vector3 chunk_min_corner, Vector3 camera_position, Vector3 camera_forward) {
        float to_chunk_x = (chunk_min_corner.x + CHUNK_SIZE / 2.0f) - camera_position.x;
        float to_chunk_z = (chunk_min_corner.z + CHUNK_SIZE / 2.0f) - camera_position.z;
        float distance = std::sqrt(to_chunk_x * to_chunk_x + to_chunk_z * to_chunk_z);
        if (distance <= ALWAYS_VISIBLE_BLOCKS) return true;

        float forward_length = std::sqrt(camera_forward.x * camera_forward.x + camera_forward.z * camera_forward.z);
        if (forward_length < 1e-4f) return true; // looking straight up/down: no horizontal facing to cull against

        float cos_angle = (to_chunk_x * camera_forward.x + to_chunk_z * camera_forward.z) / (distance * forward_length);
        return cos_angle >= VIEW_CONE_COS;
    }

    // The world has no fixed size — any chunk within this many blocks of
    // the origin can be generated on demand (World::chunk_at), same idea as
    // Minecraft's own world border: technically a limit, practically never
    // reached by walking. It's nowhere near Minecraft's actual 29,999,984,
    // deliberately: this project stores every position in a 32-bit float
    // (raylib's Vector3), and float can only represent every integer
    // exactly up to 2^24 (16,777,216) — past that, block positions start
    // rounding to the nearest representable value, which looks like blocks
    // jittering off-grid. Minecraft avoids this because it stores position
    // in a 64-bit double internally; matching its exact border number here
    // without also switching this project to double-precision positions
    // (or a camera-relative "floating origin", the other common fix) would
    // just mean the world visibly falls apart before you ever reached it.
    // 8,000,000 keeps a comfortable 2x safety margin under that limit while
    // still being, for any practical purpose, unreachable on foot.
    constexpr int WORLD_BORDER_BLOCKS = 8'000'000;
    constexpr int WORLD_BORDER_CHUNKS = WORLD_BORDER_BLOCKS / CHUNK_SIZE;

    int chebyshev_distance(int ax, int az, int bx, int bz) {
        return std::max(std::abs(ax - bx), std::abs(az - bz));
    }

    // draw_chunk_borders(): magenta doesn't occur naturally in terrain, so
    // it reads clearly as a debug overlay against any biome.
    constexpr Color CHUNK_BORDER_COLOR = {255, 0, 255, 255};
}

World::World(uint32_t seed)
    : seed(seed)
    , terrain_noise(std::make_unique<TerrainNoise>(seed))
{
    // Nothing is loaded yet — the first update_chunk_states() call (see
    // GameEngine::set_world()/tick()) populates the world around wherever
    // the observer actually starts.
}

World::~World() = default;

const Chunk* World::chunk_at(int chunk_x, int chunk_z) const
{
    if (chunk_x < -WORLD_BORDER_CHUNKS || chunk_x >= WORLD_BORDER_CHUNKS ||
        chunk_z < -WORLD_BORDER_CHUNKS || chunk_z >= WORLD_BORDER_CHUNKS) {
        return nullptr;
    }
    auto it = chunks.find(chunk_key(chunk_x, chunk_z));
    return it != chunks.end() ? it->second.get() : nullptr;
}

// One real implementation (above) instead of two identical bodies — the
// standard way to share a const/non-const accessor pair (Meyers, Effective
// C++ Item 3).
Chunk* World::chunk_at(int chunk_x, int chunk_z)
{
    return const_cast<Chunk*>(std::as_const(*this).chunk_at(chunk_x, chunk_z));
}

void World::rebuild_mesh(int chunk_x, int chunk_z)
{
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    chunk->build_mesh(
        chunk_at(chunk_x - 1, chunk_z),     chunk_at(chunk_x + 1, chunk_z),
        chunk_at(chunk_x, chunk_z - 1),     chunk_at(chunk_x, chunk_z + 1),
        chunk_at(chunk_x - 1, chunk_z - 1), chunk_at(chunk_x + 1, chunk_z - 1),
        chunk_at(chunk_x - 1, chunk_z + 1), chunk_at(chunk_x + 1, chunk_z + 1));
}

void World::rebuild_mesh_neighborhood(int chunk_x, int chunk_z)
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            rebuild_mesh(chunk_x + dx, chunk_z + dz);
        }
    }
}

void World::draw(const Camera3D& camera) const
{
    float fog_end, fog_start;
    Color fog_color;
    if (auto depth = water_depth_at(camera.position)) {
        // Submerged: swap in underwater fog (see UNDERWATER_FOG_* above)
        // instead of the normal render-distance one — real Minecraft's own
        // approach, rather than darkening the water block's own color.
        float t = std::clamp(static_cast<float>(*depth) / UNDERWATER_FOG_MAX_DEPTH, 0.0f, 1.0f);
        fog_end = UNDERWATER_FOG_END_SHALLOW + (UNDERWATER_FOG_END_DEEP - UNDERWATER_FOG_END_SHALLOW) * t;
        fog_start = fog_end * UNDERWATER_FOG_START_FRACTION;
        fog_color = ColorLerp(UNDERWATER_FOG_COLOR_SHALLOW, UNDERWATER_FOG_COLOR_DEEP, t);
    } else {
        fog_end = LOADED_RADIUS * CHUNK_SIZE * FOG_END_FRACTION;
        fog_start = fog_end * FOG_START_FRACTION;
        fog_color = skybox_horizon_color();
    }
    set_chunk_fog(camera.position, fog_color, fog_start, fog_end);
    set_chunk_water_time(static_cast<float>(GetTime()));

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    std::vector<const Chunk*> visible;
    for (const auto& [key, chunk] : chunks) {
        if (chunk_in_view(chunk->get_position(), camera.position, forward)) {
            visible.push_back(chunk.get());
        }
    }

    // Opaque geometry first, world-wide, before any translucent (water)
    // geometry anywhere — alpha blending needs to composite over the
    // finished opaque picture, not however opaque and translucent chunks
    // would otherwise interleave by draw order alone.
    set_chunk_water_pass(false);
    for (const Chunk* chunk : visible) {
        chunk->draw();
    }

    // Water: alpha blended, and not depth-*written* (only depth-*tested*,
    // so solid terrain in front of it still correctly hides it). Two
    // overlapping translucent surfaces aren't sorted against each other
    // this way, which can look slightly off at some angles, but that's the
    // same trade-off most simple voxel renderers make instead of full
    // per-triangle transparency sorting. Backface culling is off for this
    // pass specifically: a water top face's winding only faces up, so
    // without this, looking at it from *underneath* (submerged, looking up
    // toward the surface) would cull it away entirely and let the raw sky
    // show through unobstructed and unfogged — breaking the enclosed,
    // foggy underwater look this is all for in the first place.
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    set_chunk_water_pass(true);
    for (const Chunk* chunk : visible) {
        chunk->draw_water();
    }
    set_chunk_water_pass(false);
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}

void World::draw_chunk_borders() const
{
    for (const auto& [key, chunk] : chunks) {
        Vector3 min_corner = chunk->get_position();
        Vector3 center = {
            min_corner.x + CHUNK_SIZE / 2.0f,
            min_corner.y + CHUNK_HEIGHT / 2.0f,
            min_corner.z + CHUNK_SIZE / 2.0f,
        };
        DrawCubeWires(center, static_cast<float>(CHUNK_SIZE), static_cast<float>(CHUNK_HEIGHT), static_cast<float>(CHUNK_SIZE), CHUNK_BORDER_COLOR);
    }
}

BlockType World::get_block(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return BlockType::Air; // above/below the world

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return BlockType::Air;

    return chunk->get_block(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

int World::get_light(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return MAX_LIGHT; // above/below the world

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return MAX_LIGHT; // edge of the loaded world

    return chunk->get_light(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

World::ChunkCoordinates World::chunk_coordinates(int x, int z) const
{
    return {floor_div(x, CHUNK_SIZE), floor_div(z, CHUNK_SIZE)};
}

Biome World::get_biome(int x, int z) const
{
    return terrain_noise->biome(static_cast<float>(x), static_cast<float>(z));
}

std::optional<World::RaycastHit> World::raycast(Vector3 origin, Vector3 direction, float max_distance) const
{
    Vector3 dir = Vector3Normalize(direction);

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    int step_x = (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0);
    int step_y = (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0);
    int step_z = (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0);

    constexpr float INF = std::numeric_limits<float>::infinity();

    // Distance (in units of `direction`'s length, i.e. world units since
    // `dir` is normalized) along the ray to the next boundary crossing on
    // each axis, and how much that distance grows every time this axis
    // crosses one more voxel boundary.
    auto next_boundary_t = [](float origin_coord, int voxel, int step, float dir_component) {
        if (step == 0) return INF;
        float boundary = static_cast<float>(step > 0 ? voxel + 1 : voxel);
        return (boundary - origin_coord) / dir_component;
    };
    auto boundary_t_step = [](int step, float dir_component) {
        return (step == 0) ? INF : std::fabs(1.0f / dir_component);
    };

    float t_max_x = next_boundary_t(origin.x, x, step_x, dir.x);
    float t_max_y = next_boundary_t(origin.y, y, step_y, dir.y);
    float t_max_z = next_boundary_t(origin.z, z, step_z, dir.z);

    float t_delta_x = boundary_t_step(step_x, dir.x);
    float t_delta_y = boundary_t_step(step_y, dir.y);
    float t_delta_z = boundary_t_step(step_z, dir.z);

    // Outward normal of the face the ray most recently entered the current
    // voxel through; {0,0,0} for the starting voxel (there's no "entry
    // face" if the ray already starts inside a solid block).
    Vector3 normal = {0.0f, 0.0f, 0.0f};
    float traveled = 0.0f;

    while (traveled <= max_distance) {
        if (get_block_properties(get_block(x, y, z)).solid) {
            return RaycastHit{x, y, z, normal};
        }

        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            x += step_x;
            traveled = t_max_x;
            t_max_x += t_delta_x;
            normal = {static_cast<float>(-step_x), 0.0f, 0.0f};
        } else if (t_max_y < t_max_z) {
            y += step_y;
            traveled = t_max_y;
            t_max_y += t_delta_y;
            normal = {0.0f, static_cast<float>(-step_y), 0.0f};
        } else {
            z += step_z;
            traveled = t_max_z;
            t_max_z += t_delta_z;
            normal = {0.0f, 0.0f, static_cast<float>(-step_z)};
        }
    }

    return std::nullopt;
}

void World::break_block(int x, int y, int z)
{
    if (!get_block_properties(get_block(x, y, z)).solid) return; // nothing there to break
    set_block_and_rebuild(x, y, z, BlockType::Air);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y + 1, z);
}

void World::place_block(int x, int y, int z, BlockType type)
{
    if (get_block_properties(get_block(x, y, z)).solid) return; // something's already there
    set_block_and_rebuild(x, y, z, type);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y, z);
}

void World::set_block_and_rebuild(int x, int y, int z, BlockType type)
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return;

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    int local_x = x - chunk_x * CHUNK_SIZE;
    int local_z = z - chunk_z * CHUNK_SIZE;
    chunk->set_block(local_x, y - MIN_WORLD_Y, local_z, type);
    chunk->mark_modified();
    chunk->compute_lighting();

    rebuild_mesh_neighborhood(chunk_x, chunk_z);
}

namespace {
    constexpr int FLUID_NEIGHBOR_OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
}

std::optional<uint8_t> World::compute_fluid_level(int x, int y, int z) const
{
    // Water directly above always feeds this cell, regardless of its own
    // level — a falling column doesn't care how far *that* water is from
    // its own source, only that it's there.
    if (get_block(x, y + 1, z) == BlockType::Water) {
        return FLUID_LEVEL_FALLING;
    }

    int best = 255;
    for (const auto& offset : FLUID_NEIGHBOR_OFFSETS) {
        int nx = x + offset[0];
        int nz = z + offset[1];
        if (get_block(nx, y, nz) != BlockType::Water) continue;

        int chunk_x = floor_div(nx, CHUNK_SIZE);
        int chunk_z = floor_div(nz, CHUNK_SIZE);
        const Chunk* chunk = chunk_at(chunk_x, chunk_z);
        uint8_t neighbor_level = chunk->get_fluid_level(nx - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, nz - chunk_z * CHUNK_SIZE);
        // A SOURCE or FALLING neighbor is as good as being right next to
        // the source itself for spread-distance purposes — only a
        // FLOWING neighbor's own distance actually costs anything extra.
        int effective = (neighbor_level == FLUID_LEVEL_SOURCE || neighbor_level == FLUID_LEVEL_FALLING) ? 0 : neighbor_level;
        best = std::min(best, effective + 1);
    }

    if (best <= FLUID_LEVEL_MAX_FLOW) return static_cast<uint8_t>(best);
    return std::nullopt; // nothing feeds this cell -- it should be dry
}

void World::schedule_fluid_update(int x, int y, int z)
{
    int64_t key = fluid_key(x, y, z);
    if (!scheduled_fluid_cells.insert(key).second) return; // already pending
    pending_fluid_updates.push_back({x, y, z, fluid_tick + FLUID_TICK_DELAY});
}

void World::schedule_fluid_neighbors(int x, int y, int z)
{
    schedule_fluid_update(x, y, z);
    schedule_fluid_update(x, y - 1, z);
    schedule_fluid_update(x, y + 1, z);
    for (const auto& offset : FLUID_NEIGHBOR_OFFSETS) {
        schedule_fluid_update(x + offset[0], y, z + offset[1]);
    }
}

void World::update_fluids()
{
    ++fluid_tick;
    if (pending_fluid_updates.empty()) return;

    // Same batching idea as update_chunk_states(): a chunk relit/remeshed
    // once per unique chunk this tick's updates actually touched, not once
    // per individual block change — a flood filling a dozen cells in the
    // same chunk shouldn't relight or remesh it a dozen times over.
    std::unordered_set<int64_t> relit_chunks;
    std::unordered_set<int64_t> needs_mesh;
    auto mark_dirty = [&needs_mesh](int chunk_x, int chunk_z) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                needs_mesh.insert(chunk_key(chunk_x + dx, chunk_z + dz));
            }
        }
    };

    // Entries not yet due (due_tick still in the future) go back for a
    // later call; the rest are popped off the front and, if still due
    // after MAX_FLUID_UPDATES_PER_TICK of them have been resolved this
    // call, left for the very next one instead (their due_tick already
    // <= fluid_tick, so update_fluids() picks them straight back up).
    std::deque<PendingFluidUpdate> still_pending;
    int processed = 0;
    while (!pending_fluid_updates.empty()) {
        PendingFluidUpdate update = pending_fluid_updates.front();
        pending_fluid_updates.pop_front();

        if (update.due_tick > fluid_tick || processed >= MAX_FLUID_UPDATES_PER_TICK) {
            still_pending.push_back(update);
            continue;
        }
        ++processed;
        scheduled_fluid_cells.erase(fluid_key(update.x, update.y, update.z));

        BlockType current = get_block(update.x, update.y, update.z);
        if (current != BlockType::Water && current != BlockType::Air) continue; // solid now -- not our concern

        int chunk_x = floor_div(update.x, CHUNK_SIZE);
        int chunk_z = floor_div(update.z, CHUNK_SIZE);
        Chunk* chunk = chunk_at(chunk_x, chunk_z);
        if (chunk == nullptr) continue; // unloaded since this was scheduled

        int local_x = update.x - chunk_x * CHUNK_SIZE;
        int local_y = update.y - MIN_WORLD_Y;
        int local_z = update.z - chunk_z * CHUNK_SIZE;

        // A SOURCE never changes -- it's the one level compute_fluid_level()
        // is never asked to re-derive.
        if (current == BlockType::Water && chunk->get_fluid_level(local_x, local_y, local_z) == FLUID_LEVEL_SOURCE) {
            continue;
        }

        std::optional<uint8_t> new_level = compute_fluid_level(update.x, update.y, update.z);
        bool changed = false;
        if (new_level.has_value()) {
            if (current != BlockType::Water || chunk->get_fluid_level(local_x, local_y, local_z) != *new_level) {
                chunk->set_block(local_x, local_y, local_z, BlockType::Water);
                chunk->set_fluid_level(local_x, local_y, local_z, *new_level);
                changed = true;
            }
        } else if (current == BlockType::Water) {
            // Nothing feeds this FLOWING/FALLING cell any more -- dry up.
            chunk->set_block(local_x, local_y, local_z, BlockType::Air);
            changed = true;
        }

        if (changed) {
            chunk->mark_modified();
            relit_chunks.insert(chunk_key(chunk_x, chunk_z));
            mark_dirty(chunk_x, chunk_z);
            schedule_fluid_neighbors(update.x, update.y, update.z);
        }
    }
    pending_fluid_updates = std::move(still_pending);

    for (int64_t key : relit_chunks) {
        auto [cx, cz] = unpack_chunk_key(key);
        Chunk* chunk = chunk_at(cx, cz);
        if (chunk != nullptr) chunk->compute_lighting();
    }
    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        rebuild_mesh(cx, cz);
    }
}

void World::schedule_falling_check(int x, int y, int z)
{
    BlockType type = get_block(x, y, z);
    if (type != BlockType::Sand && type != BlockType::Gravel) return;

    int64_t key = fluid_key(x, y, z); // same generic (x, y, z) packing the fluid system already uses
    if (!scheduled_falling_cells.insert(key).second) return; // already pending
    pending_falling_blocks.push_back({x, y, z});
}

void World::update_falling_blocks()
{
    if (pending_falling_blocks.empty()) return;

    std::unordered_set<int64_t> relit_chunks;
    std::unordered_set<int64_t> needs_mesh;
    auto mark_dirty = [&needs_mesh](int chunk_x, int chunk_z) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                needs_mesh.insert(chunk_key(chunk_x + dx, chunk_z + dz));
            }
        }
    };

    // Only entries already queued *before* this call started get resolved
    // this tick (hence a fixed iteration count taken up front, not a
    // while-loop draining the deque) — schedule_falling_check() below,
    // called on a block right after it falls, queues its new position for
    // the *next* update_falling_blocks() call, not this one. Without that
    // distinction, a block would keep re-entering this same pass and fall
    // its entire distance in a single tick instead of one cell at a time.
    int initial_count = std::min(static_cast<int>(pending_falling_blocks.size()), MAX_FALLING_UPDATES_PER_TICK);
    for (int i = 0; i < initial_count; ++i) {
        std::array<int, 3> cell = pending_falling_blocks.front();
        pending_falling_blocks.pop_front();

        int x = cell[0], y = cell[1], z = cell[2];
        scheduled_falling_cells.erase(fluid_key(x, y, z));

        BlockType type = get_block(x, y, z);
        if (type != BlockType::Sand && type != BlockType::Gravel) continue; // no longer relevant
        if (y - 1 < MIN_WORLD_Y) continue; // already resting on the world floor (shouldn't happen given bedrock, but be safe)
        if (get_block_properties(get_block(x, y - 1, z)).solid) continue; // already supported

        int chunk_x = floor_div(x, CHUNK_SIZE);
        int chunk_z = floor_div(z, CHUNK_SIZE);
        Chunk* chunk = chunk_at(chunk_x, chunk_z);
        if (chunk == nullptr) continue; // unloaded since this was scheduled

        int local_x = x - chunk_x * CHUNK_SIZE;
        int local_z = z - chunk_z * CHUNK_SIZE;
        // Falls straight down within the same chunk column — no vertical
        // chunk stacking, so both cells always share one chunk.
        chunk->set_block(local_x, y - MIN_WORLD_Y, local_z, BlockType::Air);
        chunk->set_block(local_x, (y - 1) - MIN_WORLD_Y, local_z, type);
        chunk->mark_modified();
        relit_chunks.insert(chunk_key(chunk_x, chunk_z));
        mark_dirty(chunk_x, chunk_z);

        // Whatever water this displaced (or is now newly adjacent to the
        // cell it vacated) should react — same as real Minecraft, sand/
        // gravel isn't stopped by water, it falls through and replaces it.
        schedule_fluid_neighbors(x, y, z);
        schedule_fluid_neighbors(x, y - 1, z);

        // Keep falling next tick if still unsupported, and let whatever
        // was resting on top of this block — if it's also Sand or Gravel —
        // know it may have just lost its own support in turn.
        schedule_falling_check(x, y - 1, z);
        schedule_falling_check(x, y + 1, z);
    }

    for (int64_t key : relit_chunks) {
        auto [cx, cz] = unpack_chunk_key(key);
        Chunk* chunk = chunk_at(cx, cz);
        if (chunk != nullptr) chunk->compute_lighting();
    }
    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        rebuild_mesh(cx, cz);
    }
}

std::optional<int> World::water_depth_at(Vector3 position) const
{
    int x = static_cast<int>(std::floor(position.x));
    int y = static_cast<int>(std::floor(position.y));
    int z = static_cast<int>(std::floor(position.z));
    if (get_block(x, y, z) != BlockType::Water) return std::nullopt;

    int depth = 0;
    while (get_block(x, y + depth + 1, z) == BlockType::Water) ++depth;
    return depth;
}

void World::update_chunk_states(Vector3 observer_position)
{
    ChunkCoordinates observer_chunk = chunk_coordinates(
        static_cast<int>(std::floor(observer_position.x)),
        static_cast<int>(std::floor(observer_position.z)));

    // Nothing can have changed state since the last call if the observer is
    // still in the same chunk it was in then.
    if (last_observer_chunk && last_observer_chunk->x == observer_chunk.x && last_observer_chunk->z == observer_chunk.z) {
        return;
    }
    last_observer_chunk = observer_chunk;

    // Every chunk a generate/unload this call touches needs its mesh (and
    // its neighbors', per rebuild_mesh_neighborhood's reasoning) rebuilt —
    // collected here instead of meshing immediately inside generate_chunk/
    // unload_chunk, and only actually rebuilt once each in a final pass
    // below. The set dedups: a border chunk shared by several newly-loaded
    // (or unloaded) neighbors would otherwise get remeshed once per
    // neighbor instead of once, total — measured at 1345 rebuilds for 289
    // chunks' worth of initial world generation before this batching, ~4.6x
    // more than the 289 actually needed.
    std::unordered_set<int64_t> needs_mesh;
    auto mark_dirty = [&needs_mesh](int chunk_x, int chunk_z) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                needs_mesh.insert(chunk_key(chunk_x + dx, chunk_z + dz));
            }
        }
    };

    // Bring every chunk within LOADED_RADIUS up to its correct state:
    // generate whatever isn't loaded yet, then set the state that was
    // actually asked for either way — for an already-loaded chunk that's
    // just the Active/Loaded tick flag, no generation involved.
    int min_x = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.x - LOADED_RADIUS);
    int max_x = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.x + LOADED_RADIUS);
    int min_z = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.z - LOADED_RADIUS);
    int max_z = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.z + LOADED_RADIUS);

    for (int cx = min_x; cx <= max_x; ++cx) {
        for (int cz = min_z; cz <= max_z; ++cz) {
            Chunk* chunk = chunk_at(cx, cz);
            if (chunk == nullptr) {
                generate_chunk(cx, cz);
                chunk = chunk_at(cx, cz);
                mark_dirty(cx, cz);
            }
            chunk->set_state(desired_state_for(cx, cz, observer_chunk));
        }
    }

    // Unload anything still resident that fell outside LOADED_RADIUS.
    // Collected first since unload_chunk() erases from `chunks` — iterating
    // and erasing from the same map at once needs more care than this is
    // worth for a scan that only runs when the observer changes chunks.
    std::vector<std::pair<int, int>> out_of_range;
    for (const auto& [key, chunk] : chunks) {
        auto [cx, cz] = unpack_chunk_key(key);
        if (chebyshev_distance(cx, cz, observer_chunk.x, observer_chunk.z) > LOADED_RADIUS) {
            out_of_range.emplace_back(cx, cz);
        }
    }
    for (auto [cx, cz] : out_of_range) {
        unload_chunk(cx, cz);
        mark_dirty(cx, cz);
    }

    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        rebuild_mesh(cx, cz);
    }
}

Vector3 World::find_spawn_position()
{
    // Cheap first (just a noise sample, no chunk needed): reject Sea/Ocean
    // outright before ever generating anything for this candidate. Land
    // covers roughly half the world, so this alone already succeeds on the
    // very first or second ring tried in practice.
    auto is_land = [this](int x, int z) {
        Biome biome = get_biome(x, z);
        return biome != Biome::Sea && biome != Biome::Ocean;
    };

    // Only once a candidate passes that check do we pay for generating its
    // area, so this can actually confirm real, clear ground to stand on —
    // not just "probably land" — before accepting it.
    auto try_candidate = [this](int x, int z) -> std::optional<Vector3> {
        update_chunk_states({static_cast<float>(x), 0.0f, static_cast<float>(z)});

        for (int y = MIN_WORLD_Y + CHUNK_HEIGHT - 2; y >= MIN_WORLD_Y; --y) {
            if (!get_block_properties(get_block(x, y, z)).solid) continue;
            // Found the ground. Only actually a valid spawn if there's
            // room to stand in above it — a beach column can dip just
            // under a nearby Sea's water level despite reading as "land"
            // by biome alone, and this rejects appearing submerged there.
            if (get_block(x, y + 1, z) == BlockType::Air && get_block(x, y + 2, z) == BlockType::Air) {
                return Vector3{x + 0.5f, static_cast<float>(y + 1), z + 0.5f};
            }
            return std::nullopt;
        }
        return std::nullopt; // no solid ground found in this column at all
    };

    constexpr int SEARCH_STEP = 8;
    constexpr int MAX_SEARCH_RADIUS = 512;
    if (is_land(0, 0)) {
        if (auto spawn = try_candidate(0, 0)) return *spawn;
    }
    for (int radius = SEARCH_STEP; radius <= MAX_SEARCH_RADIUS; radius += SEARCH_STEP) {
        for (int x = -radius; x <= radius; x += SEARCH_STEP) {
            for (int z = -radius; z <= radius; z += SEARCH_STEP) {
                if (std::max(std::abs(x), std::abs(z)) != radius) continue; // this ring's perimeter only
                if (!is_land(x, z)) continue;
                if (auto spawn = try_candidate(x, z)) return *spawn;
            }
        }
    }

    // Astronomically unlikely (would need no dry, clear land anywhere
    // within 512 blocks of the origin) but still a definite Vector3
    // rather than leaving the caller with nothing.
    return {0.5f, 100.0f, 0.5f};
}

ChunkState World::desired_state_for(int chunk_x, int chunk_z, ChunkCoordinates observer_chunk) const
{
    int distance = chebyshev_distance(chunk_x, chunk_z, observer_chunk.x, observer_chunk.z);
    if (distance <= ACTIVE_RADIUS) return ChunkState::Active;
    if (distance <= LOADED_RADIUS) return ChunkState::Loaded;
    return ChunkState::Unloaded; // never actually assigned to a Chunk — see update_chunk_states
}

void World::generate_chunk(int chunk_x, int chunk_z)
{
    Vector3 position = {
        chunk_x * static_cast<float>(CHUNK_SIZE),
        static_cast<float>(MIN_WORLD_Y), // a Chunk's own local Y 0 sits here in world space
        chunk_z * static_cast<float>(CHUNK_SIZE),
    };
    auto chunk = std::make_unique<Chunk>(position);

    // World data only — what a future server would own. No mesh built here:
    // update_chunk_states() batches meshing (this chunk's and any affected
    // neighbors') into one pass after every generate/unload this call needs
    // is done, instead of doing it immediately per chunk.
    chunk->generate_terrain(*terrain_noise);
    chunk->carve_caves(seed, chunk_x, chunk_z);
    chunk->compute_lighting();
    chunks.emplace(chunk_key(chunk_x, chunk_z), std::move(chunk));
}

void World::unload_chunk(int chunk_x, int chunk_z)
{
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    // TODO: if chunk->is_modified(), persist its block/light data to disk
    // here before freeing it. Not implemented yet — a modified chunk's
    // edits are lost on unload, same as if they'd never happened.

    chunks.erase(chunk_key(chunk_x, chunk_z)); // ~Chunk() frees the GPU mesh too

    // No neighborhood remesh here — same batching reasoning as
    // generate_chunk(), handled by update_chunk_states().
}
