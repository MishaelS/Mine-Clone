#pragma once

#include "raylib.h"
#include "WorldObject.hpp"
#include "core/Block.hpp"

#include <array>
#include <cstdint>

class TerrainNoise;

constexpr int CHUNK_SIZE = 16;    // width/depth (X/Z) — chunks are still only streamed in the X/Z grid (World::update_chunk_states), no vertical stacking
constexpr int CHUNK_HEIGHT = 384; // Y — a single chunk spans the whole world height, Minecraft 1.18+'s build limit (-64..319)

// World-space Y of a chunk's own local index 0 — Minecraft's own world
// floor (bedrock generates here). A Chunk's internal block/light arrays and
// loops stay plainly 0-based (0..CHUNK_HEIGHT-1), same as X/Z; World is the
// only place that translates a world-space Y to/from that local index, by
// subtracting/adding this — see World::get_block()/get_light()/
// set_block_and_rebuild() and World::generate_chunk()'s own Y position.
constexpr int MIN_WORLD_Y = -64;

// Brightest possible sky/block light level (see Chunk::get_light);
// exported so anything sampling light outside a Chunk (World, the debug
// overlay) can express its own out-of-range fallback in the same units
// rather than a bare magic number.
constexpr int MAX_LIGHT = 15;

// A chunk's simulation/render tier, based on distance from an observer (see
// World::update_chunk_states). There's no Chunk object for an Unloaded
// coordinate at all — World reports that state itself for any (x, z) it
// has no Chunk for; a live Chunk is always at least Loaded.
//
// This is also the seam a future client/server split grows from: Loaded
// vs. Active already means exactly "the client should draw this, but not
// simulate it" vs. "draw and simulate" — a server deciding that instead of
// each client's own distance check, and sending the result over the
// network, wouldn't need this enum or anything that reads it to change.
enum class ChunkState : uint8_t {
    Unloaded,
    Loaded,
    Active,
};

// Compiles assets/shaders/chunk.{vs,fs} — raylib's own default mesh shader
// (texture*vertexColor, so AO/tint already baked into vertex colors by
// Chunk::build_mesh keeps working unchanged) plus linear distance fog.
// Every chunk's mesh material (get_chunk_material() in Chunk.cpp) uses this
// one shared shader. Call once, after the window exists (needs a GL
// context) — GameEngine's constructor does this alongside
// Load_block_definitions()/FontManager::get().
void load_chunk_shader();

// Configures every chunk's shared atlas material for distance fog — call
// once per frame (World::draw() does this) before any Chunk::draw(), since
// camera_position changes every frame. Fragments at or past fog_end fade
// fully to fog_color; fragments before fog_start are unaffected; a linear
// ramp fills the gap between them (world units from the camera). fog_color
// should match the skybox's own horizon color (skybox_horizon_color(),
// Skybox.hpp) so the render-distance edge reads as fading into the sky
// instead of a hard cutoff where chunks just stop being drawn.
void set_chunk_fog(Vector3 camera_position, Color fog_color, float fog_start, float fog_end);

// Frees the shader set_chunk_fog()/every Chunk's mesh material shares.
// Call once before CloseWindow() — unlike the plain-texture default
// material this replaced, it isn't a raylib-internal resource freed
// automatically by CloseWindow()'s own cleanup.
void unload_chunk_fog_shader();

// A CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE grid of blocks, positioned in the
// world by WorldObject's position (its min corner, not its center). Block
// data builds into a single GPU mesh (build_mesh()) with hidden faces culled
// out, so a whole chunk draws in one call instead of one draw per visible
// block face.
class Chunk : public WorldObject {
public:
    explicit Chunk(Vector3 position = {0.0f, 0.0f, 0.0f});
    ~Chunk() override;

    // Owns a GPU mesh; copying would double-free it, so don't.
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // Fills the chunk with terrain: a biome per (x, z) column (TerrainNoise's
    // temperature/humidity layers), that biome's own height range and
    // surface/subsurface blocks, stone below that, and bedrock at y=0.
    // `noise` is sampled at this chunk's world-space X/Z so both terrain
    // height and biome are continuous across chunk borders.
    void generate_terrain(const TerrainNoise& noise);

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

    // Draws this chunk's opaque geometry (everything except translucent
    // blocks like water — see draw_water()).
    void draw() const override;

    // Draws this chunk's translucent geometry (water — see
    // BlockProperties::translucent) — a separate mesh from draw()'s, built
    // by the same build_mesh() call. World::draw() calls this in its own
    // pass, after every chunk's draw() (opaque geometry world-wide) and
    // with alpha blending enabled, so translucent faces correctly blend
    // over everything solid instead of however they'd happen to interleave
    // with it by draw order alone.
    void draw_water() const;

    BlockType get_block(int x, int y, int z) const;
    void set_block(int x, int y, int z, BlockType type);

    // max(sky, block) light, 0..15. Public so a neighboring chunk's
    // build_mesh() can sample real light data across a chunk border instead
    // of guessing — bounds-checked to MAX_LIGHT (open, sunlit space)
    // outside this chunk, since there's no neighbor-chunk data to fall back
    // on here; the caller is expected to resolve cross-chunk coordinates
    // itself and call this only with this chunk's own local coordinates.
    int get_light(int x, int y, int z) const;

    // Always Loaded or Active for a live Chunk (see ChunkState) — World is
    // the only writer, via update_chunk_states()'s transition handling.
    ChunkState get_state() const { return state; }
    void set_state(ChunkState new_state) { state = new_state; }

    // Set by World whenever a block here changes after generation
    // (break_block/place_block), so a future unload can tell a chunk that
    // needs its edits saved apart from one that can just be regenerated.
    // Not acted on yet — see World::unload_chunk's TODO.
    bool is_modified() const { return modified; }
    void mark_modified() { modified = true; }

private:
    static int index(int x, int y, int z);

    // Chunk-local opacity check for light propagation (transparent blocks,
    // including air, let light pass through). Out-of-range counts as open.
    bool is_opaque(int x, int y, int z) const;

    int get_sky_light(int x, int y, int z) const;
    void set_sky_light(int x, int y, int z, int value);
    int get_block_light(int x, int y, int z) const;
    void set_block_light(int x, int y, int z, int value);

    std::array<BlockType, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> blocks;

    // Packed per-cell light: upper nibble = sky light, lower nibble = block
    // light, each 0-15.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> light{};

    // Highest Y with a non-Air block anywhere in this chunk (generation
    // tracks it; set_block() only ever raises it, never lowers it — cheap
    // and safe, since overestimating just means scanning a few extra
    // guaranteed-air rows, while underestimating would hide real blocks).
    // With CHUNK_HEIGHT now 256 but actual terrain rarely reaching much
    // above 90, build_mesh() and compute_lighting() use this to skip the
    // rest of the column instead of always walking the full 256, since it's
    // guaranteed air up there — see get_sky_light() for the one place that
    // needs to know the difference between "above this" and "out of chunk".
    int highest_block_y = 0;

    // vertexCount == 0 (and mesh_uploaded == false) until build_mesh() runs.
    Mesh mesh{};
    bool mesh_uploaded = false;

    // Translucent geometry (water), built and drawn separately from `mesh`
    // — see draw_water().
    Mesh water_mesh{};
    bool water_mesh_uploaded = false;

    // A live Chunk is always at least Loaded (see ChunkState) — Unloaded is
    // never stored, only reported by World for a coordinate with no Chunk.
    ChunkState state = ChunkState::Loaded;
    bool modified = false;
};
