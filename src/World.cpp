#include "World.hpp"
#include "core/PerlinNoise.hpp"

#include "raymath.h"

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

    // How far, in chunks, a chunk is drawn (LOADED_RADIUS) vs. drawn *and*
    // ticking (ACTIVE_RADIUS) around an observer — Minecraft's own render
    // vs. simulation distance split. Square (Chebyshev) radius, same shape
    // Minecraft loads in, not a circle.
    constexpr int LOADED_RADIUS = 8;
    constexpr int ACTIVE_RADIUS = 4;

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
}

World::World(uint32_t seed)
    : terrain_noise(std::make_unique<PerlinNoise>(seed))
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

void World::draw() const
{
    for (const auto& [key, chunk] : chunks) {
        chunk->draw();
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
}

void World::place_block(int x, int y, int z, BlockType type)
{
    if (get_block_properties(get_block(x, y, z)).solid) return; // something's already there
    set_block_and_rebuild(x, y, z, type);
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
