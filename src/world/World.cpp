#include "world/World.hpp"
#include "core/TerrainNoise.hpp"
#include "rendering/Skybox.hpp"
#include "rendering/BlockMesh.hpp"
#include "rendering/EntityLighting.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <queue>
#include <set>
#include <shared_mutex>
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
    // World::scheduled_fluid_cells - biased to non-negative first since a
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

    // Water flow (World::update_fluids): how many ticks after a cell is
    // scheduled before it's actually re-evaluated - the same idea as real
    // Minecraft's own liquid tick rate (5 game ticks in Java Edition), so
    // a flow visibly advances outward one step at a time instead of
    // instantly resolving the moment something changes.
    constexpr int FLUID_TICK_DELAY = 5;

    // Caps how many due fluid cells update_fluids() resolves in a single
    // tick - the same "spiral of death" caution MAX_TICKS_PER_FRAME uses
    // elsewhere (GameEngine.cpp), here against an enormous number of cells
    // all coming due on the same tick (e.g. draining a whole lake) turning
    // one tick into a multi-chunk remesh storm. Anything past this limit
    // is simply left due (its due_tick already <= fluid_tick) and picked
    // up first thing on the very next call instead of being delayed
    // further or dropped.
    constexpr int MAX_FLUID_UPDATES_PER_TICK = 64;

    // Same "spiral of death" caution as MAX_FLUID_UPDATES_PER_TICK, for
    // Sand/Gravel gravity (World::update_falling_blocks) - against, say, a
    // huge floating platform losing its support all at once.
    constexpr int MAX_FALLING_UPDATES_PER_TICK = 128;

    // Real Minecraft's own per-tick FallingBlockEntity numbers (blocks/
    // tick, multiplicative drag) - see http://minecraft.wiki/w/Falling_Block:
    // gravity 0.04, vertical drag 0.98, and explicitly *not* slowed by
    // water or lava (unlike every other entity, including dropped items -
    // see DroppedItem.cpp's own water handling).
    constexpr float FALLING_BLOCK_GRAVITY_PER_TICK = 0.04f;
    constexpr float FALLING_BLOCK_DRAG_VERTICAL = 0.98f;

    // World::integrate_worker_results()'s own per-frame budget - the actual
    // mechanism that keeps a large background backlog (e.g. after a stall,
    // or the player teleporting/moving very fast) from spiking a single
    // frame's cost, now that generation/meshing themselves run on
    // background threads instead of inline. Gen results are cheap to
    // integrate (just a map insert - no GL call), so that budget is
    // generous; mesh results each cost one real GPU upload
    // (Chunk::upload_mesh_data()), the actual limiting factor, so that
    // budget is deliberately small. Both are tunable without touching
    // anything else - see integrate_worker_results()'s own comment.
    constexpr size_t MAX_GEN_INTEGRATIONS_PER_FRAME = 2;
    constexpr size_t MAX_MESH_INTEGRATIONS_PER_FRAME = 2;
    constexpr size_t MAX_GEN_DISPATCHES_PER_UPDATE = 4;

    // Safety ceiling on the user-configured fog distance (Settings), not
    // its primary source (see World::draw): fog fully hides everything by
    // FOG_END_FRACTION of config.loaded_radius_chunks's own distance at
    // the very latest, so a chunk unloading at the render-distance edge
    // does so already inside the fog, never visibly, no matter how far the
    // player pushed the fog slider. Close to 1.0 rather than a generous
    // margin below it - the margin's own job is only to absorb a chunk
    // moving from "loaded" to "unloaded" between frames, not to leave a
    // stretch of clear, unfogged terrain sitting right in front of the
    // pop; too big a margin (previously 0.8) recreates exactly the hard
    // edge fog exists to hide. FOG_START_FRACTION is relative to whichever
    // end distance actually applies (the configured one, or this ceiling
    // if that's nearer) - lower means fog starts closer to the camera and
    // thickens over more of the visible distance instead of staying thin
    // until close to the end.
    constexpr float FOG_END_FRACTION = 0.95f;
    constexpr float FOG_START_FRACTION = 0.35f;

    // Underwater fog: real Minecraft doesn't darken the water block's own
    // surface color by depth (it stays one plain color everywhere) -
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

    // Chunks within this many blocks of the camera are always drawn - the
    // view-cone test below approximates visibility by angle alone, which
    // breaks down at very close range (a chunk right next to the camera can
    // legitimately be visible well outside a "reasonable" cone), so it
    // doesn't get applied there at all.
    constexpr float ALWAYS_VISIBLE_BLOCKS = 3.0f * CHUNK_SIZE;

    // cos(70 degrees). An approximate view-cone test, not exact frustum
    // culling: exact culling needs frustum planes extracted from the
    // camera's view-projection matrix, which depends on getting raylib's
    // exact matrix convention (row- vs column-vector) right - a mismatch
    // there fails silently as chunks incorrectly popping out of view, which
    // is a much worse bug than under-culling. 70 degrees is a deliberately
    // generous margin over the actual worst case at the default 1280x720
    // window (fovy 60 => ~46 degree half-FOV horizontally, ~50 degrees to
    // the frustum's own corner) - it still culls whatever's clearly behind
    // or well to the side of the camera, just not as tightly as the exact
    // frustum would.
    constexpr float VIEW_CONE_COS = 0.342f;

    // Approximates whether a chunk (by its column footprint in the X/Z
    // plane - chunks span the whole world height, so Y never narrows this)
    // is worth drawing from the camera's position/facing. Never a false
    // negative by a wide margin (see VIEW_CONE_COS) - the goal is skipping
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

    // The world has no fixed size - any chunk within this many blocks of
    // the origin can be generated on demand (World::chunk_at), same idea as
    // Minecraft's own world border: technically a limit, practically never
    // reached by walking. It's nowhere near Minecraft's actual 29,999,984,
    // deliberately: this project stores every position in a 32-bit float
    // (raylib's Vector3), and float can only represent every integer
    // exactly up to 2^24 (16,777,216) - past that, block positions start
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

    void offset_shape(BlockShapeBoxes& shape, int x, int y, int z)
    {
        Vector3 offset{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        for (int i = 0; i < shape.count; ++i) {
            shape.boxes[i].min = Vector3Add(shape.boxes[i].min, offset);
            shape.boxes[i].max = Vector3Add(shape.boxes[i].max, offset);
        }
    }

    std::optional<World::RaycastHit> ray_box_hit(Vector3 origin, Vector3 dir,
                                                 const BoundingBox& box, int x, int y, int z,
                                                 float max_distance)
    {
        constexpr float EPS = 0.00001f;
        float t_min = 0.0f;
        float t_max = max_distance;
        Vector3 normal{0.0f, 0.0f, 0.0f};

        auto axis = [&](float origin_coord, float dir_coord, float min_coord, float max_coord,
                        Vector3 min_normal, Vector3 max_normal) {
            if (std::fabs(dir_coord) < EPS) {
                return origin_coord >= min_coord - EPS && origin_coord <= max_coord + EPS;
            }
            float inv = 1.0f / dir_coord;
            float t1 = (min_coord - origin_coord) * inv;
            float t2 = (max_coord - origin_coord) * inv;
            Vector3 n1 = min_normal;
            Vector3 n2 = max_normal;
            if (t1 > t2) {
                std::swap(t1, t2);
                std::swap(n1, n2);
            }
            if (t1 > t_min) {
                t_min = t1;
                normal = n1;
            }
            t_max = std::min(t_max, t2);
            return t_min <= t_max + EPS;
        };

        if (!axis(origin.x, dir.x, box.min.x, box.max.x, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}) ||
            !axis(origin.y, dir.y, box.min.y, box.max.y, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}) ||
            !axis(origin.z, dir.z, box.min.z, box.max.z, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f})) {
            return std::nullopt;
        }
        if (t_max < 0.0f || t_min > max_distance) return std::nullopt;

        float distance = std::max(0.0f, t_min);
        return World::RaycastHit{x, y, z, normal, distance, Vector3Add(origin, Vector3Scale(dir, distance))};
    }

    int chebyshev_distance(int ax, int az, int bx, int bz) {
        return std::max(std::abs(ax - bx), std::abs(az - bz));
    }

    // draw_chunk_borders(): magenta doesn't occur naturally in terrain, so
    // it reads clearly as a debug overlay against any biome.
    constexpr Color CHUNK_BORDER_COLOR = {255, 0, 255, 255};

    bool has_centered_top_support(const World& world, int x, int y, int z)
    {
        constexpr float SUPPORT_EPSILON = 0.001f;
        const float support_y = static_cast<float>(y) + 1.0f;
        const float center_x = static_cast<float>(x) + 0.5f;
        const float center_z = static_cast<float>(z) + 0.5f;
        BlockShapeBoxes boxes = world.collision_boxes_at(x, y, z);
        for (int i = 0; i < boxes.count; ++i) {
            const BoundingBox& box = boxes.boxes[i];
            if (std::fabs(box.max.y - support_y) > SUPPORT_EPSILON) continue;
            if (center_x + SUPPORT_EPSILON < box.min.x || center_x - SUPPORT_EPSILON > box.max.x) continue;
            if (center_z + SUPPORT_EPSILON < box.min.z || center_z - SUPPORT_EPSILON > box.max.z) continue;
            return true;
        }
        return false;
    }

    bool has_centered_side_support(const World& world, int x, int y, int z, HorizontalDirection side)
    {
        constexpr float SUPPORT_EPSILON = 0.001f;
        DirectionOffset step = horizontal_direction_offset(side);
        const float plane_x = step.dx > 0 ? static_cast<float>(x + 1) : static_cast<float>(x);
        const float plane_z = step.dz > 0 ? static_cast<float>(z + 1) : static_cast<float>(z);
        const float center_y = static_cast<float>(y) + 0.5f;
        const float center_x = static_cast<float>(x) + 0.5f;
        const float center_z = static_cast<float>(z) + 0.5f;
        BlockShapeBoxes boxes = world.collision_boxes_at(x, y, z);
        for (int i = 0; i < boxes.count; ++i) {
            const BoundingBox& box = boxes.boxes[i];
            if (center_y + SUPPORT_EPSILON < box.min.y || center_y - SUPPORT_EPSILON > box.max.y) continue;
            if (step.dx != 0) {
                float face_x = step.dx > 0 ? box.max.x : box.min.x;
                if (std::fabs(face_x - plane_x) > SUPPORT_EPSILON) continue;
                if (center_z + SUPPORT_EPSILON < box.min.z || center_z - SUPPORT_EPSILON > box.max.z) continue;
                return true;
            }
            float face_z = step.dz > 0 ? box.max.z : box.min.z;
            if (std::fabs(face_z - plane_z) > SUPPORT_EPSILON) continue;
            if (center_x + SUPPORT_EPSILON < box.min.x || center_x - SUPPORT_EPSILON > box.max.x) continue;
            return true;
        }
        return false;
    }

    bool is_torch_block(BlockType type)
    {
        return type == BlockType::Torch || type == BlockType::RedstoneTorch || type == BlockType::LitRedstoneTorch;
    }

    std::optional<HorizontalDirection> side_from_normal(Vector3 normal)
    {
        if (normal.x > 0.5f) return HorizontalDirection::East;
        if (normal.x < -0.5f) return HorizontalDirection::West;
        if (normal.z > 0.5f) return HorizontalDirection::South;
        if (normal.z < -0.5f) return HorizontalDirection::North;
        return std::nullopt;
    }
}

World::World(WorldConfig config)
    : config(std::move(config))
    , terrain_noise(std::make_unique<TerrainNoise>(this->config.seed))
    , worker_pool(std::make_unique<ChunkWorkerPool>(this->config.seed, this->config.save_directory))
{
    // Active must never exceed Loaded - desired_state_for() assumes this
    // ordering, and Settings only actually exposes render distance to the
    // player (active/simulation distance stays fixed - see WorldConfig).
    this->config.active_radius_chunks = std::min(this->config.active_radius_chunks, this->config.loaded_radius_chunks);

    if (this->config.save_directory) {
        std::error_code ec;
        std::filesystem::create_directories(*this->config.save_directory + "/chunks", ec);
    }

    // Nothing is loaded yet - the first update_chunk_states() call (see
    // GameEngine::set_world()/tick()) populates the world around wherever
    // the observer actually starts.
}

World::~World()
{
    // Must run before anything below touches a Chunk: joins every
    // background worker thread, so by the time this returns, nothing can
    // still be mid-read of a chunk's data (Chunk::data_mutex()) or holding
    // a shared_ptr reference to one that a worker thread might otherwise go
    // on to drop (and so destroy - see ChunkWorkerPool's own comment) at
    // some point after this destructor has already returned. Idempotent -
    // ~ChunkWorkerPool() calls it again below (as part of worker_pool's own
    // automatic destruction), harmlessly.
    worker_pool->shutdown();

    // Anything still mid-fall (falling_blocks) exists only as this
    // transient list, not in any chunk's own data - saving/closing right
    // now would otherwise just lose it. Snap each one back into the grid
    // wherever it currently is, as if it landed there this instant, so the
    // save loop below picks it up like any other block. No relight/remesh
    // needed - the game isn't rendering again before it exits.
    for (const FallingBlock& entity : falling_blocks) {
        int grid_x = static_cast<int>(std::floor(entity.motion.current.x));
        int grid_z = static_cast<int>(std::floor(entity.motion.current.z));
        int grid_y = std::clamp(static_cast<int>(std::floor(entity.motion.current.y - 0.5f)),
                                 MIN_WORLD_Y, MIN_WORLD_Y + CHUNK_HEIGHT - 1);
        int chunk_x = floor_div(grid_x, CHUNK_SIZE);
        int chunk_z = floor_div(grid_z, CHUNK_SIZE);
        Chunk* chunk = chunk_at(chunk_x, chunk_z);
        if (chunk == nullptr) continue; // its own chunk already unloaded - nothing to write back into
        int local_x = grid_x - chunk_x * CHUNK_SIZE;
        int local_z = grid_z - chunk_z * CHUNK_SIZE;
        if (chunk->get_block(local_x, grid_y - MIN_WORLD_Y, local_z) == BlockType::Air) {
            chunk->set_block(local_x, grid_y - MIN_WORLD_Y, local_z, entity.type);
            chunk->mark_modified();
        }
    }

    // The only place a still-resident chunk's edits reach disk on the
    // "just quit the game" path - unload_chunk() covers a chunk that
    // streamed out while still playing, this covers whatever's left
    // loaded when the World itself goes away.
    if (config.save_directory) {
        for (const auto& [key, chunk] : chunks) {
            if (!chunk->is_modified()) continue;
            auto [cx, cz] = unpack_chunk_key(key);
            chunk->save_to_file(chunk_file_path(cx, cz));
        }
    }
}

std::string World::chunk_file_path(int chunk_x, int chunk_z) const
{
    return *config.save_directory + "/chunks/" + std::to_string(chunk_x) + "_" + std::to_string(chunk_z) + ".chunk";
}

const Chunk* World::chunk_at(int chunk_x, int chunk_z) const
{
    if (chunk_x < -WORLD_BORDER_CHUNKS || chunk_x >= WORLD_BORDER_CHUNKS ||
        chunk_z < -WORLD_BORDER_CHUNKS || chunk_z >= WORLD_BORDER_CHUNKS) {
        return nullptr;
    }
    auto it = chunks.find(chunk_key(chunk_x, chunk_z));
    return it != chunks.end() ? it->second.get() : nullptr;
}

// One real implementation (above) instead of two identical bodies - the
// standard way to share a const/non-const accessor pair (Meyers, Effective
// C++ Item 3).
Chunk* World::chunk_at(int chunk_x, int chunk_z)
{
    return const_cast<Chunk*>(std::as_const(*this).chunk_at(chunk_x, chunk_z));
}

std::shared_ptr<Chunk> World::chunk_shared_at(int chunk_x, int chunk_z) const
{
    auto it = chunks.find(chunk_key(chunk_x, chunk_z));
    return it != chunks.end() ? it->second : nullptr;
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
            request_remesh(chunk_x + dx, chunk_z + dz);
        }
    }
}

void World::relight_chunk_neighborhood(int chunk_x, int chunk_z)
{
    relight_chunks_around({{chunk_x, chunk_z}});
}

void World::relight_chunks_around(const std::vector<std::pair<int, int>>& centers)
{
    std::unordered_set<int64_t> area_keys;
    for (auto [chunk_x, chunk_z] : centers) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                int cx = chunk_x + dx;
                int cz = chunk_z + dz;
                if (chunk_at(cx, cz) != nullptr) {
                    area_keys.insert(chunk_key(cx, cz));
                }
            }
        }
    }
    if (area_keys.empty()) return;

    struct AreaChunk {
        int x, z;
        Chunk* chunk;
    };
    std::vector<AreaChunk> area;
    area.reserve(area_keys.size());
    for (int64_t key : area_keys) {
        auto [cx, cz] = unpack_chunk_key(key);
        if (Chunk* chunk = chunk_at(cx, cz)) {
            area.push_back({cx, cz, chunk});
        }
    }
    if (area.empty()) return;

    std::vector<Chunk*> lock_order;
    lock_order.reserve(area.size());
    for (const AreaChunk& entry : area) lock_order.push_back(entry.chunk);
    std::sort(lock_order.begin(), lock_order.end());
    lock_order.erase(std::unique(lock_order.begin(), lock_order.end()), lock_order.end());

    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(lock_order.size());
    for (Chunk* chunk : lock_order) locks.emplace_back(chunk->data_mutex());

    auto area_contains = [&](int chunk_x, int chunk_z) {
        return area_keys.count(chunk_key(chunk_x, chunk_z)) > 0;
    };
    auto resolve = [&](int x, int z, int& chunk_x, int& chunk_z, int& local_x, int& local_z) {
        chunk_x = floor_div(x, CHUNK_SIZE);
        chunk_z = floor_div(z, CHUNK_SIZE);
        local_x = x - chunk_x * CHUNK_SIZE;
        local_z = z - chunk_z * CHUNK_SIZE;
    };
    auto area_chunk_at_world = [&](int x, int z, int& local_x, int& local_z) -> Chunk* {
        int chunk_x, chunk_z;
        resolve(x, z, chunk_x, chunk_z, local_x, local_z);
        if (!area_contains(chunk_x, chunk_z)) return nullptr;
        return chunk_at(chunk_x, chunk_z);
    };
    auto transparent_at = [&](int x, int y, int z) {
        if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return false;
        return get_block_properties(get_block(x, y, z)).transparent;
    };
    auto set_sky = [&](int x, int y, int z, int value) {
        int local_x, local_z;
        Chunk* chunk = area_chunk_at_world(x, z, local_x, local_z);
        if (chunk == nullptr || y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return;
        chunk->set_sky_light(local_x, y - MIN_WORLD_Y, local_z, std::clamp(value, 0, MAX_LIGHT));
    };
    auto set_block = [&](int x, int y, int z, int value) {
        int local_x, local_z;
        Chunk* chunk = area_chunk_at_world(x, z, local_x, local_z);
        if (chunk == nullptr || y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return;
        chunk->set_block_light(local_x, y - MIN_WORLD_Y, local_z, std::clamp(value, 0, MAX_LIGHT));
    };

    for (const AreaChunk& entry : area) {
        entry.chunk->clear_lighting();
    }

    using Cell = std::array<int, 3>;
    std::queue<Cell> sky_queue;
    std::queue<Cell> block_queue;

    for (const AreaChunk& entry : area) {
        int base_x = entry.x * CHUNK_SIZE;
        int base_z = entry.z * CHUNK_SIZE;

        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                int wx = base_x + lx;
                int wz = base_z + lz;
                for (int ly = entry.chunk->highest_lit_y(); ly >= 0; --ly) {
                    if (!get_block_properties(entry.chunk->get_block(lx, ly, lz)).transparent) break;
                    entry.chunk->set_sky_light(lx, ly, lz, MAX_LIGHT);
                    sky_queue.push({wx, ly + MIN_WORLD_Y, wz});
                }
            }
        }

        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            for (int ly = 0; ly <= entry.chunk->highest_lit_y(); ++ly) {
                for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                    int luminance = get_block_properties(entry.chunk->get_block(lx, ly, lz)).luminance;
                    if (luminance <= 0) continue;
                    entry.chunk->set_block_light(lx, ly, lz, luminance);
                    block_queue.push({base_x + lx, ly + MIN_WORLD_Y, base_z + lz});
                }
            }
        }
    }

    constexpr int OFFSETS[6][3] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};

    auto seed_from_outside = [&](int inside_x, int y, int inside_z, int outside_x, int outside_z) {
        if (!transparent_at(inside_x, y, inside_z)) return;

        int sky_level = get_sky_light(outside_x, y, outside_z) - 1;
        if (sky_level > get_sky_light(inside_x, y, inside_z)) {
            set_sky(inside_x, y, inside_z, sky_level);
            if (sky_level > 0) sky_queue.push({inside_x, y, inside_z});
        }

        int block_level = get_block_light(outside_x, y, outside_z) - 1;
        if (block_level > get_block_light(inside_x, y, inside_z)) {
            set_block(inside_x, y, inside_z, block_level);
            if (block_level > 0) block_queue.push({inside_x, y, inside_z});
        }
    };

    for (const AreaChunk& entry : area) {
        int min_x = entry.x * CHUNK_SIZE;
        int max_x = min_x + CHUNK_SIZE - 1;
        int min_z = entry.z * CHUNK_SIZE;
        int max_z = min_z + CHUNK_SIZE - 1;

        for (int y = MIN_WORLD_Y; y < MIN_WORLD_Y + CHUNK_HEIGHT; ++y) {
            if (!area_contains(entry.x - 1, entry.z)) {
                for (int z = min_z; z <= max_z; ++z) seed_from_outside(min_x, y, z, min_x - 1, z);
            }
            if (!area_contains(entry.x + 1, entry.z)) {
                for (int z = min_z; z <= max_z; ++z) seed_from_outside(max_x, y, z, max_x + 1, z);
            }
            if (!area_contains(entry.x, entry.z - 1)) {
                for (int x = min_x; x <= max_x; ++x) seed_from_outside(x, y, min_z, x, min_z - 1);
            }
            if (!area_contains(entry.x, entry.z + 1)) {
                for (int x = min_x; x <= max_x; ++x) seed_from_outside(x, y, max_z, x, max_z + 1);
            }
        }
    }

    auto propagate = [&](std::queue<Cell>& queue, bool sky) {
        while (!queue.empty()) {
            Cell cell = queue.front();
            queue.pop();
            int level = sky ? get_sky_light(cell[0], cell[1], cell[2])
                            : get_block_light(cell[0], cell[1], cell[2]);
            int new_level = level - 1;
            if (new_level <= 0) continue;

            for (const auto& offset : OFFSETS) {
                int nx = cell[0] + offset[0];
                int ny = cell[1] + offset[1];
                int nz = cell[2] + offset[2];
                if (ny < MIN_WORLD_Y || ny >= MIN_WORLD_Y + CHUNK_HEIGHT) continue;

                int local_x, local_z;
                Chunk* chunk = area_chunk_at_world(nx, nz, local_x, local_z);
                if (chunk == nullptr) continue;
                int local_y = ny - MIN_WORLD_Y;
                if (!get_block_properties(chunk->get_block(local_x, local_y, local_z)).transparent) continue;

                int current = sky ? chunk->get_sky_light(local_x, local_y, local_z)
                                  : chunk->get_block_light(local_x, local_y, local_z);
                if (new_level <= current) continue;

                if (sky) {
                    chunk->set_sky_light(local_x, local_y, local_z, new_level);
                } else {
                    chunk->set_block_light(local_x, local_y, local_z, new_level);
                }
                queue.push({nx, ny, nz});
            }
        }
    };
    propagate(sky_queue, true);
    propagate(block_queue, false);
}

std::vector<const Chunk*> World::compute_visible_chunks(const Camera3D& camera) const
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    std::vector<const Chunk*> visible;
    for (const auto& [key, chunk] : chunks) {
        if (chunk_in_view(chunk->get_position(), camera.position, forward)) {
            visible.push_back(chunk.get());
        }
    }
    return visible;
}

void World::draw_opaque(const Camera3D& camera) const
{
    float fog_end, fog_start;
    Color fog_color, fog_sky_color;
    if (auto depth = water_depth_at(camera.position)) {
        // Submerged: swap in underwater fog (see UNDERWATER_FOG_* above)
        // instead of the normal render-distance one - real Minecraft's own
        // approach, rather than darkening the water block's own color.
        // No skybox visible underwater to blend toward, so both fog colors
        // are the same flat tone - the shader's horizon/sky mix is then a
        // no-op regardless of view angle.
        float t = std::clamp(static_cast<float>(*depth) / UNDERWATER_FOG_MAX_DEPTH, 0.0f, 1.0f);
        fog_end = UNDERWATER_FOG_END_SHALLOW + (UNDERWATER_FOG_END_DEEP - UNDERWATER_FOG_END_SHALLOW) * t;
        fog_start = fog_end * UNDERWATER_FOG_START_FRACTION;
        fog_color = ColorLerp(UNDERWATER_FOG_COLOR_SHALLOW, UNDERWATER_FOG_COLOR_DEEP, t);
        fog_sky_color = fog_color;
    } else {
        float max_fog_end = config.loaded_radius_chunks * CHUNK_SIZE * FOG_END_FRACTION;
        fog_end = std::min(static_cast<float>(config.fog_distance_blocks), max_fog_end);
        fog_start = fog_end * FOG_START_FRACTION;
        fog_color = skybox_horizon_color();
        fog_sky_color = skybox_sky_color();
    }
    set_chunk_fog(camera.position, fog_color, fog_sky_color, fog_start, fog_end);
    set_chunk_water_time(static_cast<float>(GetTime()));

    // Opaque and alpha-cutout geometry only, world-wide - see
    // draw_translucent() for the see-through layers (glass, ice, water),
    // deliberately a separate call now: alpha blending needs to composite
    // over a finished picture that already includes anything solid meant
    // to be seen *through* it (a dropped item sitting underwater, say),
    // not just the opaque terrain drawn here.
    set_chunk_water_pass(false);
    set_chunk_dynamic_entity_pass(false);
    for (const Chunk* chunk : compute_visible_chunks(camera)) {
        chunk->draw();
    }
}

void World::draw_translucent(const Camera3D& camera) const
{
    std::vector<const Chunk*> visible = compute_visible_chunks(camera);

    // Transparent/translucent geometry: alpha blended, and not depth-
    // *written* (only depth-*tested*, so solid terrain in front still
    // correctly hides it) - a transparent block's face writing to the
    // depth buffer as if it were solid would incorrectly occlude whatever
    // real geometry sits behind it, up to and including entire chunks
    // visible through a large window. With depth writes off, nothing here
    // is sorted against anything else by actual depth any more - it all
    // just composites in whatever order it's drawn in - so every
    // transparent layer from every visible chunk (see below) is instead
    // explicitly sorted by its own true 3D distance from the camera and
    // drawn farthest first, the same trade-off most simple voxel renderers
    // make instead of full per-triangle transparency sorting. Backface
    // culling stays *on* here (unlike depth writes) for every transparent
    // layer - an isolated glass block's far face (facing away from the
    // camera) would otherwise also render, right behind its near face,
    // showing the same glass texture twice in a row before whatever's
    // actually beyond it. Water is the one exception (see draw_water()
    // below): its own top face winding only faces up, so backface culling
    // would hide it entirely when viewed from *underneath* (submerged,
    // looking up toward the surface) - it's disabled just for that one
    // call instead.
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    set_chunk_dynamic_entity_pass(false);

    // Every see-through layer, from every visible chunk, in one single
    // flat list - every distinct transparent BlockType a chunk has
    // (Chunk::get_transparent_layer_avg_y()) plus its water
    // (get_water_avg_y()) - each with its own true 3D distance from the
    // camera. This has to be flat and sorted *once*, globally, rather
    // than sorting chunks by (horizontal-only) distance first and only
    // then sorting each chunk's own layers: two chunks close in the X/Z
    // plane can still hold layers at very different Y (glass in one
    // chunk, ice in the chunk right next to it, say), and a per-chunk
    // sort nested inside a coarser cross-chunk sort would never actually
    // compare those two layers' real distances against each other -
    // only within-chunk ties would come out right, cross-chunk ones
    // wouldn't.
    struct Layer { float distance_sq; const Chunk* chunk; bool is_water; size_t index; };
    std::vector<Layer> layers;
    for (const Chunk* chunk : visible) {
        Vector3 pos = chunk->get_position();
        float dx = (pos.x + CHUNK_SIZE / 2.0f) - camera.position.x;
        float dz = (pos.z + CHUNK_SIZE / 2.0f) - camera.position.z;
        float horizontal_dist_sq = dx * dx + dz * dz;

        for (size_t i = 0; i < chunk->transparent_layer_count(); ++i) {
            float dy = (pos.y + chunk->get_transparent_layer_avg_y(i)) - camera.position.y;
            layers.push_back({horizontal_dist_sq + dy * dy, chunk, false, i});
        }
        float water_dy = (pos.y + chunk->get_water_avg_y()) - camera.position.y;
        layers.push_back({horizontal_dist_sq + water_dy * water_dy, chunk, true, 0});
    }

    std::sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b) {
        return a.distance_sq > b.distance_sq; // farthest first
    });

    for (const Layer& layer : layers) {
        if (layer.is_water) {
            set_chunk_water_pass(true);
            rlDisableBackfaceCulling(); // see this block's own comment above - water-only
            layer.chunk->draw_water();
            rlEnableBackfaceCulling();
            set_chunk_water_pass(false);
        } else {
            layer.chunk->draw_transparent_layer(layer.index);
        }
    }

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

HorizontalDirection World::get_block_orientation(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return HorizontalDirection::South;

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return HorizontalDirection::South;

    return chunk->get_orientation(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

void World::set_block_orientation(int x, int y, int z, HorizontalDirection direction)
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return;

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    int local_x = x - chunk_x * CHUNK_SIZE;
    int local_z = z - chunk_z * CHUNK_SIZE;
    {
        // Same locking discipline as set_block_and_rebuild() - a
        // background mesh job could be reading this chunk's data right now.
        std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
        chunk->set_orientation(local_x, y - MIN_WORLD_Y, local_z, direction);
        chunk->mark_modified();
    }
    // Only this one chunk's own mesh can show the change (a directional
    // block's faces never cross a chunk border), so a neighborhood rebuild
    // like set_block_and_rebuild()'s own isn't needed - just this chunk.
    request_remesh(chunk_x, chunk_z);
}

uint16_t World::get_block_state(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return 0u;

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return 0u;

    return chunk->get_block_state(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

void World::set_block_state(int x, int y, int z, uint16_t packed)
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return;

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    int local_x = x - chunk_x * CHUNK_SIZE;
    int local_z = z - chunk_z * CHUNK_SIZE;
    {
        // Same locking discipline as set_block_orientation() - a background
        // mesh job could be reading this chunk's data right now.
        std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
        chunk->set_block_state(local_x, y - MIN_WORLD_Y, local_z, packed);
        chunk->mark_modified();
    }
    request_remesh(chunk_x, chunk_z);
}

BlockShapeBoxes World::collision_boxes_at(int x, int y, int z) const
{
    BlockType type = get_block(x, y, z);
    const BlockProperties& properties = get_block_properties(type);

    BlockShapeBoxes result;
    if (!properties.has_custom_shape) {
        // Fast path - the overwhelming majority of blocks: a plain full
        // unit cube if solid, nothing at all otherwise. No BlockShape
        // lookup, no orientation/block_state read.
        if (properties.solid) {
            result.boxes[0] = {
                {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                {static_cast<float>(x) + 1.0f, static_cast<float>(y) + 1.0f, static_cast<float>(z) + 1.0f},
            };
            result.count = 1;
        }
        return result;
    }

    BlockInstanceState state = unpack_block_state(get_block_orientation(x, y, z), get_block_state(x, y, z));

    result = get_block_shape(type, state);
    offset_shape(result, x, y, z);
    return result;
}

BlockShapeBoxes World::outline_boxes_at(int x, int y, int z) const
{
    BlockType type = get_block(x, y, z);
    const BlockProperties& properties = get_block_properties(type);
    if (!properties.selectable) return BlockShapeBoxes{};

    BlockInstanceState state;
    if (properties.has_custom_shape) {
        state = unpack_block_state(get_block_orientation(x, y, z), get_block_state(x, y, z));
    }

    BlockShapeBoxes result = get_outline_shape(type, state);
    offset_shape(result, x, y, z);
    return result;
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

int World::get_sky_light(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return MAX_LIGHT; // above/below the world

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return MAX_LIGHT; // edge of the loaded world

    return chunk->get_sky_light(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

int World::get_block_light(int x, int y, int z) const
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return 0; // above/below the world - no block light source out there

    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return 0; // edge of the loaded world

    return chunk->get_block_light(x - chunk_x * CHUNK_SIZE, y - MIN_WORLD_Y, z - chunk_z * CHUNK_SIZE);
}

int World::get_effective_light(int x, int y, int z, float sky_light_factor) const
{
    int block = get_block_light(x, y, z);
    int sky = static_cast<int>(std::round(get_sky_light(x, y, z) * sky_light_factor));
    return std::max(block, sky);
}

World::ChunkCoordinates World::chunk_coordinates(int x, int z) const
{
    return {floor_div(x, CHUNK_SIZE), floor_div(z, CHUNK_SIZE)};
}

Biome World::get_biome(int x, int z) const
{
    return terrain_noise->biome(static_cast<float>(x), static_cast<float>(z));
}

Color World::get_foliage_tint(int x, int z) const
{
    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) {
        return get_block_properties(BlockType::Foliage)
            .texture_tints[static_cast<int>(BlockFace::Top)];
    }
    return chunk->get_foliage_tint(
        x - chunk_x * CHUNK_SIZE, z - chunk_z * CHUNK_SIZE);
}

Color World::get_grass_tint(int x, int z) const
{
    int chunk_x = floor_div(x, CHUNK_SIZE);
    int chunk_z = floor_div(z, CHUNK_SIZE);
    const Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) {
        return get_block_properties(BlockType::ShortGrass)
            .texture_tints[static_cast<int>(BlockFace::Top)];
    }
    return chunk->get_grass_tint(
        x - chunk_x * CHUNK_SIZE, z - chunk_z * CHUNK_SIZE);
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

    float traveled = 0.0f;

    while (traveled <= max_distance) {
        BlockShapeBoxes outline = outline_boxes_at(x, y, z);
        std::optional<RaycastHit> nearest;
        for (int i = 0; i < outline.count; ++i) {
            auto hit = ray_box_hit(origin, dir, outline.boxes[i], x, y, z, max_distance);
            if (hit && (!nearest || hit->distance < nearest->distance)) {
                nearest = hit;
            }
        }
        if (nearest) {
            return nearest;
        }

        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            x += step_x;
            traveled = t_max_x;
            t_max_x += t_delta_x;
        } else if (t_max_y < t_max_z) {
            y += step_y;
            traveled = t_max_y;
            t_max_y += t_delta_y;
        } else {
            z += step_z;
            traveled = t_max_z;
            t_max_z += t_delta_z;
        }
    }

    return std::nullopt;
}

namespace {
    ChestPart chest_part_at(const World& world, int x, int y, int z) {
        uint16_t packed = world.get_block_state(x, y, z);
        return static_cast<ChestPart>((packed & BlockStateBits::MULTIBLOCK_PART_MASK) >> BlockStateBits::MULTIBLOCK_PART_SHIFT);
    }
}

std::optional<BlockType> World::break_block(int x, int y, int z)
{
    BlockType broken = get_block(x, y, z);
    if (broken == BlockType::Bedrock || !get_block_properties(broken).selectable) return std::nullopt;

    // Door/bed are two-cell objects - breaking either half silently
    // removes both (only the half the player actually targeted goes
    // through resolve_block_drops(), so exactly one drop happens either
    // way). A large chest's halves demote to independent single chests
    // instead - their own inventories are untouched, never merged/moved.
    // The partner is only cleared if it really is the matching half - never
    // whatever unrelated block happens to sit where a partner would be.
    auto clear_partner = [this](int px, int py, int pz, BlockType expected) {
        if (get_block(px, py, pz) == expected) set_block_and_rebuild(px, py, pz, BlockType::Air);
    };
    if (broken == BlockType::OakDoorLower) {
        clear_partner(x, y + 1, z, BlockType::OakDoorUpper);
    } else if (broken == BlockType::IronDoorLower) {
        clear_partner(x, y + 1, z, BlockType::IronDoorUpper);
    } else if (broken == BlockType::OakDoorUpper) {
        clear_partner(x, y - 1, z, BlockType::OakDoorLower);
    } else if (broken == BlockType::IronDoorUpper) {
        clear_partner(x, y - 1, z, BlockType::IronDoorLower);
    } else if (broken == BlockType::BedHead || broken == BlockType::BedFoot) {
        // Head sits one cell from the foot in the stored facing's direction
        // - see place_bed().
        DirectionOffset step = horizontal_direction_offset(get_block_orientation(x, y, z));
        if (broken == BlockType::BedFoot) clear_partner(x + step.dx, y, z + step.dz, BlockType::BedHead);
        else clear_partner(x - step.dx, y, z - step.dz, BlockType::BedFoot);
    } else if (broken == BlockType::Chest) {
        ChestPart part = chest_part_at(*this, x, y, z);
        if (part != ChestPart::Single) {
            DirectionOffset right_step = horizontal_direction_offset(horizontal_direction_right_of(get_block_orientation(x, y, z)));
            int sign = part == ChestPart::Primary ? 1 : -1;
            int partner_x = x + right_step.dx * sign;
            int partner_z = z + right_step.dz * sign;
            if (get_block(partner_x, y, partner_z) == BlockType::Chest) {
                set_block_state(partner_x, y, partner_z, 0);
            }
        }
    }

    set_block_and_rebuild(x, y, z, BlockType::Air);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y + 1, z);
    return broken;
}

bool World::place_block(int x, int y, int z, BlockType type)
{
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return false;
    if (chunk_at(floor_div(x, CHUNK_SIZE), floor_div(z, CHUNK_SIZE)) == nullptr) return false;
    if (type == BlockType::Air || !get_block_properties(get_block(x, y, z)).replaceable) return false;
    if (type == BlockType::ShortGrass && get_block(x, y - 1, z) != BlockType::Grass) return false;
    if (type == BlockType::OakSapling) {
        BlockType below = get_block(x, y - 1, z);
        if (below != BlockType::Grass && below != BlockType::Dirt) return false;
    }
    // Generic placement only handles floor-mounted torches; player
    // placement uses place_torch() so side-clicks can become wall torches.
    // Floor torches need an actual centered top face directly underneath.
    if ((type == BlockType::Torch || type == BlockType::RedstoneTorch || type == BlockType::LitRedstoneTorch) &&
        !has_centered_top_support(*this, x, y - 1, z)) {
        return false;
    }
    set_block_and_rebuild(x, y, z, type);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y, z);
    return true;
}

bool World::place_torch(int x, int y, int z, BlockType type, Vector3 hit_normal)
{
    if (!is_torch_block(type)) return false;
    if (y < MIN_WORLD_Y || y >= MIN_WORLD_Y + CHUNK_HEIGHT) return false;
    if (chunk_at(floor_div(x, CHUNK_SIZE), floor_div(z, CHUNK_SIZE)) == nullptr) return false;
    if (!get_block_properties(get_block(x, y, z)).replaceable) return false;

    if (std::optional<HorizontalDirection> side = side_from_normal(hit_normal)) {
        DirectionOffset step = horizontal_direction_offset(*side);
        if (!has_centered_side_support(*this, x - step.dx, y, z - step.dz, *side)) return false;

        set_block_and_rebuild(x, y, z, type);
        set_block_orientation(x, y, z, *side);
        set_block_state(x, y, z, BlockStateBits::TOP_HALF);
        schedule_fluid_neighbors(x, y, z);
        schedule_falling_check(x, y, z);
        return true;
    }

    if (hit_normal.y < 0.5f || !has_centered_top_support(*this, x, y - 1, z)) return false;
    set_block_and_rebuild(x, y, z, type);
    set_block_state(x, y, z, 0);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y, z);
    return true;
}

bool World::torch_has_support(int x, int y, int z) const
{
    BlockType type = get_block(x, y, z);
    if (!is_torch_block(type)) return true;

    uint16_t packed = get_block_state(x, y, z);
    if ((packed & BlockStateBits::TOP_HALF) == 0) {
        return has_centered_top_support(*this, x, y - 1, z);
    }

    HorizontalDirection side = get_block_orientation(x, y, z);
    DirectionOffset step = horizontal_direction_offset(side);
    return has_centered_side_support(*this, x - step.dx, y, z - step.dz, side);
}

bool World::combine_oak_slab(int x, int y, int z)
{
    if (get_block(x, y, z) != BlockType::OakSlab) return false;
    set_block_and_rebuild(x, y, z, BlockType::OakPlanks);
    schedule_fluid_neighbors(x, y, z);
    schedule_falling_check(x, y, z);
    return true;
}

bool World::place_door(int x, int y, int z, BlockType lower_type, HorizontalDirection facing)
{
    if (!get_block_properties(get_block(x, y - 1, z)).solid) return false;
    if (!place_block(x, y, z, lower_type)) return false;

    BlockType upper_type = lower_type == BlockType::IronDoorLower ? BlockType::IronDoorUpper : BlockType::OakDoorUpper;
    if (!place_block(x, y + 1, z, upper_type)) {
        break_block(x, y, z);
        return false;
    }
    set_block_orientation(x, y, z, facing);
    set_block_orientation(x, y + 1, z, facing);
    return true;
}

bool World::place_bed(int x, int y, int z, HorizontalDirection facing)
{
    DirectionOffset step = horizontal_direction_offset(facing);
    int head_x = x + step.dx;
    int head_z = z + step.dz;
    if (!place_block(x, y, z, BlockType::BedFoot)) return false;
    if (!place_block(head_x, y, head_z, BlockType::BedHead)) {
        break_block(x, y, z);
        return false;
    }
    set_block_orientation(x, y, z, facing);
    set_block_orientation(head_x, y, head_z, facing);
    return true;
}

bool World::place_chest(int x, int y, int z, HorizontalDirection facing)
{
    if (!place_block(x, y, z, BlockType::Chest)) return false;
    set_block_orientation(x, y, z, facing);

    // A double chest only ever pairs side by side - perpendicular to its
    // own facing, same as real Minecraft (two same-facing chests placed
    // front-to-back along the facing axis never merge). `sign` picks which
    // of the two perpendicular sides: +1 is the "right of facing" side
    // (this chest becomes Primary if it finds a partner there), -1 the
    // left (this chest becomes Secondary).
    DirectionOffset right_step = horizontal_direction_offset(horizontal_direction_right_of(facing));
    DirectionOffset facing_step = horizontal_direction_offset(facing);
    for (int sign : {1, -1}) {
        int nx = x + right_step.dx * sign;
        int nz = z + right_step.dz * sign;
        if (get_block(nx, y, nz) != BlockType::Chest) continue;
        if (get_block_orientation(nx, y, nz) != facing) continue;
        if (chest_part_at(*this, nx, y, nz) != ChestPart::Single) continue;

        // Diagonal-conflict check: the two cells directly in front of and
        // behind the neighbor (along the shared facing axis) - if either
        // already belongs to a different large chest, merging here would
        // form an ambiguous corner/triple arrangement, so refuse it
        // entirely and leave this chest single.
        bool conflict = false;
        for (int facing_sign : {1, -1}) {
            int diagonal_x = nx + facing_step.dx * facing_sign;
            int diagonal_z = nz + facing_step.dz * facing_sign;
            if (get_block(diagonal_x, y, diagonal_z) == BlockType::Chest &&
                chest_part_at(*this, diagonal_x, y, diagonal_z) != ChestPart::Single) {
                conflict = true;
                break;
            }
        }
        if (conflict) continue;

        bool this_is_primary = sign == 1;
        auto packed_part = [](ChestPart part) {
            return static_cast<uint16_t>(static_cast<uint16_t>(part) << BlockStateBits::MULTIBLOCK_PART_SHIFT);
        };
        set_block_state(x, y, z, packed_part(this_is_primary ? ChestPart::Primary : ChestPart::Secondary));
        set_block_state(nx, y, nz, packed_part(this_is_primary ? ChestPart::Secondary : ChestPart::Primary));
        break; // a chest only ever pairs with one neighbor
    }
    return true;
}

int World::command_fill_region(int min_x, int min_y, int min_z, int max_x, int max_y, int max_z, BlockType type)
{
    int clamped_min_y = std::max(min_y, MIN_WORLD_Y);
    int clamped_max_y = std::min(max_y, MIN_WORLD_Y + CHUNK_HEIGHT - 1);
    if (clamped_min_y > clamped_max_y) return 0;

    int placed = 0;
    std::set<std::pair<int, int>> touched_chunks;
    for (int x = min_x; x <= max_x; ++x) {
        for (int z = min_z; z <= max_z; ++z) {
            int chunk_x = floor_div(x, CHUNK_SIZE);
            int chunk_z = floor_div(z, CHUNK_SIZE);
            Chunk* chunk = chunk_at(chunk_x, chunk_z);
            if (chunk == nullptr) continue;
            int local_x = x - chunk_x * CHUNK_SIZE;
            int local_z = z - chunk_z * CHUNK_SIZE;
            {
                std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
                for (int y = clamped_min_y; y <= clamped_max_y; ++y) {
                    chunk->set_block(local_x, y - MIN_WORLD_Y, local_z, type);
                }
            }
            chunk->mark_modified();
            for (int y = clamped_min_y; y <= clamped_max_y; ++y) {
                schedule_fluid_neighbors(x, y, z);
                schedule_falling_check(x, y, z);
                ++placed;
            }
            touched_chunks.insert({chunk_x, chunk_z});
        }
    }
    std::vector<std::pair<int, int>> relight_centers(touched_chunks.begin(), touched_chunks.end());
    relight_chunks_around(relight_centers);
    for (const auto& [chunk_x, chunk_z] : touched_chunks) {
        rebuild_mesh_neighborhood(chunk_x, chunk_z);
    }
    return placed;
}

int World::command_clone_region(int min_x, int min_y, int min_z, int max_x, int max_y, int max_z,
                                 int dest_x, int dest_y, int dest_z)
{
    std::vector<BlockType> buffer;
    buffer.reserve(static_cast<size_t>(max_x - min_x + 1) *
                   static_cast<size_t>(max_y - min_y + 1) *
                   static_cast<size_t>(max_z - min_z + 1));
    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int z = min_z; z <= max_z; ++z) {
                buffer.push_back(get_block(x, y, z));
            }
        }
    }

    int placed = 0;
    std::set<std::pair<int, int>> touched_chunks;
    size_t i = 0;
    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int z = min_z; z <= max_z; ++z) {
                BlockType type = buffer[i++];
                int wx = dest_x + (x - min_x);
                int wy = dest_y + (y - min_y);
                int wz = dest_z + (z - min_z);
                if (wy < MIN_WORLD_Y || wy >= MIN_WORLD_Y + CHUNK_HEIGHT) continue;
                int chunk_x = floor_div(wx, CHUNK_SIZE);
                int chunk_z = floor_div(wz, CHUNK_SIZE);
                Chunk* chunk = chunk_at(chunk_x, chunk_z);
                if (chunk == nullptr) continue;
                int local_x = wx - chunk_x * CHUNK_SIZE;
                int local_z = wz - chunk_z * CHUNK_SIZE;
                {
                    std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
                    chunk->set_block(local_x, wy - MIN_WORLD_Y, local_z, type);
                }
                chunk->mark_modified();
                schedule_fluid_neighbors(wx, wy, wz);
                schedule_falling_check(wx, wy, wz);
                touched_chunks.insert({chunk_x, chunk_z});
                ++placed;
            }
        }
    }
    std::vector<std::pair<int, int>> relight_centers(touched_chunks.begin(), touched_chunks.end());
    relight_chunks_around(relight_centers);
    for (const auto& [chunk_x, chunk_z] : touched_chunks) {
        rebuild_mesh_neighborhood(chunk_x, chunk_z);
    }
    return placed;
}

void World::place_structure_block(int x, int y, int z, BlockType type, bool allow_foliage_overwrite)
{
    BlockType existing = get_block(x, y, z);
    bool can_replace = existing == BlockType::Air || (allow_foliage_overwrite && existing == BlockType::Foliage);
    if (can_replace) set_block_and_rebuild(x, y, z, type);
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
    {
        // Guards against a background mesh job (ChunkWorkerPool) reading
        // this same chunk's data mid-write - see Chunk::data_mutex()'s own
        // comment for the full locking discipline this is one half of.
        std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
        chunk->set_block(local_x, y - MIN_WORLD_Y, local_z, type);
        chunk->mark_modified();
    }

    relight_chunk_neighborhood(chunk_x, chunk_z);
    rebuild_mesh_neighborhood(chunk_x, chunk_z);
}

namespace {
    constexpr int FLUID_NEIGHBOR_OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
}

std::optional<uint8_t> World::compute_fluid_level(int x, int y, int z) const
{
    // Water directly above always feeds this cell, regardless of its own
    // level - a falling column doesn't care how far *that* water is from
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
        // the source itself for spread-distance purposes - only a
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
    // per individual block change - a flood filling a dozen cells in the
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
                // See set_block_and_rebuild()'s own comment on why this
                // write needs to be locked against a concurrent background
                // mesh-job read of this same chunk.
                std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
                chunk->set_block(local_x, local_y, local_z, BlockType::Water);
                chunk->set_fluid_level(local_x, local_y, local_z, *new_level);
                changed = true;
            }
        } else if (current == BlockType::Water) {
            // Nothing feeds this FLOWING/FALLING cell any more -- dry up.
            std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
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

    std::vector<std::pair<int, int>> relight_centers;
    relight_centers.reserve(relit_chunks.size());
    for (int64_t key : relit_chunks) relight_centers.push_back(unpack_chunk_key(key));
    relight_chunks_around(relight_centers);
    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        request_remesh(cx, cz);
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
    if (pending_falling_blocks.empty() && falling_blocks.empty()) return;

    std::unordered_set<int64_t> relit_chunks;
    std::unordered_set<int64_t> needs_mesh;
    auto mark_dirty = [&needs_mesh](int chunk_x, int chunk_z) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                needs_mesh.insert(chunk_key(chunk_x + dx, chunk_z + dz));
            }
        }
    };

    // Phase 1: cells newly (or still) unsupported since last tick each
    // become their own free-falling entity - only entries already queued
    // *before* this call started are resolved this tick (a fixed iteration
    // count taken up front, not a while-loop draining the deque), same
    // reasoning update_fluids() has for its own budget.
    int initial_count = std::min(static_cast<int>(pending_falling_blocks.size()), MAX_FALLING_UPDATES_PER_TICK);
    for (int i = 0; i < initial_count; ++i) {
        std::array<int, 3> cell = pending_falling_blocks.front();
        pending_falling_blocks.pop_front();

        int x = cell[0], y = cell[1], z = cell[2];
        scheduled_falling_cells.erase(fluid_key(x, y, z));

        BlockType type = get_block(x, y, z);
        if (type != BlockType::Sand && type != BlockType::Gravel) continue; // no longer relevant
        if (get_block_properties(get_block(x, y - 1, z)).solid) continue; // already supported

        int chunk_x = floor_div(x, CHUNK_SIZE);
        int chunk_z = floor_div(z, CHUNK_SIZE);
        Chunk* chunk = chunk_at(chunk_x, chunk_z);
        if (chunk == nullptr) continue; // unloaded since this was scheduled

        int local_x = x - chunk_x * CHUNK_SIZE;
        int local_z = z - chunk_z * CHUNK_SIZE;
        {
            std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
            chunk->set_block(local_x, y - MIN_WORLD_Y, local_z, BlockType::Air);
        }
        chunk->mark_modified();
        relit_chunks.insert(chunk_key(chunk_x, chunk_z));
        mark_dirty(chunk_x, chunk_z);

        FallingBlock entity;
        entity.motion.reset({x + 0.5f, y + 0.5f, z + 0.5f});
        entity.type = type;
        falling_blocks.push_back(entity);

        // Whatever water this displaced should react - same as real
        // Minecraft, sand/gravel isn't stopped by water, it falls through
        // and replaces it.
        schedule_fluid_neighbors(x, y, z);
        // Whatever was resting on top of this block - if it's also Sand or
        // Gravel - may have just lost its own support in turn.
        schedule_falling_check(x, y + 1, z);
    }

    // Phase 2: every entity already in flight advances one tick (real
    // per-tick gravity/drag - see FALLING_BLOCK_GRAVITY_PER_TICK's own
    // comment - not a flat one-cell/tick crawl) and lands the instant its
    // underside reaches something solid or the world floor. X/Z are fixed
    // at spawn (no horizontal drift, same as vanilla), so a landing entity
    // always writes back into the exact chunk it fell from.
    for (auto it = falling_blocks.begin(); it != falling_blocks.end();) {
        FallingBlock& entity = *it;
        entity.motion.begin_tick();
        entity.velocity_y -= FALLING_BLOCK_GRAVITY_PER_TICK;
        entity.velocity_y *= FALLING_BLOCK_DRAG_VERTICAL;

        float next_center_y = entity.motion.current.y + entity.velocity_y;
        int grid_x = static_cast<int>(std::floor(entity.motion.current.x));
        int grid_z = static_cast<int>(std::floor(entity.motion.current.z));
        int underside_cell = static_cast<int>(std::floor(next_center_y - 0.5f));

        bool landed = underside_cell < MIN_WORLD_Y ||
                      get_block_properties(get_block(grid_x, underside_cell, grid_z)).solid;
        if (!landed) {
            entity.motion.current.y = next_center_y;
            ++it;
            continue;
        }

        int land_y = std::max(underside_cell + 1, MIN_WORLD_Y);
        // Something may have filled this cell while the block was still
        // falling (a player placing there, say) - if so the block is
        // simply lost rather than overwriting whatever's there now, the
        // same "placement never replaces solid" rule place_block() itself
        // enforces everywhere else.
        if (!get_block_properties(get_block(grid_x, land_y, grid_z)).solid) {
            int chunk_x = floor_div(grid_x, CHUNK_SIZE);
            int chunk_z = floor_div(grid_z, CHUNK_SIZE);
            Chunk* chunk = chunk_at(chunk_x, chunk_z);
            if (chunk != nullptr) {
                int local_x = grid_x - chunk_x * CHUNK_SIZE;
                int local_z = grid_z - chunk_z * CHUNK_SIZE;
                {
                    std::unique_lock<std::shared_mutex> lock(chunk->data_mutex());
                    chunk->set_block(local_x, land_y - MIN_WORLD_Y, local_z, entity.type);
                }
                chunk->mark_modified();
                relit_chunks.insert(chunk_key(chunk_x, chunk_z));
                mark_dirty(chunk_x, chunk_z);
                schedule_fluid_neighbors(grid_x, land_y, grid_z);
                schedule_falling_check(grid_x, land_y - 1, grid_z);
            }
        }
        it = falling_blocks.erase(it);
    }

    std::vector<std::pair<int, int>> relight_centers;
    relight_centers.reserve(relit_chunks.size());
    for (int64_t key : relit_chunks) relight_centers.push_back(unpack_chunk_key(key));
    relight_chunks_around(relight_centers);
    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        request_remesh(cx, cz);
    }
}

void World::draw_falling_blocks(float tick_alpha) const
{
    for (const FallingBlock& entity : falling_blocks) {
        Vector3 position = entity.motion.interpolated(tick_alpha);
        rlPushMatrix();
        rlTranslatef(position.x, position.y, position.z);
        draw_block_cube(entity.type, 255, std::nullopt,
                        entity_environment_tint(*this, position));
        rlPopMatrix();
    }
}

std::array<ItemStack, INVENTORY_STORAGE_SIZE>& World::chest_inventory(int x, int y, int z)
{
    return chest_storage[ChestPosKey{x, y, z}]; // operator[] default-constructs (all-empty) an absent entry
}

std::vector<World::ChestSnapshot> World::all_chest_inventories() const
{
    std::vector<ChestSnapshot> result;
    result.reserve(chest_storage.size());
    for (const auto& [key, slots] : chest_storage) {
        result.push_back({key.x, key.y, key.z, slots});
    }
    return result;
}

FurnaceState& World::furnace_state(int x, int y, int z)
{
    return furnace_storage[ChestPosKey{x, y, z}];
}

FurnaceState World::take_furnace_state(int x, int y, int z)
{
    auto it = furnace_storage.find(ChestPosKey{x, y, z});
    if (it == furnace_storage.end()) return {};
    FurnaceState state = it->second;
    furnace_storage.erase(it);
    return state;
}

std::vector<World::FurnaceSnapshot> World::all_furnace_states() const
{
    std::vector<FurnaceSnapshot> result;
    result.reserve(furnace_storage.size());
    for (const auto& [key, state] : furnace_storage) {
        result.push_back({key.x, key.y, key.z, state});
    }
    return result;
}

void World::update_furnaces()
{
    for (auto it = furnace_storage.begin(); it != furnace_storage.end();) {
        const ChestPosKey& pos = it->first;
        // Only furnaces in loaded chunks run - same as vanilla, where an
        // unloaded furnace simply pauses until its chunk is back.
        if (chunk_at(floor_div(pos.x, CHUNK_SIZE), floor_div(pos.z, CHUNK_SIZE)) == nullptr) {
            ++it;
            continue;
        }
        BlockType block = get_block(pos.x, pos.y, pos.z);
        if (block != BlockType::Furnace && block != BlockType::LitFurnace) {
            it = furnace_storage.erase(it);
            continue;
        }

        FurnaceState& furnace = it->second;
        tick_furnace(furnace);

        // Lit <-> unlit swap. Replacing the block clears its stored
        // facing, so it's written straight back; the furnace's own state
        // lives in this map by position and isn't touched.
        BlockType wanted = furnace.burning() ? BlockType::LitFurnace : BlockType::Furnace;
        if (block != wanted) {
            HorizontalDirection facing = get_block_orientation(pos.x, pos.y, pos.z);
            set_block_and_rebuild(pos.x, pos.y, pos.z, wanted);
            set_block_orientation(pos.x, pos.y, pos.z, facing);
        }
        ++it;
    }
}

std::vector<std::pair<int, int>> World::loaded_chunk_coordinates() const
{
    std::vector<std::pair<int, int>> result;
    result.reserve(chunks.size());
    for (const auto& [key, chunk] : chunks) {
        result.push_back(unpack_chunk_key(key));
    }
    return result;
}

std::array<ItemStack, INVENTORY_STORAGE_SIZE> World::take_chest_inventory(int x, int y, int z)
{
    auto it = chest_storage.find(ChestPosKey{x, y, z});
    if (it == chest_storage.end()) return {};
    std::array<ItemStack, INVENTORY_STORAGE_SIZE> result = it->second;
    chest_storage.erase(it);
    return result;
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

Vector3 World::water_flow_at(Vector3 position) const
{
    int x = static_cast<int>(std::floor(position.x));
    int y = static_cast<int>(std::floor(position.y));
    int z = static_cast<int>(std::floor(position.z));
    if (get_block(x, y, z) != BlockType::Water) return {0.0f, 0.0f, 0.0f};

    auto fluid_level_at = [&](int wx, int wy, int wz) -> std::optional<uint8_t> {
        if (wy < MIN_WORLD_Y || wy >= MIN_WORLD_Y + CHUNK_HEIGHT) return std::nullopt;
        if (get_block(wx, wy, wz) != BlockType::Water) return std::nullopt;

        int chunk_x = floor_div(wx, CHUNK_SIZE);
        int chunk_z = floor_div(wz, CHUNK_SIZE);
        const Chunk* chunk = chunk_at(chunk_x, chunk_z);
        if (chunk == nullptr) return std::nullopt;
        return chunk->get_fluid_level(wx - chunk_x * CHUNK_SIZE, wy - MIN_WORLD_Y, wz - chunk_z * CHUNK_SIZE);
    };

    auto effective_level = [](uint8_t level) {
        return (level == FLUID_LEVEL_SOURCE || level == FLUID_LEVEL_FALLING) ? 0 : static_cast<int>(level);
    };

    std::optional<uint8_t> current_level = fluid_level_at(x, y, z);
    if (!current_level.has_value()) return {0.0f, 0.0f, 0.0f};

    Vector3 flow{0.0f, 0.0f, 0.0f};
    const int current_effective = effective_level(*current_level);
    constexpr int OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& offset : OFFSETS) {
        int nx = x + offset[0];
        int nz = z + offset[1];
        if (std::optional<uint8_t> neighbor_level = fluid_level_at(nx, y, nz)) {
            int delta = effective_level(*neighbor_level) - current_effective;
            if (delta > 0) {
                flow.x += static_cast<float>(offset[0] * delta);
                flow.z += static_cast<float>(offset[1] * delta);
            }
        } else if (get_block(nx, y, nz) == BlockType::Air &&
                   !get_block_properties(get_block(nx, y - 1, nz)).solid) {
            // A spill edge: the next cell is open and unsupported below, so
            // bias the current toward the falling sheet instead of leaving
            // the last horizontal cell feeling still.
            flow.x += static_cast<float>(offset[0]) * 1.5f;
            flow.z += static_cast<float>(offset[1]) * 1.5f;
        }
    }

    if (Vector3LengthSqr(flow) <= 0.000001f) return {0.0f, 0.0f, 0.0f};
    return Vector3Normalize(flow);
}

void World::update_chunk_states(Vector3 observer_position)
{
    ChunkCoordinates observer_chunk = chunk_coordinates(
        static_cast<int>(std::floor(observer_position.x)),
        static_cast<int>(std::floor(observer_position.z)));

    // Nothing can have changed state since the last call if the observer is
    // still in the same chunk it was in then. Pending generation tickets are
    // the exception: they are intentionally paced across ticks.
    if (last_observer_chunk && last_observer_chunk->x == observer_chunk.x && last_observer_chunk->z == observer_chunk.z) {
        dispatch_pending_generation_jobs();
        return;
    }
    last_observer_chunk = observer_chunk;
    pending_generation.clear();

    // Bring every chunk within LOADED_RADIUS up to its correct state: for
    // an already-loaded chunk that's just the Active/Loaded tick flag - no
    // generation involved, no remesh needed (the chunk's own geometry
    // hasn't changed, only which of Active/Loaded it's ticked as). Anything
    // not loaded yet becomes a nearest-first generation ticket here; only a
    // small batch is actually dispatched below, and nothing about that
    // chunk's state or neighbors' meshes is touched until
    // integrate_worker_results() picks up the finished GenResult later.
    int min_x = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.x - config.loaded_radius_chunks);
    int max_x = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.x + config.loaded_radius_chunks);
    int min_z = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.z - config.loaded_radius_chunks);
    int max_z = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.z + config.loaded_radius_chunks);

    std::vector<ChunkCoord> missing;
    for (int cx = min_x; cx <= max_x; ++cx) {
        for (int cz = min_z; cz <= max_z; ++cz) {
            Chunk* chunk = chunk_at(cx, cz);
            if (chunk == nullptr) {
                int64_t key = chunk_key(cx, cz);
                if (generating.count(key) == 0) {
                    missing.push_back({cx, cz});
                }
                continue;
            }
            chunk->set_state(desired_state_for(cx, cz, observer_chunk));
        }
    }
    std::sort(missing.begin(), missing.end(), [observer_chunk](ChunkCoord a, ChunkCoord b) {
        int da = std::max(std::abs(a.x - observer_chunk.x), std::abs(a.z - observer_chunk.z));
        int db = std::max(std::abs(b.x - observer_chunk.x), std::abs(b.z - observer_chunk.z));
        if (da != db) return da < db;
        int ma = std::abs(a.x - observer_chunk.x) + std::abs(a.z - observer_chunk.z);
        int mb = std::abs(b.x - observer_chunk.x) + std::abs(b.z - observer_chunk.z);
        return ma < mb;
    });
    for (ChunkCoord coord : missing) pending_generation.push_back(coord);

    // Unload anything still resident that fell outside LOADED_RADIUS - kept
    // synchronous and immediate (cheap: no generation, no meshing, just
    // freeing what's there - see unload_chunk()'s own comment on why this
    // is safe even with a background mesh job possibly still reading one of
    // these chunks as a neighbor). Collected first since unload_chunk()
    // erases from `chunks` - iterating and erasing from the same map at
    // once needs more care than this is worth for a scan that only runs
    // when the observer changes chunks.
    std::vector<std::pair<int, int>> out_of_range;
    for (const auto& [key, chunk] : chunks) {
        auto [cx, cz] = unpack_chunk_key(key);
        if (chebyshev_distance(cx, cz, observer_chunk.x, observer_chunk.z) > config.loaded_radius_chunks) {
            out_of_range.emplace_back(cx, cz);
        }
    }
    std::vector<std::pair<int, int>> relight_centers;
    for (auto [cx, cz] : out_of_range) {
        unload_chunk(cx, cz);
        relight_centers.emplace_back(cx, cz);
        // A departed neighbor changes how its still-loaded neighbors' own
        // border faces should read (stale AO/light baked in against a
        // chunk that's no longer there) - schedule those for a background
        // remesh, same as the old synchronous version always did. This
        // coordinate's own request_remesh() call below is a no-op (nothing
        // loaded there any more), which is fine - only the neighbors matter
        // here.
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                request_remesh(cx + dx, cz + dz);
            }
        }
    }
    relight_chunks_around(relight_centers);
    dispatch_pending_generation_jobs();
}

void World::update_chunk_states_blocking(Vector3 observer_position)
{
    ChunkCoordinates observer_chunk = chunk_coordinates(
        static_cast<int>(std::floor(observer_position.x)),
        static_cast<int>(std::floor(observer_position.z)));

    last_observer_chunk = observer_chunk;

    // Every chunk a generate/unload this call touches needs its mesh (and
    // its neighbors', per rebuild_mesh_neighborhood's reasoning) rebuilt -
    // collected here instead of meshing immediately inside generate_chunk/
    // unload_chunk, and only actually rebuilt once each in a final pass
    // below. The set dedups: a border chunk shared by several newly-loaded
    // (or unloaded) neighbors would otherwise get remeshed once per
    // neighbor instead of once, total - measured at 1345 rebuilds for 289
    // chunks' worth of initial world generation before this batching, ~4.6x
    // more than the 289 actually needed.
    std::unordered_set<int64_t> needs_mesh;
    std::vector<std::pair<int, int>> needs_relight;
    auto mark_dirty = [&needs_mesh](int chunk_x, int chunk_z) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                needs_mesh.insert(chunk_key(chunk_x + dx, chunk_z + dz));
            }
        }
    };

    // Bring every chunk within LOADED_RADIUS up to its correct state:
    // generate whatever isn't loaded yet, then set the state that was
    // actually asked for either way - for an already-loaded chunk that's
    // just the Active/Loaded tick flag, no generation involved.
    int min_x = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.x - config.loaded_radius_chunks);
    int max_x = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.x + config.loaded_radius_chunks);
    int min_z = std::max(-WORLD_BORDER_CHUNKS, observer_chunk.z - config.loaded_radius_chunks);
    int max_z = std::min(WORLD_BORDER_CHUNKS - 1, observer_chunk.z + config.loaded_radius_chunks);

    // Loading-screen progress (set_load_progress_callback()): terrain
    // generation is by far the heaviest part, then meshing, then light.
    constexpr float TERRAIN_SHARE = 0.6f;
    constexpr float LIGHTING_SHARE = 0.1f;
    auto report = [this](WorldLoadStage stage, float progress) {
        if (load_progress) load_progress(stage, progress);
    };
    int to_generate = 0;
    if (load_progress) {
        for (int cx = min_x; cx <= max_x; ++cx) {
            for (int cz = min_z; cz <= max_z; ++cz) {
                if (chunk_at(cx, cz) == nullptr) ++to_generate;
            }
        }
    }
    int generated = 0;
    report(WorldLoadStage::Terrain, 0.0f);

    for (int cx = min_x; cx <= max_x; ++cx) {
        for (int cz = min_z; cz <= max_z; ++cz) {
            Chunk* chunk = chunk_at(cx, cz);
            if (chunk == nullptr) {
                generate_chunk(cx, cz);
                chunk = chunk_at(cx, cz);
                mark_dirty(cx, cz);
                needs_relight.emplace_back(cx, cz);
                ++generated;
                report(WorldLoadStage::Terrain, TERRAIN_SHARE * generated / std::max(1, to_generate));
            }
            chunk->set_state(desired_state_for(cx, cz, observer_chunk));
        }
    }

    // Unload anything still resident that fell outside LOADED_RADIUS.
    // Collected first since unload_chunk() erases from `chunks` - iterating
    // and erasing from the same map at once needs more care than this is
    // worth for a scan that only runs when the observer changes chunks.
    std::vector<std::pair<int, int>> out_of_range;
    for (const auto& [key, chunk] : chunks) {
        auto [cx, cz] = unpack_chunk_key(key);
        if (chebyshev_distance(cx, cz, observer_chunk.x, observer_chunk.z) > config.loaded_radius_chunks) {
            out_of_range.emplace_back(cx, cz);
        }
    }
    for (auto [cx, cz] : out_of_range) {
        unload_chunk(cx, cz);
        mark_dirty(cx, cz);
        needs_relight.emplace_back(cx, cz);
    }

    report(WorldLoadStage::Lighting, TERRAIN_SHARE);
    relight_chunks_around(needs_relight);

    const float mesh_start = TERRAIN_SHARE + LIGHTING_SHARE;
    report(WorldLoadStage::Meshes, mesh_start);
    int meshed = 0;
    for (int64_t key : needs_mesh) {
        auto [cx, cz] = unpack_chunk_key(key);
        rebuild_mesh(cx, cz);
        ++meshed;
        report(WorldLoadStage::Meshes,
               mesh_start + (1.0f - mesh_start) * meshed / std::max<size_t>(1, needs_mesh.size()));
    }
    report(WorldLoadStage::Meshes, 1.0f);
}

void World::set_view_distance(int loaded_radius_chunks, int fog_distance_blocks)
{
    if (config.loaded_radius_chunks == loaded_radius_chunks && config.fog_distance_blocks == fog_distance_blocks) {
        return; // called unconditionally from GameEngine::tick() - nothing to do most ticks
    }

    if (config.loaded_radius_chunks != loaded_radius_chunks) {
        config.loaded_radius_chunks = loaded_radius_chunks;
        // Active must never exceed Loaded - same clamp the constructor
        // applies once up front (see World::World()).
        config.active_radius_chunks = std::min(config.active_radius_chunks, config.loaded_radius_chunks);
        // update_chunk_states()'s own early-out only rescans once the
        // observer enters a different chunk than last time - nothing else
        // would otherwise notice this radius changed until the player next
        // moved. Forcing that rescan here, immediately, is what makes a
        // Settings change apply live instead of merely being remembered
        // for the next chunk crossing.
        last_observer_chunk.reset();
        pending_generation.clear();
    }
    config.fog_distance_blocks = fog_distance_blocks; // draw_opaque() reads this fresh every frame - nothing else to nudge
}

void World::integrate_worker_results()
{
    // Anything a worker deferred destroying (because dropping its last
    // shared_ptr reference happened to land on a worker thread - see
    // make_chunk()'s own comment) actually gets deleted here, on the main
    // thread.
    worker_pool->reclaim_pending_destroys();

    std::vector<std::pair<int, int>> relight_centers;
    std::vector<ChunkCoord> remesh_centers;
    for (ChunkWorkerPool::GenResult& result : worker_pool->drain_gen_results(MAX_GEN_INTEGRATIONS_PER_FRAME)) {
        int64_t key = chunk_key(result.chunk_x, result.chunk_z);
        generating.erase(key);

        // Shouldn't happen - generation dispatch's own `generating` dedup
        // prevents a second in-flight job for the same coordinate, but
        // check rather than assume before inserting.
        if (chunks.find(key) != chunks.end()) continue;
        if (last_observer_chunk &&
            chebyshev_distance(result.chunk_x, result.chunk_z,
                               last_observer_chunk->x, last_observer_chunk->z) > config.loaded_radius_chunks) {
            continue;
        }

        Chunk* chunk_ptr = result.chunk.get();
        chunks.emplace(key, std::move(result.chunk));
        if (last_observer_chunk) {
            chunk_ptr->set_state(desired_state_for(result.chunk_x, result.chunk_z, *last_observer_chunk));
        }

        relight_centers.emplace_back(result.chunk_x, result.chunk_z);
        remesh_centers.push_back({result.chunk_x, result.chunk_z});
    }

    relight_chunks_around(relight_centers);

    for (ChunkCoord coord : remesh_centers) {
        // Cascade: this chunk's own coordinate, and every neighbor that may
        // have been waiting on it to exist before its own mesh job could
        // pass remesh_neighborhood_ready() - without this, a chunk at the
        // trailing edge of a fast-moving load wave could end up generated
        // but never actually meshed.
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                request_remesh(coord.x + dx, coord.z + dz);
            }
        }
    }

    for (ChunkWorkerPool::MeshResult& result : worker_pool->drain_mesh_results(MAX_MESH_INTEGRATIONS_PER_FRAME)) {
        int64_t key = chunk_key(result.chunk_x, result.chunk_z);
        meshing.erase(key);

        // Only actually upload if this chunk is still the live one at this
        // coordinate - it may have been unloaded while the job was in
        // flight, in which case this geometry is for a chunk nobody will
        // ever draw again (see request_remesh()'s own comment on why the
        // job still ran to completion instead of being cancelled).
        if (chunk_at(result.chunk_x, result.chunk_z) == result.chunk.get()) {
            result.chunk->upload_mesh_data(std::move(result.mesh_data));
        }

        // Something changed this chunk's data again while this job was
        // already in flight - that change may not be reflected in the
        // result that just landed, so schedule a fresh job for it now.
        if (remesh_pending.erase(key)) {
            request_remesh(result.chunk_x, result.chunk_z);
        }
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
    // area, so this can actually confirm real, clear ground to stand on -
    // not just "probably land" - before accepting it.
    auto try_candidate = [this](int x, int z) -> std::optional<Vector3> {
        // Blocking: this needs the candidate's ground fully generated (and
        // its ChunkState set) the instant this call returns, so it can read
        // blocks back immediately below - the async update_chunk_states()
        // would only dispatch the generation job and return, with nothing
        // actually there yet.
        update_chunk_states_blocking({static_cast<float>(x), 0.0f, static_cast<float>(z)});

        for (int y = MIN_WORLD_Y + CHUNK_HEIGHT - 2; y >= MIN_WORLD_Y; --y) {
            if (!get_block_properties(get_block(x, y, z)).solid) continue;
            // Found the ground. Only actually a valid spawn if there's
            // room to stand in above it - a beach column can dip just
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
    if (distance <= config.active_radius_chunks) return ChunkState::Active;
    if (distance <= config.loaded_radius_chunks) return ChunkState::Loaded;
    return ChunkState::Unloaded; // never actually assigned to a Chunk - see update_chunk_states
}

void World::generate_chunk(int chunk_x, int chunk_z)
{
    // Same work generate_chunk_data() does for a background GenJob (see
    // ChunkWorkerPool.hpp), just run synchronously right here and fed
    // World's own shared terrain_noise - safe only because this (the
    // blocking bootstrap path) never runs concurrently with anything else
    // that might also be using it (see update_chunk_states_blocking()'s own
    // comment).
    chunks.emplace(chunk_key(chunk_x, chunk_z),
                    generate_chunk_data(chunk_x, chunk_z, config.seed, config.save_directory, *terrain_noise));
}

void World::unload_chunk(int chunk_x, int chunk_z)
{
    Chunk* chunk = chunk_at(chunk_x, chunk_z);
    if (chunk == nullptr) return;

    if (config.save_directory && chunk->is_modified()) {
        chunk->save_to_file(chunk_file_path(chunk_x, chunk_z));
    }

    // Drops World's own reference; if a background mesh job is still
    // reading this chunk as its target or as a neighbor, it holds its own
    // shared_ptr copy (see ChunkWorkerPool::MeshJobInput), so this doesn't
    // destroy it out from under that job - see make_chunk()'s own comment
    // (ChunkWorkerPool.hpp) for why a chunk's eventual destruction still
    // needs to be deferred to the main thread even so.
    chunks.erase(chunk_key(chunk_x, chunk_z));

    // No neighborhood remesh here - callers that need one (update_chunk_
    // states()) schedule it themselves via request_remesh(), same batching
    // reasoning update_chunk_states_blocking() still uses its own mark_dirty
    // for.
}

void World::dispatch_pending_generation_jobs()
{
    if (pending_generation.empty()) return;

    ChunkCoord observer = last_observer_chunk
        ? ChunkCoord{last_observer_chunk->x, last_observer_chunk->z}
        : pending_generation.front();
    std::vector<ChunkCoord> batch;
    batch.reserve(MAX_GEN_DISPATCHES_PER_UPDATE);

    while (!pending_generation.empty() && batch.size() < MAX_GEN_DISPATCHES_PER_UPDATE) {
        ChunkCoord coord = pending_generation.front();
        pending_generation.pop_front();
        int64_t key = chunk_key(coord.x, coord.z);

        if (chunk_at(coord.x, coord.z) != nullptr) continue;
        if (chebyshev_distance(coord.x, coord.z, observer.x, observer.z) > config.loaded_radius_chunks) continue;
        if (!generating.insert(key).second) continue;
        batch.push_back(coord);
    }

    if (!batch.empty()) {
        worker_pool->submit_gen_jobs(batch, observer);
    }
}

bool World::remesh_neighborhood_ready(int chunk_x, int chunk_z) const
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            int nx = chunk_x + dx;
            int nz = chunk_z + dz;
            if (nx < -WORLD_BORDER_CHUNKS || nx >= WORLD_BORDER_CHUNKS ||
                nz < -WORLD_BORDER_CHUNKS || nz >= WORLD_BORDER_CHUNKS) {
                continue; // past the world border entirely - permanently absent, same as chunk_at() returning null there
            }
            if (chunk_at(nx, nz) != nullptr) continue; // present

            if (generating.count(chunk_key(nx, nz)) > 0) return false; // still on its way - wait for it

            // Not present, not generating, in-bounds: simply outside the
            // currently-loaded radius (e.g. this coordinate sits near the
            // edge of config.loaded_radius_chunks and this neighbor one
            // step further out was never requested) - permanently absent
            // for meshing purposes, same as before (reads as open air,
            // matching a null neighbor the old synchronous rebuild_mesh()
            // always allowed).
        }
    }
    return true;
}

void World::request_remesh(int chunk_x, int chunk_z)
{
    if (!remesh_neighborhood_ready(chunk_x, chunk_z)) return; // integrate_worker_results()'s own cascade retries this once it is

    int64_t key = chunk_key(chunk_x, chunk_z);
    if (meshing.count(key) > 0) {
        // A job for this coordinate is already running. It may have
        // started reading before whatever just triggered this call, so its
        // result can't be assumed to reflect it - remember to re-request
        // once it lands (integrate_worker_results()) instead of assuming
        // this call is redundant.
        remesh_pending.insert(key);
        return;
    }

    ChunkWorkerPool::MeshJobInput job;
    job.chunk_x = chunk_x;
    job.chunk_z = chunk_z;
    job.chunks[0] = chunk_shared_at(chunk_x, chunk_z);
    if (!job.chunks[0]) return; // nothing loaded at this coordinate (any more) - nothing to mesh
    // Order matches Chunk::build_mesh_data()'s own parameter order.
    job.chunks[1] = chunk_shared_at(chunk_x - 1, chunk_z);     // west
    job.chunks[2] = chunk_shared_at(chunk_x + 1, chunk_z);     // east
    job.chunks[3] = chunk_shared_at(chunk_x, chunk_z - 1);     // north
    job.chunks[4] = chunk_shared_at(chunk_x, chunk_z + 1);     // south
    job.chunks[5] = chunk_shared_at(chunk_x - 1, chunk_z - 1); // northwest
    job.chunks[6] = chunk_shared_at(chunk_x + 1, chunk_z - 1); // northeast
    job.chunks[7] = chunk_shared_at(chunk_x - 1, chunk_z + 1); // southwest
    job.chunks[8] = chunk_shared_at(chunk_x + 1, chunk_z + 1); // southeast

    meshing.insert(key);
    ChunkCoord observer = last_observer_chunk
        ? ChunkCoord{last_observer_chunk->x, last_observer_chunk->z}
        : ChunkCoord{chunk_x, chunk_z};
    std::vector<ChunkWorkerPool::MeshJobInput> batch;
    batch.push_back(std::move(job));
    worker_pool->submit_mesh_jobs(std::move(batch), observer);
}
