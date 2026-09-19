#pragma once

#include "raylib.h"
#include "core/GameObject.hpp"
#include "core/Block.hpp"

#include <array>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class TerrainNoise;

constexpr int CHUNK_SIZE   = 16;  // width/depth (X/Z) - chunks are still only streamed in the X/Z grid (World::update_chunk_states), no vertical stacking
constexpr int CHUNK_HEIGHT = 384; // Y - a single chunk spans the whole world height, Minecraft 1.18+'s build limit (-64..319)

// World-space Y of a chunk's own local index 0 - Minecraft's own world
// floor (bedrock generates here). A Chunk's internal block/light arrays and
// loops stay plainly 0-based (0..CHUNK_HEIGHT-1), same as X/Z; World is the
// only place that translates a world-space Y to/from that local index, by
// subtracting/adding this - see World::get_block()/get_light()/
// set_block_and_rebuild() and World::generate_chunk()'s own Y position.
constexpr int MIN_WORLD_Y = -64;

// Brightest possible sky/block light level (see Chunk::get_light);
// exported so anything sampling light outside a Chunk (World, the debug
// overlay) can express its own out-of-range fallback in the same units
// rather than a bare magic number.
constexpr int MAX_LIGHT = 15;

// Minimum brightness fraction (of full light) anything ever renders at,
// even in a fully-dark (light level 0) cell - real Minecraft's ambient
// occlusion still leaves shapes faintly readable in total darkness rather
// than rendering flat black, and a shared floor keeps every light consumer
// (chunk mesh faces, cross-shaped foliage, dropped items/mobs/the player
// via entity_environment_tint) agreeing on the same darkest brightness -
// without that, whichever path floored higher would read as visibly
// brighter than its own genuinely-dark surroundings.
constexpr float MIN_LIGHT_FRACTION = 0.2f;

// A chunk's simulation/render tier, based on distance from an observer (see
// World::update_chunk_states). There's no Chunk object for an Unloaded
// coordinate at all - World reports that state itself for any (x, z) it
// has no Chunk for; a live Chunk is always at least Loaded.
//
// This is also the seam a future client/server split grows from: Loaded
// vs. Active already means exactly "the client should draw this, but not
// simulate it" vs. "draw and simulate" - a server deciding that instead of
// each client's own distance check, and sending the result over the
// network, wouldn't need this enum or anything that reads it to change.
enum class ChunkState : uint8_t {
    Unloaded,
    Loaded,
    Active,
};

// Per-cell fluid state (Chunk::get_fluid_level/set_fluid_level), matching
// real Minecraft's own water: a SOURCE never dries up and always spreads
// at full strength; FLOWING water is 1-7 blocks of horizontal distance
// from the nearest effective source and dries up (World::update_fluids)
// the moment nothing feeds it any more; FALLING is water with more water
// directly above it - acts like a fresh source for spreading sideways
// *from this layer*, but unlike a real source it dries up if that feed
// from above stops. Meaningless wherever the block itself isn't Water.
constexpr uint8_t FLUID_LEVEL_SOURCE   = 0;
constexpr uint8_t FLUID_LEVEL_MAX_FLOW = 7;  // farthest a FLOWING level can reach; one more step than this can't flow at all
constexpr uint8_t FLUID_LEVEL_FALLING  = 8;

// Compiles assets/shaders/chunk.{vs,fs} - raylib's own default mesh shader
// (texture*vertexColor, so AO/tint already baked into vertex colors by
// Chunk::build_mesh keeps working unchanged) plus linear distance fog.
// Every chunk's mesh material (get_chunk_material() in Chunk.cpp) uses this
// one shared shader. Call once, after the window exists (needs a GL
// context) - GameEngine's constructor does this alongside
// Load_block_definitions()/FontManager::get().
void load_chunk_shader();

// Configures every chunk's shared atlas material for distance fog - call
// once per frame (World::draw() does this) before any Chunk::draw(), since
// camera_position changes every frame. Fragments at or past fog_end fade
// fully to a fog color; fragments before fog_start are unaffected; a
// linear ramp fills the gap between them (world units from the camera).
// That fog color itself isn't flat - the shader blends fog_color toward
// fog_sky_color as the view ray tilts upward (fog_color at eye level,
// fog_sky_color straight up), matching the skybox's own vertical gradient
// (skybox_horizon_color()/skybox_sky_color(), Skybox.hpp) instead of one
// fixed tone, so a chunk silhouette rising above the horizon line fogs
// into whatever sky color actually sits behind it at that height rather
// than staying visible as a mismatched pale shape against a bluer sky.
void set_chunk_fog(Vector3 camera_position, Color fog_color, Color fog_sky_color, float fog_start, float fog_end);

// Feeds the water shader's own animation its clock - call once per frame
// (World::draw() does this, same as set_chunk_fog()) with seconds since
// startup (or any other steadily-increasing time source); drives the
// scrolling-texel flow effect in assets/shaders/chunk.fs, active only
// while isWaterPass is true (see set_chunk_water_pass()).
void set_chunk_water_time(float time);

// Toggles the water shader's animation on/off - true right before
// Chunk::draw_water() draws anything, false again right after, since
// every chunk's opaque and water mesh share this one Material/shader (see
// get_chunk_material() in Chunk.cpp) and the animation must only affect
// the water pass, not every other block's texture too. World::draw() is
// the only caller.
void set_chunk_water_pass(bool active);

// Feeds the chunk shader's own day/night dimming - call once per frame
// (GameEngine::draw() does this, same as set_chunk_fog()) with
// DayNightCycle::sky_light_factor(game_tick): 1.0 at full day, its own
// MIN_NIGHT_SKY_LIGHT_FACTOR at full night. The fragment shader multiplies
// this into each fragment's own sky-light vertex channel only - block
// light (torches, lava), baked into a separate channel, is never scaled by
// it - see chunk.fs's own comment.
void set_chunk_daylight(float sky_light_factor);

// Settings > Graphics' own brightness slider - call once per frame with a
// gamma exponent derived from settings.brightness (1.0 at the slider's own
// max - a no-op). Applied as pow(lightScale, gamma) to the *final* combined
// light strength (block and sky alike, after day/night's own combine), so
// it only ever darkens shadow: anything already at full strength (direct
// sunlight, a torch up close) stays pow(1.0, gamma) == 1.0 regardless of
// the slider, same as real sunlit blocks never dimming from it. A flat
// client-side render adjustment, not a lighting-simulation value - never
// touches the skybox/sun/moon/fog (not lit by a block light level at all)
// or World::get_effective_light()'s own data, only how dim/bright
// already-computed block lighting reads on screen.
void set_chunk_brightness(float gamma);

// True only while begin_dynamic_entity_shader()'s own draws (players,
// item entities, particles) are active. Dynamic entities have no per-
// vertex sky/block light channels the way a chunk mesh does (see
// chunk.fs's own comment) - their light is already fully baked into
// vertexColor by entity_environment_tint(), which folds day/night in
// itself (EntityLighting.hpp's set_entity_daylight_factor()) - so while
// this is true, the shader skips its own two-channel combine and just
// trusts that baked color as-is. begin_dynamic_entity_shader()/
// end_dynamic_entity_shader() toggle this themselves; World::draw_opaque()/
// draw_translucent() also set it false before their own chunk-mesh draws,
// so neither relies on call-order assumptions about the other.
void set_chunk_dynamic_entity_pass(bool active);

// Dynamic immediate-mode geometry (players, item entities, particles) must
// use the same fog program as chunk meshes.  Without it, a camera inside
// water sees fogged terrain but perfectly sharp entities, making them look
// as if they were composited on top of the water volume.
void begin_dynamic_entity_shader();
void end_dynamic_entity_shader();

// Frees the shader set_chunk_fog()/every Chunk's mesh material shares.
// Call once before CloseWindow() - unlike the plain-texture default
// material this replaced, it isn't a raylib-internal resource freed
// automatically by CloseWindow()'s own cleanup.
void unload_chunk_fog_shader();

// CPU-side vertex buffers for one mesh (positions/normals/texcoords/colors,
// flat per-attribute arrays matching raylib's own Mesh layout) - the
// pure-data half of what Chunk::build_mesh_data() builds, before
// Chunk::upload_mesh_data() turns it into an actual GPU Mesh. Safe to build
// on a background thread (see ChunkWorkerPool) since nothing here touches
// raylib/GL state; only upload_mesh_data() does that, and only on the main
// thread (the one holding the GL context).
struct ChunkMeshBuffers {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<unsigned char> colors;
    // Per-vertex (sky, block) light fractions, each 0..1 - uploaded as the
    // mesh's texcoords2 (see Chunk::upload_buffers()) rather than baked
    // into `colors` the way the day/night-independent face-direction
    // shading is, so chunk.fs can combine them with the current
    // daylightFactor uniform itself, per fragment, instead of this mesh
    // needing to be rebuilt whenever the time of day changes.
    std::vector<float> light;

    // Per-vertex ambient occlusion strength (AO_BRIGHTNESS[ao], 0.5..1.0 -
    // 1.0 meaning "no occlusion"), 4 identical copies per vertex - uploaded
    // as the mesh's tangents (unused by chunk meshes otherwise, so it's a
    // free XYZW slot; only .x is actually read - see Chunk::upload_buffers()).
    // Kept separate from `colors`' own baked face-direction shading for the
    // same reason `light` is: chunk.fs blends this dynamically against how
    // directly sunlit a fragment currently is (see its own comment), so
    // real Minecraft-style vertex AO doesn't darken a corner that's
    // actually standing in direct sunlight right now - only true shadow.
    std::vector<float> ao;
};

// One distinct transparent-but-not-translucent BlockType's own mesh data -
// see Chunk::transparent_layer_count()'s own comment for why each type
// present in a chunk needs its own separate mesh rather than sharing one.
struct ChunkMeshTransparentBucket {
    BlockType type;
    ChunkMeshBuffers data;
    float avg_y = 0.0f;
};

// Everything Chunk::build_mesh_data() produces for one chunk: opaque
// geometry, every distinct transparent type present (each its own bucket),
// and water (translucent) geometry - see Chunk::draw()'s own comment for
// why these stay separate meshes instead of merging into one. Crosses from
// a ChunkWorkerPool background thread to the main thread as plain data,
// then Chunk::upload_mesh_data() consumes it there.
struct ChunkMeshBuildResult {
    ChunkMeshBuffers opaque;
    std::vector<ChunkMeshTransparentBucket> transparent;
    ChunkMeshBuffers water;
    float water_avg_y = 0.0f;
};

// A CHUNK_SIZE x CHUNK_HEIGHT x CHUNK_SIZE grid of blocks, positioned in the
// world by GameObject's position (its min corner, not its center). Block
// data builds into a single GPU mesh (build_mesh()) with hidden faces culled
// out, so a whole chunk draws in one call instead of one draw per visible
// block face.
class Chunk : public GameObject {
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

    // Scatters ore veins (Coal/Iron/Gold/Lapis/Redstone/Diamond) plus
    // underground Dirt/Gravel patches through this chunk's already-solid
    // stone, Beta 1.7.3-style Y bands (see ORE_VEINS in the .cpp) - each
    // vein is a small random-walk blob replacing only Stone, so it can
    // never eat into an ore vein or patch generated moments earlier by this
    // same pass. Deterministic per-chunk from (world_seed, chunk_x,
    // chunk_z), same idea as carve_caves()'s own seeding, just without that
    // one's cross-chunk reach - a vein is small enough to just accept never
    // spanning a chunk border. Call after generate_terrain() and before
    // carve_caves(), so a cave carved afterward can naturally expose (or
    // partially destroy) a vein it happens to cut through, instead of
    // veins only ever appearing in solid, unreachable stone.
    void generate_ores(uint32_t world_seed, int chunk_x, int chunk_z);

    // Carves cave tunnels (and, much more rarely, ravines) into this
    // chunk's already-generated terrain, Beta 1.7.3-style: a "Perlin worm"
    // random walk (see the .cpp) rather than the 3D density-function
    // caves modern Minecraft replaced this with in 1.18. A ravine is the
    // same idea carved narrow-and-tall instead of round, starting closer
    // to the surface, and rolled on its own separate rarity so it doesn't
    // just track wherever a cave system happens to roll too. A tunnel (or
    // ravine) can start in a neighboring chunk and wind its way into this
    // one, so `world_seed` and every chunk coordinate within
    // CAVE_CHUNK_RADIUS (Chunk.cpp) of (chunk_x, chunk_z) are re-walked
    // here too, purely as geometry - only the parts of each path landing
    // inside *this* chunk are actually carved. Because each path's shape
    // is a deterministic function of (world_seed, its own origin chunk),
    // not of generation order, a chunk generated later still carves the
    // exact same path a neighbor generated earlier already carved its own
    // half of, so the two halves always line up into one continuous
    // tunnel/ravine. Call after generate_terrain() and before
    // compute_lighting() (so sky/block light correctly floods into the
    // new voids) - never carves Water, Air, or Bedrock, so it can't drain
    // a lake or breach the world floor.
    void carve_caves(uint32_t world_seed, int chunk_x, int chunk_z);

    // Full sky+block light recompute via BFS flood fill. Call after the block
    // layout is set. Incremental (BFS-from-the-change-only) updates for
    // placing/breaking single blocks come with Block Interaction.
    void compute_lighting();

    // Rebuilds the GPU mesh from the current block/light data, skipping any
    // face whose neighbor is opaque - hidden faces never make it into the
    // mesh at all. At the chunk's own edges, that neighbor lives in an
    // adjacent chunk, so the 4 side neighbors are consulted too - without
    // them, every boundary face would be drawn as an (unnecessary, and
    // visibly wrong from inside solid ground) wall. Vertex AO and smooth
    // lighting sample one cell past a face too, which for a corner vertex
    // can land in a diagonal neighbor instead of a side one, so all 8
    // border chunks are taken - any may be null, at the edge of the world,
    // in which case that side reads as open/fully sunlit as before. Call
    // once after generate_terrain()/compute_lighting(), once every
    // neighbor's block data is also ready; call again after any future
    // in-place block edit.
    //
    // A synchronous convenience wrapper around build_mesh_data() +
    // upload_mesh_data() below - main-thread only (see upload_mesh_data()).
    // World's steady-state streaming path no longer calls this directly
    // (see ChunkWorkerPool); it's kept for the handful of call sites that
    // still want one-shot synchronous meshing (World::update_chunk_states_
    // blocking()'s bootstrap path).
    void build_mesh(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                     const Chunk* northwest, const Chunk* northeast,
                     const Chunk* southwest, const Chunk* southeast);

    // Pure-CPU half of what build_mesh() does: scans this chunk's blocks
    // and produces vertex buffers (AO/smooth lighting sampled from the 8
    // given neighbors, hidden faces culled), with no OpenGL calls
    // whatsoever - safe to call from a background thread. The caller is
    // responsible for holding a shared_lock on data_mutex() for this chunk
    // and all 8 neighbors for the duration of this call (ChunkWorkerPool
    // does this before invoking it) - see data_mutex()'s own comment for
    // the full locking discipline this is one half of. const: unlike
    // build_mesh()/upload_mesh_data(), this never touches this chunk's own
    // GPU mesh state, so it's safe to call concurrently with this chunk
    // being drawn.
    ChunkMeshBuildResult build_mesh_data(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                                          const Chunk* northwest, const Chunk* northeast,
                                          const Chunk* southwest, const Chunk* southeast) const;

    // GPU half: uploads build_mesh_data()'s result as this chunk's mesh(es),
    // freeing whatever was uploaded before - the same UnloadMesh-old/
    // UploadMesh-new work build_mesh() has always done, just fed from a
    // result built elsewhere (possibly on another thread) instead of
    // building it inline. Main-thread only - UploadMesh/UnloadMesh need the
    // GL context, which only the main thread holds.
    void upload_mesh_data(ChunkMeshBuildResult&& result);

    // Draws this chunk's opaque and alpha-cutout geometry. Translucent
    // glass/ice/water stay out, while foliage belongs here: its transparent
    // texels are discarded by chunk.fs and visible texels write depth.
    // See draw_transparent_layer()/draw_water(). Kept depth-writing
    // deliberately: this is the only one of the three meshes drawn with
    // normal depth *writes* on, and a blended block's face writing to
    // the depth buffer as if it were solid would incorrectly hide whatever
    // real geometry sits behind it - visible as terrain (or whole chunks,
    // if the transparent surface is large) vanishing when looked at
    // through a window or similar.
    void draw() const override;

    // Every blended transparent block TYPE present in this chunk (glass,
    // ice, ...) gets its OWN separate mesh/"layer" -
    // never merged into one, even though they're drawn with the same GL
    // state - because two different transparent types can both be visible
    // through each other (e.g. a glass pane in front of a foliage block),
    // and with neither writing to the depth buffer (see draw()'s own
    // comment on why not), there's no way to sort them against each other
    // *within* one merged mesh: draw order there is whatever order
    // build_mesh()'s block scan happened to add them in, fixed at mesh-
    // build time and unrelated to the camera's actual, ever-changing
    // position. World::draw() sorts these layers itself instead (using
    // get_transparent_layer_avg_y()) - drawn in its own pass, after every
    // chunk's draw() (opaque geometry world-wide), with alpha blending on
    // and depth *writes* off (still depth *tested*, so solid terrain in
    // front still correctly hides it) - same GL state as draw_water(),
    // just without its flowing-texture shader animation
    // (BlockProperties::translucent is specifically water's own thing).
    size_t transparent_layer_count() const { return transparent_layers.size(); }
    void draw_transparent_layer(size_t index) const;

    // Draws this chunk's translucent geometry (water - see
    // BlockProperties::translucent) - a separate mesh from draw()'s/
    // draw_transparent()'s, built by the same build_mesh() call.
    // World::draw() calls this in its own pass, after every chunk's draw()
    // (opaque geometry world-wide) and with alpha blending enabled, so
    // translucent faces correctly blend over everything solid instead of
    // however they'd happen to interleave with it by draw order alone.
    void draw_water() const;

    // Local-space (0..CHUNK_HEIGHT) average Y of transparent layer `index`
    // (or of draw_water()'s own mesh), computed once by build_mesh() - a
    // cheap per-layer stand-in for "where this content actually sits
    // vertically" so World::draw() can decide, every frame, which of a
    // chunk's several see-through layers (each blended type present,
    // plus water) is closest to the camera *right now* and draw
    // farthest-first. Chunks span the whole world height, so two chunks
    // being different distances apart in the X/Z plane says nothing about
    // which of *this one chunk's own* layers, at very different Y, is
    // nearer - e.g. a glass roof directly over a pond in the same chunk.
    float get_transparent_layer_avg_y(size_t index) const { return transparent_layers[index].avg_y; }
    float get_water_avg_y() const { return water_avg_y; }

    BlockType get_block(int x, int y, int z) const;
    void set_block(int x, int y, int z, BlockType type);

    // Per-instance facing for a directional block (see HorizontalDirection's
    // own comment) - South (blocks.json's own authored default front) if
    // this position was never explicitly set. Sparse (a plain
    // unordered_map, not a parallel CHUNK_SIZE^2*CHUNK_HEIGHT array like
    // `blocks`/`light`) since only a handful of block *types* ever need
    // this at all; set_block() clears any stale entry for a position
    // whenever it changes what block lives there. Persisted by save_to_file()/
    // load_from_file() (chunk file version 3+) as a compact (index,
    // direction) list rather than a full parallel array, for the same
    // sparseness reason.
    HorizontalDirection get_orientation(int x, int y, int z) const;
    void set_orientation(int x, int y, int z, HorizontalDirection direction);

    // Extra per-instance state a shaped/multi-block BlockType needs beyond
    // a facing (door/trapdoor open, trapdoor top/bottom half, door hinge
    // side, chest multiblock pairing, cake bites eaten) - see core/
    // BlockShape.hpp's BlockInstanceState and BlockStateBits (Chunk.cpp) for
    // the bit layout. Same sparse-map shape and same "0 means never set"
    // default as get_orientation() above, kept as its own map since not
    // every shaped block needs a facing *and* this - mixing them would
    // force every reader to unpack bits meaningless to its own BlockType.
    // Persisted by save_to_file()/load_from_file() (chunk file version 4+).
    uint16_t get_block_state(int x, int y, int z) const;
    void set_block_state(int x, int y, int z, uint16_t packed);

    // Exact biome-blended foliage color generated for this local (x, z)
    // column. Used by a broken foliage block so its dropped-item cube keeps
    // the same color instead of reverting to the atlas' gray tint mask.
    Color get_grass_tint(int x, int z) const;
    Color get_foliage_tint(int x, int z) const;

    // FLUID_LEVEL_SOURCE/FLOWING(1-7)/FALLING - see the constants' own
    // comments. Only meaningful where get_block() is Water; garbage
    // (whatever this cell's array slot happens to hold) otherwise, since
    // nothing reads it except code that already checked the block type
    // first (World::compute_fluid_level, Chunk::build_mesh's neighbor
    // checks don't need it at all). Defaults to FLUID_LEVEL_SOURCE (0) for
    // every cell, so terrain-generated water - never explicitly written
    // here - reads as a source without generate_terrain needing to set it
    // itself, matching how a generated body of water in Minecraft is all
    // source blocks.
    uint8_t get_fluid_level(int x, int y, int z) const;
    void set_fluid_level(int x, int y, int z, uint8_t level);

    // max(sky, block) light, 0..15. Public so a neighboring chunk's
    // build_mesh() can sample real light data across a chunk border instead
    // of guessing - bounds-checked to MAX_LIGHT (open, sunlit space)
    // outside this chunk, since there's no neighbor-chunk data to fall back
    // on here; the caller is expected to resolve cross-chunk coordinates
    // itself and call this only with this chunk's own local coordinates.
    int get_light(int x, int y, int z) const;

    // The two channels get_light() itself maxes together - torch/lava light
    // (block) is time-invariant; open-sky light (sky) is the *raw*,
    // always-fully-lit propagated value real Minecraft itself never
    // modifies either - day/night dims how much a cell's sky light actually
    // shows, not the stored value. Public (like get_light()) so World/
    // GameEngine's own day/night-aware queries (World::get_effective_light())
    // and the chunk mesh's own two-channel vertex data (see build_mesh())
    // can read each channel separately instead of only ever seeing them
    // pre-combined.
    int get_sky_light(int x, int y, int z) const;
    int get_block_light(int x, int y, int z) const;

    // World-level relighting needs to flood-fill across several loaded
    // chunks at once. Chunk still owns the packed storage, but World owns
    // cross-border propagation and uses these narrow mutators under
    // data_mutex() to update the raw channels.
    void clear_lighting();
    void set_sky_light(int x, int y, int z, int value);
    void set_block_light(int x, int y, int z, int value);
    int highest_lit_y() const { return highest_block_y; }

    // Guards blocks/light/fluid_level/column biome tints/highest_block_y
    // once this chunk is live in World::chunks and so reachable from more
    // than one thread (ChunkWorkerPool's background workers, alongside the
    // main thread) - not needed, and not touched, while a chunk is still
    // mid-generation and not yet inserted into World::chunks, since nothing
    // else can reach it yet at that point. The actual discipline built on
    // top of this (World.cpp): every post-generation write from the main
    // thread (a block edit, a fluid/falling-block tick's set_block/
    // set_fluid_level, any re-run of compute_lighting()) takes a
    // std::unique_lock on exactly one chunk at a time for the whole
    // mutation; build_mesh_data()'s cross-thread reads take a
    // std::shared_lock, held for that entire call, across this chunk and up
    // to 8 neighbors at once (acquired in ascending chunk-key order, so two
    // concurrent mesh jobs sharing a neighbor can never deadlock against
    // each other). Plain main-thread-only readers (World::get_block,
    // raycast, the debug overlay, ...) take no lock at all - two reads
    // never race, and nothing outside the main thread ever reads through
    // those call paths, so the only real hazard (a background read racing a
    // main-thread write) is exactly what the unique/shared split above
    // prevents.
    std::shared_mutex& data_mutex() const { return mesh_data_mutex; }

    // Always Loaded or Active for a live Chunk (see ChunkState) - World is
    // the only writer, via update_chunk_states()'s transition handling.
    ChunkState get_state() const { return state; }
    void set_state(ChunkState new_state) { state = new_state; }

    // Set by World whenever a block here changes after generation
    // (break_block/place_block), so a future unload can tell a chunk that
    // needs its edits saved apart from one that can just be regenerated.
    // Not acted on yet - see World::unload_chunk's TODO.
    bool is_modified() const { return modified; }
    void mark_modified() { modified = true; }

    // Raw binary dump of everything a reload needs to reconstruct this
    // chunk exactly (blocks, fluid_level, column biome tints,
    // highest_block_y) behind a small magic+version header - not JSON
    // (Json.hpp has no writer, and ~98KB of block IDs has no business
    // being text). Light isn't included - compute_lighting() cheaply
    // rebuilds it after either generation or load, same as it always has.
    // Written to a ".tmp" sibling then renamed into place, so a crash
    // mid-write can't leave a later load reading back a half-written file.
    // Returns false (nothing written) if the file couldn't be opened.
    bool save_to_file(const std::string& path) const;

    // Inverse of save_to_file(): true and every array above overwritten
    // from `path`; false (this Chunk's data is left untouched - the caller
    // discards it and falls back to procedural generation, see
    // World::generate_chunk) if the file is missing, truncated, or its
    // header doesn't match.
    bool load_from_file(const std::string& path);

private:
    static int index(int x, int y, int z);

    // Chunk-local opacity check for light propagation (transparent blocks,
    // including air, let light pass through). Out-of-range counts as open.
    bool is_opaque(int x, int y, int z) const;

    std::array<BlockType, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> blocks;

    // See get_orientation()/set_orientation() above - keyed by the same
    // flat local index() every other per-block array uses.
    std::unordered_map<int, HorizontalDirection> orientation;

    // See get_block_state()/set_block_state() above.
    std::unordered_map<int, uint16_t> block_state;

    // Packed per-cell light: upper nibble = sky light, lower nibble = block
    // light, each 0-15.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> light{};

    // See get_fluid_level()/set_fluid_level() and the FLUID_LEVEL_*
    // constants above.
    std::array<uint8_t, CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE> fluid_level{};

    // Grass top tint for each (x, z) column, blended across every grass-
    // bearing biome's own tint by that column's BiomeWeights when
    // generate_terrain() ran - a smooth gradient instead of a hard color
    // switch at a biome border, the same way terrain height itself blends.
    // Recomputing this from noise again at mesh-build time would work just
    // as well, but this is a 256-entry lookup instead of another noise
    // sample per grass-top face. build_mesh() uses it in place of the one
    // fixed color a normal block's texture_tints would give every instance
    // of Grass.
    std::array<Color, CHUNK_SIZE * CHUNK_SIZE> column_grass_tint{};

    // Foliage equivalent of column_grass_tint. Stored separately because
    // Minecraft-style leaves use their own biome palette, even though both
    // colors are blended from the same BiomeWeights for seamless borders.
    std::array<Color, CHUNK_SIZE * CHUNK_SIZE> column_foliage_tint{};

    // See data_mutex()'s own comment for the locking discipline this backs.
    // mutable: a const reader (build_mesh_data()) still needs to lock it.
    mutable std::shared_mutex mesh_data_mutex;

    // Highest Y with a non-Air block anywhere in this chunk (generation
    // tracks it; set_block() only ever raises it, never lowers it - cheap
    // and safe, since overestimating just means scanning a few extra
    // guaranteed-air rows, while underestimating would hide real blocks).
    // With CHUNK_HEIGHT now 256 but actual terrain rarely reaching much
    // above 90, build_mesh() and compute_lighting() use this to skip the
    // rest of the column instead of always walking the full 256, since it's
    // guaranteed air up there - see get_sky_light() for the one place that
    // needs to know the difference between "above this" and "out of chunk".
    int highest_block_y = 0;

    // vertexCount == 0 (and mesh_uploaded == false) until build_mesh() runs.
    Mesh mesh{};
    bool mesh_uploaded = false;

    // One entry per distinct transparent-but-not-translucent BlockType
    // present in this chunk (glass, ice, ...) - see
    // transparent_layer_count()'s own comment for why these can't just be
    // merged into one mesh the way opaque blocks are.
    struct TransparentLayer {
        Mesh mesh{};
        bool uploaded = false;
        float avg_y = 0.0f;
    };
    std::vector<TransparentLayer> transparent_layers;

    // Translucent geometry (water), built and drawn separately from `mesh`
    // - see draw_water().
    Mesh water_mesh{};
    bool water_mesh_uploaded = false;
    float water_avg_y = 0.0f;

    // A live Chunk is always at least Loaded (see ChunkState) - Unloaded is
    // never stored, only reported by World for a coordinate with no Chunk.
    ChunkState state = ChunkState::Loaded;
    bool modified = false;
};
