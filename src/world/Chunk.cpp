#include "world/Chunk.hpp"
#include "core/BlockShape.hpp"
#include "core/TerrainNoise.hpp"

#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <vector>

namespace {
    constexpr float HALF = 0.5f;

    // Terrain shape: a wavelength-~96-block rolling hill signal, layered 4
    // octaves deep for detail (Beta 1.7.3-style layering: this is itself a
    // sum of multiple noise octaves, then World Generation layers biome
    // selection - a whole separate pair of noise maps, see TerrainNoise -
    // on top of that). Mapped onto a height band centered on each biome's
    // own BASE_HEIGHT (comfortably inside the chunk's own local
    // 0..CHUNK_HEIGHT-1 column). Chunk stays plainly 0-based internally
    // (see MIN_WORLD_Y in Chunk.hpp) - BiomeTerrain's heights below are
    // LOCAL, offset by -MIN_WORLD_Y (+64) from the world-space heights
    // they're meant to represent, so e.g. a base_height of 6 there means
    // world Y ~70, not local Y 70.
    constexpr float NOISE_FREQUENCY = 1.0f / 96.0f;
    constexpr int NOISE_OCTAVES = 4;

    // Sea level: any column whose terrain height falls below this fills the
    // gap with water up to it. World Y 64, same idea (and the same absolute
    // coordinate) as Minecraft's own sea level.
    constexpr int WATER_LEVEL = 64 - MIN_WORLD_Y;

    // A source/falling water cell renders just below a full cube, while
    // flowing levels 1..7 step down toward the far edge of an 8-block flow.
    // This is visual only: collision/fluid logic still treats Water as the
    // same grid cell and reads Chunk::fluid_level for its state.
    constexpr float WATER_SOURCE_SURFACE_HEIGHT = 14.0f / 16.0f;
    constexpr float WATER_MIN_FLOW_SURFACE_HEIGHT = 2.0f / 16.0f;

    // Per-biome terrain shape and surface/subsurface blocks - the same
    // overall column structure (bedrock, subsurface, surface, water/air)
    // for every biome, just with each biome's own numbers and blocks
    // dropped in, matching Beta 1.7.3's approach of biomes carrying both a
    // look (surface blocks) and a characteristic terrain shape rather than
    // just a color. Deliberately gentler than modern Minecraft's Extreme
    // Hills (Beta 1.7.3 predates that terrain rework) - even Hills here
    // stays well short of dramatic cliffs. base_height/height_variation
    // aren't used directly any more (see generate_terrain: every biome's
    // numbers are blended by BiomeWeights instead of picking just one) -
    // surface_block/subsurface_block/surface_depth still are, for whichever
    // biome ends up dominant at a given column.
    struct BiomeTerrain {
        int base_height;             // local; see the comment above on the +64 offset
        int height_variation;        // +/- around base_height (an amplitude, not itself a height - no offset needed)
        int surface_depth;           // layers of subsurface_block just under the surface block
        BlockType surface_block;     // the single block at the very top of the column
        BlockType subsurface_block;
    };

    constexpr int DEFAULT_SURFACE_DEPTH = 3;

    BiomeTerrain biome_terrain(Biome biome) {
        switch (biome) {
            case Biome::Ocean:
                // Deep and mostly flat - world Y ~10..30, i.e. up to ~54
                // blocks below sea level (64) at its deepest, so there's
                // real deep water for World::draw's underwater fog override
                // to darken toward at its own deepest (see
                // UNDERWATER_FOG_MAX_DEPTH in World.cpp). Gravel throughout,
                // same as Sea's own floor (see the "дно" requirement) - the
                // difference between the two is depth and beach material,
                // not the floor itself.
                return {20 - MIN_WORLD_Y, 10, 4, BlockType::Gravel, BlockType::Gravel};
            case Biome::Sea:
                // Shallow and calm - world Y ~56..64, mostly right at sea
                // level, so land slopes gently down into it (see the beach
                // override in generate_terrain for the actual shoreline
                // strip) instead of dropping to Ocean's deep floor.
                return {60 - MIN_WORLD_Y, 4, 3, BlockType::Gravel, BlockType::Gravel};
            case Biome::Desert:
                // Flat and dry, with deeper sand than other biomes' subsurface
                // layer before hitting stone - world Y ~56..68, mostly right
                // around sea level.
                return {62 - MIN_WORLD_Y, 6, 6, BlockType::Sand, BlockType::Sand};
            case Biome::Forest:
                // Mostly a smooth, gentle roll (closer to Plains than to
                // Hills) - world Y ~62..82.
                return {72 - MIN_WORLD_Y, 10, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
            case Biome::Hills:
                // The roughest terrain this generates - world Y ~50..114 -
                // but still gentle by modern-Minecraft standards, on purpose.
                // Its tallest peaks break through HILLS_STONE_LINE into bare
                // stone (see generate_terrain), so surface_block here only
                // actually shows up below that line.
                return {82 - MIN_WORLD_Y, 32, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
            case Biome::Plains:
            default:
                // The gentlest biome - world Y ~58..78.
                return {68 - MIN_WORLD_Y, 10, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
        }
    }

    // Above this world height, Hills' surface turns to bare stone instead
    // of grass/dirt - a simple tree-line/rocky-peak effect for its tallest
    // terrain, the one place this generator lets a biome's own surface
    // block depend on height rather than purely on position.
    constexpr int HILLS_STONE_LINE = 95 - MIN_WORLD_Y;

    // Rivers: TerrainNoise::river() is an ordinary noise field (roughly
    // [-1, 1]) - wherever its *absolute value* drops under RIVER_WIDTH, the
    // blended height above is pulled down toward RIVER_BED, tracing that
    // field's zero-contour the way a real river winds rather than running
    // straight. RIVER_WIDTH is in noise units, not blocks - TerrainNoise's
    // own RIVER_FREQUENCY is what actually sets the width in blocks.
    constexpr float RIVER_WIDTH = 0.04f;
    constexpr int RIVER_BED = WATER_LEVEL - 3; // a few blocks under sea level, so a river reliably fills with water

    // A river only carves the *surface* down into a valley when its bed is
    // within this many blocks of the natural (pre-river) terrain height.
    // Where the land is already taller than that above RIVER_BED - a ridge
    // the river's course happens to cross - generate_terrain leaves the
    // surface alone and instead tunnels a flooded channel through the rock
    // at RIVER_BED's own elevation, the same river continuing underground
    // rather than cutting an ever-deeper canyon to stay at the surface.
    constexpr int RIVER_TUNNEL_DEPTH = 10;
    constexpr int RIVER_TUNNEL_HALF_HEIGHT = 3; // the underground channel is this many blocks tall above and below RIVER_BED

    // Clay: TerrainNoise::clay() is a small-scale noise field: wherever it
    // crosses above CLAY_THRESHOLD, a patch of otherwise-Sand surface
    // (always underwater - see generate_terrain) becomes Clay instead,
    // matching real Minecraft's small shallow-water clay deposits. Only
    // ever replaces the surface block itself, not whatever's under it, so
    // a patch reads as a thin clay deposit sitting in the sand rather than
    // a solid clay column.
    constexpr float CLAY_THRESHOLD = 0.55f;

    // River bed: wherever the river carve above (see `carve` in
    // generate_terrain) is strong enough that this column sits solidly in
    // the channel rather than on its sloped bank, the bed gets sand
    // instead of whatever the land biome's own surface block would
    // otherwise leave exposed underwater (plain dirt for Plains/Forest) -
    // real rivers run over sand/gravel, not soil. GRAVEL_THRESHOLD then
    // swaps some of that sand to gravel in small patches, same mechanism
    // as CLAY_THRESHOLD above.
    constexpr float RIVER_BED_CARVE_THRESHOLD = 0.3f;
    constexpr float GRAVEL_THRESHOLD = 0.6f;

    // Beach: land within a few blocks of sea level, close enough to Sea or
    // Ocean to notice, gets a shoreline material instead of its own
    // biome's usual surface block - sand for a calm Sea coastline, gravel
    // for a "wild" Ocean one with no Sea buffer, matching how the two
    // differ everywhere else (Sea = calm/sandy, Ocean = deep/rocky).
    constexpr int BEACH_HEIGHT_ABOVE_WATER = 3;
    constexpr float BEACH_COAST_WEIGHT = 0.05f; // how much Sea+Ocean weight counts as "close enough" to be a coast

    // Grass top tint per biome - same idea as Minecraft's own per-biome
    // grass color, applied here since the block's own texture tile is a
    // deliberately colorless overlay (see blocks.json's grass "color").
    // Forest and Hills deliberately share the same, slightly darker green;
    // Plains reads noticeably lighter. Desert/Ocean/Sea never generate a
    // Grass block at all, so they don't need their own tint.
    constexpr Color PLAINS_GRASS_TINT = {180, 220, 130, 255};
    constexpr Color FOREST_GRASS_TINT = {124, 189, 107, 255}; // also Hills

    // Foliage has its own palette instead of borrowing the grass color:
    // brighter/yellower in Plains, lush green in Forest, and cooler in
    // Hills. The source leaves tile supplies the detail/alpha cutout;
    // these colors only provide the biome-dependent tint.
    constexpr Color PLAINS_FOLIAGE_TINT = {145, 205, 92, 255};
    constexpr Color FOREST_FOLIAGE_TINT = {82, 180, 82, 255};
    constexpr Color HILLS_FOLIAGE_TINT = {102, 168, 112, 255};

    // Blends the two grass tints above by how much Plains vs. Forest/Hills
    // influence this column, so a border between them fades the color
    // gradually instead of switching at whichever point dominant_biome()
    // happens to flip - the same idea as blending terrain height itself.
    // Desert/Ocean/Sea weight is deliberately excluded from the average
    // (rather than fading grass toward some meaningless "tint" for sand or
    // water): those biomes just don't produce a Grass block, so whatever
    // this returns for a fully-desert/ocean/sea column is never actually
    // used.
    Color grass_tint_for_weights(const BiomeWeights& weights) {
        float grass_total = weights.plains + weights.forest + weights.hills;
        if (grass_total < 0.0001f) {
            return FOREST_GRASS_TINT;
        }
        float plains_share = weights.plains / grass_total;
        float forest_share = 1.0f - plains_share; // forest + hills, same tint
        auto blend = [&](unsigned char plains_channel, unsigned char forest_channel) {
            return static_cast<unsigned char>(std::lround(
                plains_share * plains_channel + forest_share * forest_channel));
        };
        return {
            blend(PLAINS_GRASS_TINT.r, FOREST_GRASS_TINT.r),
            blend(PLAINS_GRASS_TINT.g, FOREST_GRASS_TINT.g),
            blend(PLAINS_GRASS_TINT.b, FOREST_GRASS_TINT.b),
            255,
        };
    }

    Color foliage_tint_for_weights(const BiomeWeights& weights) {
        float foliage_total = weights.plains + weights.forest + weights.hills;
        if (foliage_total < 0.0001f) return FOREST_FOLIAGE_TINT;

        auto blend = [&](unsigned char plains, unsigned char forest, unsigned char hills) {
            return static_cast<unsigned char>(std::lround(
                (weights.plains * plains + weights.forest * forest + weights.hills * hills) /
                foliage_total));
        };
        return {
            blend(PLAINS_FOLIAGE_TINT.r, FOREST_FOLIAGE_TINT.r, HILLS_FOLIAGE_TINT.r),
            blend(PLAINS_FOLIAGE_TINT.g, FOREST_FOLIAGE_TINT.g, HILLS_FOLIAGE_TINT.g),
            blend(PLAINS_FOLIAGE_TINT.b, FOREST_FOLIAGE_TINT.b, HILLS_FOLIAGE_TINT.b),
            255,
        };
    }

    // AO level (0..3, from vertex_ao) -> brightness multiplier.
    constexpr float AO_BRIGHTNESS[4] = {0.5f, 0.65f, 0.8f, 1.0f};

    // Per-face directional shading (top/bottom/north-south/east-west) now
    // lives in Block.hpp's FACE_DIRECTION_SHADE - shared with every other
    // place that renders a block-textured cube (dropped items, falling
    // blocks), not just chunk meshing.

    struct Face {
        Vector3 v1, v2, v3, v4; // corners, CCW as seen from outside
        Vector3 normal;
    };

    // Six faces of a box centered on the origin with the given per-axis
    // half-extents, indexed by BlockFace (Top, Bottom, North, South, East,
    // West - North/South are -Z/+Z, East/West are +X/-X). CUBE_FACES below
    // is just this at {HALF,HALF,HALF}; Chunk::build_mesh_data()'s Shaped
    // branch calls it again per box, with that shaped block's own (usually
    // non-uniform, e.g. a stair's half-height slab) extents instead, so a
    // stair/trapdoor/door/bed/cake's mini-cube pieces share exactly the
    // same corner-winding/normal logic as an ordinary full block.
    std::array<Face, 6> unit_cube_faces(Vector3 half) {
        return {{
            { {-half.x,  half.y, -half.z}, {-half.x,  half.y,  half.z}, { half.x,  half.y,  half.z}, { half.x,  half.y, -half.z}, { 0.0f,  1.0f,  0.0f} }, // Top
            { {-half.x, -half.y,  half.z}, {-half.x, -half.y, -half.z}, { half.x, -half.y, -half.z}, { half.x, -half.y,  half.z}, { 0.0f, -1.0f,  0.0f} }, // Bottom
            { {-half.x,  half.y, -half.z}, { half.x,  half.y, -half.z}, { half.x, -half.y, -half.z}, {-half.x, -half.y, -half.z}, { 0.0f,  0.0f, -1.0f} }, // North
            { { half.x,  half.y,  half.z}, {-half.x,  half.y,  half.z}, {-half.x, -half.y,  half.z}, { half.x, -half.y,  half.z}, { 0.0f,  0.0f,  1.0f} }, // South
            { { half.x,  half.y, -half.z}, { half.x,  half.y,  half.z}, { half.x, -half.y,  half.z}, { half.x, -half.y, -half.z}, { 1.0f,  0.0f,  0.0f} }, // East
            { {-half.x,  half.y,  half.z}, {-half.x,  half.y, -half.z}, {-half.x, -half.y, -half.z}, {-half.x, -half.y,  half.z}, {-1.0f,  0.0f,  0.0f} }, // West
        }};
    }

    // Indexed by BlockFace (Top, Bottom, North, South, East, West). North/South
    // are -Z/+Z, East/West are +X/-X.
    const std::array<Face, 6> CUBE_FACES = unit_cube_faces({HALF, HALF, HALF});

    // Cross-shaped vegetation: two diagonal planes, each emitted in both
    // directions because the opaque/cutout pass keeps back-face culling on.
    const std::array<Face, 4> CROSS_FACES = {{
        { {-HALF, HALF,-HALF}, { HALF, HALF, HALF}, { HALF,-HALF, HALF}, {-HALF,-HALF,-HALF}, { 0.7071f, 0.0f,-0.7071f} },
        { { HALF, HALF, HALF}, {-HALF, HALF,-HALF}, {-HALF,-HALF,-HALF}, { HALF,-HALF, HALF}, {-0.7071f, 0.0f, 0.7071f} },
        { { HALF, HALF,-HALF}, {-HALF, HALF, HALF}, {-HALF,-HALF, HALF}, { HALF,-HALF,-HALF}, { 0.7071f, 0.0f, 0.7071f} },
        { {-HALF, HALF, HALF}, { HALF, HALF,-HALF}, { HALF,-HALF,-HALF}, {-HALF,-HALF, HALF}, {-0.7071f, 0.0f,-0.7071f} },
    }};

    // The 9 chunks (this one plus its 8 border neighbors) a face-corner
    // AO/light sample might land in. A face's own normal steps one cell
    // past one edge; a diagonal corner-of-corner sample (used for AO/light
    // at a convex vertex) steps past two edges at once when the block is
    // also at the chunk's edge on that other axis, landing in a diagonal
    // neighbor rather than a side one. Any entry may be null - the edge of
    // the loaded world - same as a missing side neighbor.
    struct Neighborhood {
        const Chunk* self;
        const Chunk* west, *east, *north, *south;
        const Chunk* northwest, *northeast, *southwest, *southeast;

        // Rewrites a possibly out-of-range (x, z) in place to the same cell
        // expressed in whichever of the above 9 chunks actually owns it,
        // and returns that chunk (nullptr if it isn't loaded). `y` never
        // crosses a chunk boundary (no vertical chunk stacking yet) and
        // isn't touched here.
        const Chunk* resolve(int& x, int& z) const {
            int dx = (x < 0) ? -1 : (x >= CHUNK_SIZE ? 1 : 0);
            int dz = (z < 0) ? -1 : (z >= CHUNK_SIZE ? 1 : 0);
            if (dx != 0) x += (dx < 0) ? CHUNK_SIZE : -CHUNK_SIZE;
            if (dz != 0) z += (dz < 0) ? CHUNK_SIZE : -CHUNK_SIZE;

            if (dx == 0 && dz == 0) return self;
            if (dx == 0) return dz < 0 ? north : south;
            if (dz == 0) return dx < 0 ? west : east;
            if (dx < 0) return dz < 0 ? northwest : southwest;
            return dz < 0 ? northeast : southeast;
        }
    };

    // AO occupancy check, resolved through a Neighborhood so a sample that
    // steps outside the chunk being meshed reads the real neighbor chunk's
    // blocks instead of the chunk-local "nothing out there" default.
    // Out-of-range on y (no vertical stacking) or a missing neighbor chunk
    // still counts as not solid, same as before.
    bool solid_at(const Neighborhood& nb, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return false;
        const Chunk* chunk = nb.resolve(x, z);
        if (chunk == nullptr) return false;
        return get_block_properties(chunk->get_block(x, y, z)).solid;
    }

    // Light lookup, resolved the same way - a sample that steps outside the
    // chunk being meshed reads the neighbor's real computed light instead
    // of assuming full sky light. Out-of-range on y or a missing neighbor
    // still reads as open, sunlit space, same as before - sky light there
    // is MAX_LIGHT (unlit is never "outside the world"'s own fault), block
    // light is 0 (nothing out there emitting any). Split into two channels
    // (rather than one light_at() the old single-channel mesh used) so
    // day/night dimming - applied per-fragment in chunk.fs, from the same
    // two channels carried all the way to the GPU - can scale only the sky
    // contribution, never block light (torches, lava).
    int sky_light_at(const Neighborhood& nb, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return MAX_LIGHT;
        const Chunk* chunk = nb.resolve(x, z);
        if (chunk == nullptr) return MAX_LIGHT;
        return chunk->get_sky_light(x, y, z);
    }
    int block_light_at(const Neighborhood& nb, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return 0;
        const Chunk* chunk = nb.resolve(x, z);
        if (chunk == nullptr) return 0;
        return chunk->get_block_light(x, y, z);
    }

    // The 4 cells relevant to one face-corner's vertex: the cell right
    // outside the face, the two edge-adjacent ("side") cells, and the
    // diagonal ("corner") cell. AO and vertex light both sample these.
    struct NeighborCells {
        int base[3], side1[3], side2[3], corner[3];
    };

    NeighborCells compute_neighbor_cells(int x, int y, int z, Vector3 normal, Vector3 corner) {
        int n[3] = { static_cast<int>(normal.x), static_cast<int>(normal.y), static_cast<int>(normal.z) };
        int c[3] = { corner.x > 0.0f ? 1 : -1, corner.y > 0.0f ? 1 : -1, corner.z > 0.0f ? 1 : -1 };

        // The two axes tangent to the face (i.e. not the normal's axis).
        int axis1 = -1, axis2 = -1;
        for (int axis = 0; axis < 3; ++axis) {
            if (n[axis] == 0) { if (axis1 == -1) axis1 = axis; else axis2 = axis; }
        }

        NeighborCells cells;
        cells.base[0] = x + n[0];
        cells.base[1] = y + n[1];
        cells.base[2] = z + n[2];

        cells.side1[0] = cells.base[0];
        cells.side1[1] = cells.base[1];
        cells.side1[2] = cells.base[2];
        cells.side1[axis1] += c[axis1];

        cells.side2[0] = cells.base[0];
        cells.side2[1] = cells.base[1];
        cells.side2[2] = cells.base[2];
        cells.side2[axis2] += c[axis2];

        cells.corner[0] = cells.side1[0];
        cells.corner[1] = cells.side1[1];
        cells.corner[2] = cells.side1[2];
        cells.corner[axis2] += c[axis2];

        return cells;
    }

    // Minecraft-style vertex AO: 0 (darkest) to 3 (no occlusion), based on the
    // two blocks sharing this face-corner's edges and the one at its diagonal.
    int vertex_ao(const Neighborhood& nb, int x, int y, int z, Vector3 normal, Vector3 corner) {
        NeighborCells cells = compute_neighbor_cells(x, y, z, normal, corner);

        bool s1 = solid_at(nb, cells.side1[0] , cells.side1[1] , cells.side1[2] );
        bool s2 = solid_at(nb, cells.side2[0] , cells.side2[1] , cells.side2[2] );
        bool cc = solid_at(nb, cells.corner[0], cells.corner[1], cells.corner[2]);

        // Two occupied edge-neighbors darken a vertex fully, even if the corner
        // is empty - otherwise convex corners get a visible bright seam.
        if (s1 && s2) return 0;
        return 3 - (static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cc));
    }

    // Average sky/block light (each 0..1, raw - NOT floored at
    // MIN_LIGHT_FRACTION here) of the same three neighbor cells used for
    // AO, plus the cell right outside the face - the same per-vertex
    // sampling Minecraft calls "smooth lighting", just kept as two separate
    // channels (see sky_light_at()/block_light_at()'s own comment) instead
    // of one pre-combined value, all the way through to the GPU. The floor
    // is applied once, in chunk.fs, *after* both the day/night combine and
    // the brightness slider's own gamma curve - flooring each raw channel
    // this early would let a torch's own genuine block light get
    // needlessly gamma-darkened too (see chunk.fs's own comment on why
    // block light stays completely exempt from the brightness slider).
    struct VertexLight { float sky, block; };
    VertexLight vertex_light(const Neighborhood& nb, int x, int y, int z, Vector3 normal, Vector3 corner) {
        NeighborCells cells = compute_neighbor_cells(x, y, z, normal, corner);

        int sky_total = sky_light_at(nb, cells.base[0]  , cells.base[1]  , cells.base[2]  )
                      + sky_light_at(nb, cells.side1[0] , cells.side1[1] , cells.side1[2] )
                      + sky_light_at(nb, cells.side2[0] , cells.side2[1] , cells.side2[2] )
                      + sky_light_at(nb, cells.corner[0], cells.corner[1], cells.corner[2]);
        int block_total = block_light_at(nb, cells.base[0]  , cells.base[1]  , cells.base[2]  )
                        + block_light_at(nb, cells.side1[0] , cells.side1[1] , cells.side1[2] )
                        + block_light_at(nb, cells.side2[0] , cells.side2[1] , cells.side2[2] )
                        + block_light_at(nb, cells.corner[0], cells.corner[1], cells.corner[2]);

        return {
            (sky_total / 4.0f) / MAX_LIGHT,
            (block_total / 4.0f) / MAX_LIGHT,
        };
    }

    // One of these per distinct transparent-but-not-translucent BlockType
    // encountered while building a chunk's mesh (see
    // Chunk::transparent_layer_count()'s own comment for why they can't
    // share one mesh) - `y_sum`/`y_count` become that layer's own avg_y
    // once the block scan finishes, same idea as water's. A build-time-only
    // accumulator (kept local to this .cpp, unlike ChunkMeshBuildResult's
    // own ChunkMeshTransparentBucket in Chunk.hpp, which only needs the
    // already-finished avg_y) - build_mesh_data() converts each of these to
    // one of those once the double-precision sum/count above are no longer
    // needed.
    struct TransparentBuildBucket {
        BlockType type;
        ChunkMeshBuffers data;
        double y_sum = 0.0;
        int y_count = 0;
    };

    // Linear search rather than a hash map: a chunk realistically has a
    // handful of distinct transparent types at most, so this is cheaper
    // (and simpler) than hashing BlockType for every transparent block.
    TransparentBuildBucket& bucket_for(std::vector<TransparentBuildBucket>& buckets, BlockType type) {
        for (TransparentBuildBucket& bucket : buckets) {
            if (bucket.type == type) return bucket;
        }
        buckets.push_back(TransparentBuildBucket{type});
        return buckets.back();
    }

    float water_surface_height(uint8_t level)
    {
        if (level == FLUID_LEVEL_SOURCE || level == FLUID_LEVEL_FALLING) {
            return WATER_SOURCE_SURFACE_HEIGHT;
        }
        float t = static_cast<float>(std::clamp<uint8_t>(level, 1, FLUID_LEVEL_MAX_FLOW) - 1) /
                  static_cast<float>(FLUID_LEVEL_MAX_FLOW - 1);
        return WATER_SOURCE_SURFACE_HEIGHT +
               (WATER_MIN_FLOW_SURFACE_HEIGHT - WATER_SOURCE_SURFACE_HEIGHT) * t;
    }

    BoundingBox face_rect_for_box(const BoundingBox& box, BlockFace face)
    {
        switch (face) {
            case BlockFace::Top:
            case BlockFace::Bottom:
                return {{box.min.x, 0.0f, box.min.z}, {box.max.x, 0.0f, box.max.z}};
            case BlockFace::North:
            case BlockFace::South:
                return {{box.min.x, box.min.y, 0.0f}, {box.max.x, box.max.y, 0.0f}};
            case BlockFace::East:
            case BlockFace::West:
                return {{0.0f, box.min.y, box.min.z}, {0.0f, box.max.y, box.max.z}};
        }
        return {};
    }

    float face_plane_for_box(const BoundingBox& box, BlockFace face)
    {
        switch (face) {
            case BlockFace::Top:    return box.max.y;
            case BlockFace::Bottom: return box.min.y;
            case BlockFace::North:  return box.min.z;
            case BlockFace::South:  return box.max.z;
            case BlockFace::East:   return box.max.x;
            case BlockFace::West:   return box.min.x;
        }
        return 0.0f;
    }

    bool shaped_face_on_cell_boundary(const BoundingBox& box, BlockFace face)
    {
        constexpr float EPS = 0.0001f;
        switch (face) {
            case BlockFace::Top:    return box.max.y >= 1.0f - EPS;
            case BlockFace::Bottom: return box.min.y <= EPS;
            case BlockFace::North:  return box.min.z <= EPS;
            case BlockFace::South:  return box.max.z >= 1.0f - EPS;
            case BlockFace::East:   return box.max.x >= 1.0f - EPS;
            case BlockFace::West:   return box.min.x <= EPS;
        }
        return false;
    }

    // Appends one face as two triangles (0,1,2) and (0,2,3) - the same quad,
    // split for a Mesh's plain (non-quad) triangle list. `tint` (typically
    // WHITE) is multiplied into each vertex color alongside AO/light
    // brightness - see BlockProperties::texture_tints for why a face would
    // ever need anything other than white. `top_drop` lowers this face's own
    // upward-facing corners (any corner at local y > 0, i.e. Top's own 4
    // corners, or a side face's top edge - never Bottom's, which are all at
    // y < 0) by that many world units. `uv_v0/uv_v1` crop the face's
    // vertical texture range without stretching, the same idea shaped
    // blocks use through crop_tile_to_box().
    // `shade` is the day/night-*independent* part of a corner's brightness
    // (AO * FACE_DIRECTION_SHADE, or plain 1.0 for cross-shaped foliage,
    // which has neither) - baked straight into mesh_data.colors, same as
    // the old single-channel brightness always was. `sky_fraction`/
    // `block_fraction` are vertex_light()'s own two channels, carried
    // through as a second per-vertex attribute (mesh_data.light, uploaded
    // as the mesh's texcoords2 - see upload_buffers()) instead: chunk.fs
    // combines them with the current daylightFactor uniform itself, per
    // fragment, so day/night dims only the sky contribution without ever
    // needing this mesh rebuilt when the time of day changes.
    void append_custom_face(ChunkMeshBuffers& mesh_data, const Vector3 corners[4], Vector3 normal,
                            Vector3 center, const float u[4], const float v[4],
                            const float shade[4], const float sky_fraction[4], const float block_fraction[4],
                            const float ao[4], Color tint) {
        static constexpr int TRIANGLE[6] = {0, 1, 2, 0, 2, 3};
        for (int corner : TRIANGLE) {
            mesh_data.positions.push_back(center.x + corners[corner].x);
            mesh_data.positions.push_back(center.y + corners[corner].y);
            mesh_data.positions.push_back(center.z + corners[corner].z);

            mesh_data.normals.push_back(normal.x);
            mesh_data.normals.push_back(normal.y);
            mesh_data.normals.push_back(normal.z);

            mesh_data.texcoords.push_back(u[corner]);
            mesh_data.texcoords.push_back(v[corner]);

            mesh_data.colors.push_back(static_cast<unsigned char>(shade[corner] * tint.r));
            mesh_data.colors.push_back(static_cast<unsigned char>(shade[corner] * tint.g));
            mesh_data.colors.push_back(static_cast<unsigned char>(shade[corner] * tint.b));
            // Not scaled by shade, unlike the color channels - alpha is
            // this face's opacity (see BlockProperties::translucent), not
            // part of its shading.
            mesh_data.colors.push_back(tint.a);

            mesh_data.light.push_back(sky_fraction[corner]);
            mesh_data.light.push_back(block_fraction[corner]);

            // 4 identical copies - raylib's tangents are XYZW per vertex
            // and only .x is actually read (see chunk.fs), but the buffer
            // still has to be the full 4 floats/vertex upload_buffers()
            // validates against.
            mesh_data.ao.push_back(ao[corner]);
            mesh_data.ao.push_back(ao[corner]);
            mesh_data.ao.push_back(ao[corner]);
            mesh_data.ao.push_back(ao[corner]);
        }
    }

    void append_face(ChunkMeshBuffers& mesh_data, const Face& face, Vector3 center, Rectangle uv,
                      const float shade[4], const float sky_fraction[4], const float block_fraction[4],
                      const float ao[4], Color tint, float top_drop = 0.0f,
                      float uv_v0 = 0.0f, float uv_v1 = 1.0f) {
        uv = get_sample_safe_block_uv(uv);
        Vector3 corners[4] = {face.v1, face.v2, face.v3, face.v4};
        if (top_drop != 0.0f) {
            for (Vector3& corner : corners) {
                if (corner.y > 0.0f) corner.y -= top_drop;
            }
        }
        // V=0 is the image's top row (raylib doesn't flip on load), so the
        // top edge of the face (corners 0, 1) must sample V=0, not V=1.
        float u[4] = {uv.x,              uv.x + uv.width, uv.x + uv.width, uv.x};
        float v_top = uv.y + uv.height * uv_v0;
        float v_bottom = uv.y + uv.height * uv_v1;
        float v[4] = {v_top, v_top, v_bottom, v_bottom};
        append_custom_face(mesh_data, corners, face.normal, center, u, v,
                           shade, sky_fraction, block_fraction, ao, tint);
    }

    // Loaded once by load_chunk_shader() (called from GameEngine's
    // constructor, alongside Load_block_definitions()/FontManager::get()) -
    // not lazily, so it's clear from the startup sequence exactly when the
    // GL context it needs is required to already exist, same as those.
    // assets/shaders/chunk.{vs,fs}: raylib's own default mesh shader
    // (texture*vertexColor, so AO/tint already baked into vertex colors by
    // Chunk::build_mesh keeps working unchanged) plus linear distance fog.
    Shader chunk_shader{};

    // Every chunk's mesh samples the same block texture atlas, so they all
    // share one Material - built lazily so it's only touched once
    // Load_block_definitions() (and so the atlas texture) has already run.
    Material& get_chunk_material() {
        static Material material = [] {
            Material m = LoadMaterialDefault();
            SetMaterialTexture(&m, MATERIAL_MAP_DIFFUSE, get_block_atlas_texture());
            m.shader = chunk_shader;
            return m;
        }();
        return material;
    }

    // Copies a std::vector into a malloc'd buffer sized to match - Mesh
    // fields must be malloc-compatible since UnloadMesh() frees them with
    // RL_FREE (== free() with raylib's default allocator).
    template <typename T>
    T* to_mesh_buffer(const std::vector<T>& data) {
        T* buffer = static_cast<T*>(std::malloc(data.size() * sizeof(T)));
        std::memcpy(buffer, data.data(), data.size() * sizeof(T));
        return buffer;
    }

    // The only CPU -> GPU path for chunk geometry. Keeping validation and
    // raylib's exact attribute layout here prevents opaque/transparent/
    // water meshes from silently drifting into incompatible buffer shapes.
    bool upload_buffers(Mesh& mesh, const ChunkMeshBuffers& buffers) {
        const size_t vertex_count = buffers.positions.size() / 3;
        if (buffers.positions.size() % 3 != 0 ||
            buffers.normals.size() != vertex_count * 3 ||
            buffers.texcoords.size() != vertex_count * 2 ||
            buffers.colors.size() != vertex_count * 4 ||
            buffers.light.size() != vertex_count * 2 ||
            buffers.ao.size() != vertex_count * 4 ||
            vertex_count % 3 != 0) {
            TraceLog(LOG_ERROR, "Rejected malformed chunk mesh buffers");
            return false;
        }
        if (vertex_count == 0) return false;

        mesh.vertexCount = static_cast<int>(vertex_count);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = to_mesh_buffer(buffers.positions);
        mesh.normals = to_mesh_buffer(buffers.normals);
        mesh.texcoords = to_mesh_buffer(buffers.texcoords);
        mesh.colors = to_mesh_buffer(buffers.colors);
        mesh.texcoords2 = to_mesh_buffer(buffers.light);
        mesh.tangents = to_mesh_buffer(buffers.ao);
        UploadMesh(&mesh, false);
        return true;
    }
}

void load_chunk_shader()
{
    chunk_shader = LoadShader(ASSETS_PATH "shaders/chunk.vs", ASSETS_PATH "shaders/chunk.fs");
}

void set_chunk_fog(Vector3 camera_position, Color fog_color, Color fog_sky_color, float fog_start, float fog_end)
{
    // Looked up by name once, not on every call - GetShaderLocation() does
    // a string lookup each time, wasted work for a location that never
    // moves once the shader's compiled.
    static int camera_loc   = GetShaderLocation(chunk_shader, "cameraPosition");
    static int color_loc    = GetShaderLocation(chunk_shader, "fogColor");
    static int sky_color_loc = GetShaderLocation(chunk_shader, "fogSkyColor");
    static int start_loc    = GetShaderLocation(chunk_shader, "fogStart");
    static int end_loc      = GetShaderLocation(chunk_shader, "fogEnd");

    SetShaderValue(chunk_shader, camera_loc, &camera_position, SHADER_UNIFORM_VEC3);

    float color[3] = {fog_color.r / 255.0f, fog_color.g / 255.0f, fog_color.b / 255.0f};
    SetShaderValue(chunk_shader, color_loc, color, SHADER_UNIFORM_VEC3);
    float sky_color[3] = {fog_sky_color.r / 255.0f, fog_sky_color.g / 255.0f, fog_sky_color.b / 255.0f};
    SetShaderValue(chunk_shader, sky_color_loc, sky_color, SHADER_UNIFORM_VEC3);
    SetShaderValue(chunk_shader, start_loc, &fog_start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(chunk_shader, end_loc, &fog_end, SHADER_UNIFORM_FLOAT);
}

void set_chunk_water_time(float time)
{
    static int time_loc = GetShaderLocation(chunk_shader, "waterTime");
    SetShaderValue(chunk_shader, time_loc, &time, SHADER_UNIFORM_FLOAT);
}

void set_chunk_water_pass(bool active)
{
    static int water_pass_loc = GetShaderLocation(chunk_shader, "isWaterPass");
    int value = active ? 1 : 0; // GLSL bool uniforms are set as int from the C++ side
    SetShaderValue(chunk_shader, water_pass_loc, &value, SHADER_UNIFORM_INT);
}

void set_chunk_daylight(float sky_light_factor)
{
    static int daylight_loc = GetShaderLocation(chunk_shader, "daylightFactor");
    SetShaderValue(chunk_shader, daylight_loc, &sky_light_factor, SHADER_UNIFORM_FLOAT);
}

void set_chunk_brightness(float gamma)
{
    static int brightness_loc = GetShaderLocation(chunk_shader, "brightnessGamma");
    SetShaderValue(chunk_shader, brightness_loc, &gamma, SHADER_UNIFORM_FLOAT);
}

void set_chunk_dynamic_entity_pass(bool active)
{
    static int loc = GetShaderLocation(chunk_shader, "isDynamicEntityPass");
    int value = active ? 1 : 0; // GLSL bool uniforms are set as int from the C++ side
    SetShaderValue(chunk_shader, loc, &value, SHADER_UNIFORM_INT);
}

void begin_dynamic_entity_shader()
{
    set_chunk_water_pass(false);
    set_chunk_dynamic_entity_pass(true);
    BeginShaderMode(chunk_shader);
}

void end_dynamic_entity_shader()
{
    EndShaderMode();
    set_chunk_dynamic_entity_pass(false);
}

void unload_chunk_fog_shader()
{
    UnloadShader(chunk_shader);
}

int Chunk::index(int x, int y, int z)
{
    return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
}

Chunk::Chunk(Vector3 position) : GameObject(position)
{
    blocks.fill(BlockType::Air);
}

Chunk::~Chunk()
{
    if (mesh_uploaded) {
        UnloadMesh(mesh);
    }
    for (TransparentLayer& layer : transparent_layers) {
        if (layer.uploaded) UnloadMesh(layer.mesh);
    }
    if (water_mesh_uploaded) {
        UnloadMesh(water_mesh);
    }
}

void Chunk::generate_terrain(const TerrainNoise& noise)
{
    Vector3 origin = get_position();

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            float world_x = origin.x + x;
            float world_z = origin.z + z;

            // Biome weights first (Beta 1.7.3-style: a function of position
            // alone, not of the terrain height about to be generated) -
            // every biome's own height range/amplitude is blended by these
            // instead of picking just one, so crossing a border changes
            // terrain gradually instead of at a seam.
            BiomeWeights weights = noise.biome_weights(world_x, world_z);
            Biome dominant = dominant_biome(weights);
            column_grass_tint[z * CHUNK_SIZE + x] = grass_tint_for_weights(weights);
            column_foliage_tint[z * CHUNK_SIZE + x] = foliage_tint_for_weights(weights);

            BiomeTerrain plains = biome_terrain(Biome::Plains);
            BiomeTerrain forest = biome_terrain(Biome::Forest);
            BiomeTerrain desert = biome_terrain(Biome::Desert);
            BiomeTerrain hills  = biome_terrain(Biome::Hills);
            BiomeTerrain ocean  = biome_terrain(Biome::Ocean);
            BiomeTerrain sea    = biome_terrain(Biome::Sea);

            float base_height = weights.plains * plains.base_height
                               + weights.forest * forest.base_height
                               + weights.desert * desert.base_height
                               + weights.hills  * hills.base_height
                               + weights.ocean  * ocean.base_height
                               + weights.sea    * sea.base_height;
            float height_variation = weights.plains * plains.height_variation
                                    + weights.forest * forest.height_variation
                                    + weights.desert * desert.height_variation
                                    + weights.hills  * hills.height_variation
                                    + weights.ocean  * ocean.height_variation
                                    + weights.sea    * sea.height_variation;

            float sample = noise.height(world_x * NOISE_FREQUENCY, world_z * NOISE_FREQUENCY, NOISE_OCTAVES);
            float height_f = base_height + sample * height_variation;

            // Rivers: pull the blended height above down toward RIVER_BED
            // wherever the river noise is close to 0, smoothly (so its
            // banks slope into it instead of a sudden drop) - near a Desert
            // border (weights.desert neither ~0 nor ~1), so rivers
            // specifically cut through arid land rather than appearing
            // between every pair of biomes, and also near any Sea/Ocean
            // coastline, so a river that reaches the coast keeps carving
            // smoothly into it instead of stopping dead right at the
            // shoreline - the same noise field's course now visibly empties
            // into the sea instead of vanishing at the biome border.
            bool near_desert_border = weights.desert > 0.1f && weights.desert < 0.9f;
            bool near_coast = (weights.sea + weights.ocean) > BEACH_COAST_WEIGHT;
            bool river_tunnel = false;
            bool river_bed = false; // solidly inside the channel, not just its sloped bank - see RIVER_BED_CARVE_THRESHOLD
            if (near_desert_border || near_coast) {
                float river_distance = std::fabs(noise.river(world_x, world_z));
                float carve = std::clamp(1.0f - river_distance / RIVER_WIDTH, 0.0f, 1.0f);
                river_bed = carve > RIVER_BED_CARVE_THRESHOLD;
                if (carve > 0.0f) {
                    // How much of the surface-carving strength above still
                    // applies here, fading from 1 (full open valley) at
                    // RIVER_BED itself down to 0 by RIVER_TUNNEL_DEPTH
                    // blocks above it - a *gradual* handoff to the
                    // underground channel below as the natural land
                    // rises, instead of the two switching all-or-nothing
                    // at a single depth (which, since real terrain crosses
                    // that depth repeatedly along a winding river, made
                    // the river flicker between a visible valley and a
                    // fully hidden tunnel every few blocks - reading as
                    // scattered points from above rather than one
                    // continuous line).
                    float surface_ratio = 1.0f - std::clamp((height_f - RIVER_BED) / RIVER_TUNNEL_DEPTH, 0.0f, 1.0f);
                    float surface_carve = carve * surface_ratio;
                    height_f = height_f * (1.0f - surface_carve) + RIVER_BED * surface_carve;
                    // Always try the underground channel too, not just
                    // where the surface carve above faded out completely
                    // - it naturally has no visible effect wherever the
                    // (possibly still-lowered) surface already reaches
                    // down that far, since the fill loop below clamps the
                    // tunnel to stay under the actual surface.
                    river_tunnel = true;
                }
            }

            int height = std::clamp(static_cast<int>(std::lround(height_f)), 1, CHUNK_HEIGHT - 1);

            // The dominant biome's own surface blocks - except Hills, whose
            // tallest peaks break through the tree line into bare stone
            // regardless of what biome_terrain() would otherwise say.
            BiomeTerrain terrain = biome_terrain(dominant);
            BlockType surface_block = terrain.surface_block;
            BlockType subsurface_block = terrain.subsurface_block;
            if (dominant == Biome::Hills && height > HILLS_STONE_LINE) {
                surface_block = BlockType::Stone;
                subsurface_block = BlockType::Stone;
            }

            // Beach: a shallow shelf of land right at a Sea/Ocean coastline
            // gets a shoreline material instead of whatever its own land
            // biome would otherwise put there (grass, etc.) - sand for a
            // calm Sea coastline, gravel for a "wild" Ocean one. Only
            // applies on the land side (Sea/Ocean columns already get
            // their own gravel floor from biome_terrain() above).
            bool is_land = dominant != Biome::Sea && dominant != Biome::Ocean;
            if (is_land && near_coast && height <= WATER_LEVEL + BEACH_HEIGHT_ABOVE_WATER) {
                bool wild_coast = weights.ocean > weights.sea;
                surface_block = wild_coast ? BlockType::Gravel : BlockType::Sand;
                subsurface_block = surface_block;
            }

            // River bed: sand (with a chance of gravel, in small patches -
            // see GRAVEL_THRESHOLD) instead of the land biome's own
            // surface block, so a river carved through Plains/Forest
            // exposes a proper sandy/gravelly bed rather than leaving
            // plain dirt sitting underwater. Sea/Ocean columns keep their
            // own biome_terrain floor untouched (is_land guards that).
            if (is_land && river_bed && height < WATER_LEVEL) {
                bool has_gravel = noise.gravel(world_x, world_z) > GRAVEL_THRESHOLD;
                surface_block = has_gravel ? BlockType::Gravel : BlockType::Sand;
                subsurface_block = surface_block;
            }

            // Grass never generates underwater - same as real Minecraft,
            // where a grass block needs open air/sunlight above it and
            // reverts to dirt without that. A land column's own blended
            // height can still dip below WATER_LEVEL from height noise
            // alone, well away from an actual coastline (so the beach
            // override above never triggers for it); this catches that
            // general case, not just the coastal one.
            if (height < WATER_LEVEL && surface_block == BlockType::Grass) {
                surface_block = BlockType::Dirt;
            }

            // Clay: small patches within underwater sand only (a beach
            // shelf, or Desert dipping below sea level) - see CLAY_
            // THRESHOLD above. Never touches subsurface_block, so a patch
            // reads as a thin deposit sitting in the sand.
            if (surface_block == BlockType::Sand && height < WATER_LEVEL &&
                noise.clay(world_x, world_z) > CLAY_THRESHOLD) {
                surface_block = BlockType::Clay;
            }

            // Everything past this column's own content is already Air
            // (blocks.fill(BlockType::Air) in the constructor), so the loop
            // can stop there instead of walking all the way to
            // CHUNK_HEIGHT; set_block() tracks the chunk-wide high point
            // (highest_block_y) that build_mesh()/compute_lighting() bound
            // their own loops to.
            int column_top = std::max(height, WATER_LEVEL);

            for (int y = 0; y <= column_top; ++y) {
                BlockType type;
                if (y == 0) {
                    type = BlockType::Bedrock;
                } else if (y > height) {
                    type = BlockType::Water; // the underwater gap up to sea level
                } else if (y == height) {
                    type = surface_block;
                } else if (y > height - terrain.surface_depth) {
                    type = subsurface_block;
                } else {
                    type = BlockType::Stone;
                }
                set_block(x, y, z, type);
            }

            // River tunnel: carved after the column above is already
            // filled solid, so it reads as a channel bored straight
            // through the rock rather than a shape generate_terrain built
            // in from the start - flooded the same way a river itself is
            // filled with water above.
            if (river_tunnel) {
                int tunnel_bottom = std::max(1, RIVER_BED - RIVER_TUNNEL_HALF_HEIGHT);
                int tunnel_top = std::min(RIVER_BED + RIVER_TUNNEL_HALF_HEIGHT, height - 2);
                for (int y = tunnel_bottom; y <= tunnel_top; ++y) {
                    set_block(x, y, z, BlockType::Water);
                }
            }
        }
    }
}

namespace {
    // --- Cave generation: Beta 1.7.3-style "Perlin worms" ---
    //
    // A tunnel is a 3D random walk: start at a point, then repeatedly step
    // forward along a (yaw, pitch) heading that itself drifts by a small
    // random amount each step (rather than being re-picked from scratch),
    // carving an ellipsoid of empty space around every point along the
    // way. This is the same shape of algorithm real Minecraft used from
    // early Alpha through 1.17, before 1.18 replaced it with 3D
    // noise-density "cheese/spaghetti" caves.

    // How far, in chunks, a tunnel's *origin* can be from the chunk
    // actually being carved and still possibly reach into it. A tunnel
    // starting further away than this and somehow still reaching in would
    // simply not get carved - an acceptable trade-off for how rarely a
    // single tunnel runs longer than this many chunks.
    constexpr int CAVE_CHUNK_RADIUS = 4;

    // How many chunks, on average, go by between one that actually
    // originates a cave system - most don't. Tuned empirically (a first
    // pass using Beta's own reported triple-nested-random.nextInt formula
    // for the count averaged nearly 5 systems per origin chunk, riddling
    // ~80% of the underground with exposed voids and making initial
    // world load ~9x slower) rather than by trying to reproduce that
    // formula exactly.
    constexpr int CAVE_CHUNK_RARITY = 6;

    // How many blocks of world Y a tunnel's random starting height is
    // drawn from, added to MIN_WORLD_Y - biased toward the *bottom* of
    // that range (see cave_start_y), same as Beta's own bias toward deep
    // caves, just rescaled from Beta's 0-128 world onto this one's own
    // range.
    constexpr float CAVE_HEIGHT_RANGE = 150.0f;

    // Deterministic per-chunk seed: the same (world_seed, chunk_x,
    // chunk_z) always produces the same tunnels regardless of which chunk
    // asks for them first or when - the whole reason a tunnel can be
    // carved consistently from both sides of a chunk border. A small
    // ad-hoc mixing hash (not cryptographic, just decorrelated enough)
    // rather than something simpler like addition, so nearby chunk
    // coordinates don't produce suspiciously similar seeds.
    uint64_t cave_chunk_seed(uint32_t world_seed, int chunk_x, int chunk_z) {
        uint64_t h = world_seed + 0x9E3779B97F4A7C15ULL;
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) * 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk_z)) * 0xC4CEB9FE1A85EC53ULL;
        h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
        return h;
    }

    double cave_random_double(std::mt19937_64& rng) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
    }

    int cave_random_int(std::mt19937_64& rng, int bound) {
        if (bound <= 1) return 0;
        return std::uniform_int_distribution<int>(0, bound - 1)(rng);
    }

    // Biased toward MIN_WORLD_Y: the product of two uniform [0,1) values
    // skews toward 0 (real Minecraft's own trick for making deep caves
    // more common than shallow ones without excluding shallow ones
    // outright).
    float cave_start_y(std::mt19937_64& rng) {
        return MIN_WORLD_Y + static_cast<float>(cave_random_double(rng) * cave_random_double(rng)) * CAVE_HEIGHT_RANGE;
    }

    // Clears every block within an axis-aligned ellipsoid centered on
    // (center_x, center_y, center_z) with the given horizontal (X/Z) and
    // vertical (Y) radii, but only wherever that lands inside chunk
    // (chunk_x, chunk_z) - a tunnel step's ellipsoid is computed in full
    // world-space and this simply no-ops for the part of it (usually most
    // of it) outside this one chunk. Never touches Water (so a tunnel
    // can't drain into or flood from a lake it happens to pass near),
    // Bedrock, or cells already Air.
    void carve_ellipsoid(Chunk& chunk, int chunk_x, int chunk_z, double center_x, double center_y, double center_z,
                          double horizontal_radius, double vertical_radius) {
        if (horizontal_radius <= 0.0 || vertical_radius <= 0.0) return;

        int chunk_min_x = chunk_x * CHUNK_SIZE;
        int chunk_min_z = chunk_z * CHUNK_SIZE;
        int min_x = std::max(               0, static_cast<int>(std::floor(center_x - horizontal_radius)) - chunk_min_x);
        int max_x = std::min(  CHUNK_SIZE - 1, static_cast<int>(std::ceil( center_x + horizontal_radius)) - chunk_min_x);
        int min_z = std::max(               0, static_cast<int>(std::floor(center_z - horizontal_radius)) - chunk_min_z);
        int max_z = std::min(  CHUNK_SIZE - 1, static_cast<int>(std::ceil( center_z + horizontal_radius)) - chunk_min_z);
        int min_y = std::max(               1, static_cast<int>(std::floor(center_y -   vertical_radius)) - MIN_WORLD_Y);
        int max_y = std::min(CHUNK_HEIGHT - 1, static_cast<int>(std::ceil( center_y +   vertical_radius)) - MIN_WORLD_Y);

        for (int lx = min_x; lx <= max_x; ++lx) {
            double dx = (chunk_min_x + lx + 0.5 - center_x) / horizontal_radius;
            for (int lz = min_z; lz <= max_z; ++lz) {
                double dz = (chunk_min_z + lz + 0.5 - center_z) / horizontal_radius;
                double horizontal = dx * dx + dz * dz;
                if (horizontal >= 1.0) continue; // outside the ellipse at every height
                for (int ly = min_y; ly <= max_y; ++ly) {
                    double dy = (ly + MIN_WORLD_Y + 0.5 - center_y) / vertical_radius;
                    if (horizontal + dy * dy >= 1.0) continue;

                    BlockType existing = chunk.get_block(lx, ly, lz);
                    if (existing == BlockType::Air || existing == BlockType::Water || existing == BlockType::Bedrock) continue;
                    chunk.set_block(lx, ly, lz, BlockType::Air);
                }
            }
        }
    }

    // Walks one tunnel from (x, y, z), carving as it goes - everything
    // past the starting point (heading, length, how the radius tapers) is
    // decided here from `rng`, which the caller has already seeded
    // deterministically, so replaying this from any chunk within
    // CAVE_CHUNK_RADIUS reproduces the identical path.
    void carve_tunnel(Chunk& chunk, int chunk_x, int chunk_z, std::mt19937_64& rng,
                       double x, double y, double z, float radius_scale, int length) {
        double yaw = cave_random_double(rng) * 2.0 * PI;
        double pitch = (cave_random_double(rng) - 0.5) * 0.25;
        double yaw_velocity = 0.0;
        double pitch_velocity = 0.0;
        double base_radius = (cave_random_double(rng) * 2.0 + cave_random_double(rng)) * radius_scale;

        for (int step = 0; step < length; ++step) {
            // Widest around the middle of its length, tapering to a point
            // at both ends, rather than a uniform-diameter pipe.
            double taper             = std::sin(PI * step / length);
            double horizontal_radius = 1.5 + taper * base_radius;
            double vertical_radius   = horizontal_radius * 0.5; // flatter than it is wide, same as Beta's own tunnels

            x += std::cos(yaw) * std::cos(pitch);
            z += std::sin(yaw) * std::cos(pitch);
            y += std::sin(pitch);

            // Pitch decays back toward level and yaw/pitch's own drift is
            // itself randomly (and smoothly, since it's velocity rather
            // than position) perturbed each step - an organically curving
            // path instead of one long straight line or pure noise-free
            // randomness at every step.
            pitch *= 0.92;
            yaw_velocity   += (cave_random_double(rng) - cave_random_double(rng)) * cave_random_double(rng) * 2.0;
            pitch_velocity += (cave_random_double(rng) - cave_random_double(rng)) * cave_random_double(rng) * 4.0;
            yaw   += yaw_velocity   * 0.1;
            pitch += pitch_velocity * 0.1;

            carve_ellipsoid(chunk, chunk_x, chunk_z, x, y, z, horizontal_radius, vertical_radius);
        }
    }

    // Every tunnel *system* originating in one chunk: usually a handful of
    // separate winding tunnels branching from one starting point, but
    // occasionally (1 in 4) a single much fatter cavern-like tunnel
    // instead. `origin_chunk_x/z` is where the system starts (and where
    // its own share of `rng`'s random calls come from) - `carve_chunk_x/z`
    // is the chunk actually being written to right now, which may or may
    // not be the same chunk.
    void carve_cave_system(Chunk& chunk, int carve_chunk_x, int carve_chunk_z,
                            int origin_chunk_x, int origin_chunk_z, std::mt19937_64& rng) {
        double start_x = origin_chunk_x * CHUNK_SIZE + cave_random_double(rng) * CHUNK_SIZE;
        double start_y = cave_start_y(rng);
        double start_z = origin_chunk_z * CHUNK_SIZE + cave_random_double(rng) * CHUNK_SIZE;

        int branch_count = 1;
        float radius_scale = 1.0f;
        if (cave_random_int(rng, 4) == 0) {
            radius_scale = static_cast<float>(cave_random_double(rng) * 6.0 + 1.0); // one big cavern instead
        } else {
            branch_count = 1 + cave_random_int(rng, 4);
        }

        for (int branch = 0; branch < branch_count; ++branch) {
            int length = 25 + cave_random_int(rng, 15);
            if (cave_random_int(rng, 6) == 0) {
                length += cave_random_int(rng, 100); // a rare, much longer system
            }
            carve_tunnel(chunk, carve_chunk_x, carve_chunk_z, rng, start_x, start_y, start_z, radius_scale, length);
        }
    }

    // --- Ravines: a separate, much rarer carving feature ---
    //
    // Beta 1.7.3 carved ravines as their own thing alongside normal cave
    // tunnels: one single long crack, narrow side-to-side but stretched
    // much taller than it is wide, wandering far less than a cave tunnel
    // does and starting closer to the surface - often breaking through
    // into a visible open-air gorge rather than staying safely buried.

    // How many chunks, on average, go by between one that actually
    // originates a ravine - deliberately much rarer than a cave system
    // (see CAVE_CHUNK_RARITY above), since a ravine is meant to read as a
    // rare, striking find rather than a common feature.
    constexpr int RAVINE_CHUNK_RARITY = 30; // 60;

    // Ravines are biased toward starting higher up than caves are (see
    // cave_start_y's own deep bias) - real ravines commonly cut close to
    // the surface, sometimes exposing themselves as an open gorge.
    constexpr float RAVINE_HEIGHT_RANGE = 220.0f;

    // XORed into world_seed before deriving a ravine's own per-chunk RNG,
    // so a chunk's ravine roll and its cave roll are decorrelated instead
    // of being (or not being) the exact same coin flip every time.
    constexpr uint32_t RAVINE_SEED_SALT = 0x52415649u; // "RAVI"

    float ravine_start_y(std::mt19937_64& rng) {
        return MIN_WORLD_Y + static_cast<float>(cave_random_double(rng)) * RAVINE_HEIGHT_RANGE;
    }

    // A ravine's own walk: the same drifting-heading idea as carve_tunnel,
    // but with far less yaw/pitch drift (a ravine reads as one long,
    // mostly-straight crack, not a winding cave) and a very different
    // cross-section - narrow horizontally, stretched tall vertically, so
    // it carves like a canyon rather than a round tunnel.
    void carve_ravine(Chunk& chunk, int chunk_x, int chunk_z, std::mt19937_64& rng,
                          double x,    double y,    double z, int length) {
        double yaw      = cave_random_double(rng) * 2.0 * PI;
        double pitch    = (cave_random_double(rng) - 0.5) * 0.15;
        double yaw_velocity   = 0.0;
        double pitch_velocity = 0.0;
        double horizontal_scale = cave_random_double(rng) * 1.5 + 1.0; // stays narrow
        double vertical_scale   = cave_random_double(rng) * 3.0 + 4.0; // but tall

        for (int step = 0; step < length; ++step) {
            double taper = std::sin(PI * step / length);
            double horizontal_radius = 1.0 + taper * horizontal_scale;
            double vertical_radius   = 2.0 + taper * vertical_scale;

            x += std::cos(yaw) * std::cos(pitch);
            z += std::sin(yaw) * std::cos(pitch);
            y += std::sin(pitch) * 0.5; // shallower descent than a cave tunnel's own

            pitch *= 0.95;
            yaw_velocity   += (cave_random_double(rng) - cave_random_double(rng)) * cave_random_double(rng) * 0.5;
            pitch_velocity += (cave_random_double(rng) - cave_random_double(rng)) * cave_random_double(rng) * 1.0;
            yaw   += yaw_velocity * 0.05;
            pitch += pitch_velocity * 0.05;

            carve_ellipsoid(chunk, chunk_x, chunk_z, x, y, z, horizontal_radius, vertical_radius);
        }
    }

    // --- Ore/filler vein generation ---
    //
    // Beta 1.7.3's own per-ore Y bands, taken literally as local (chunk-
    // relative) Y rather than rescaled the way CAVE_HEIGHT_RANGE above
    // stretches Beta's shorter world onto this engine's taller one:
    // bedrock sits at local Y 0 in both worlds (this engine's MIN_WORLD_Y
    // is exactly where Beta's own Y=0 floor was), so "ore X generates at
    // Beta Y 5-60" means local Y 5-60 here too, unchanged - keeping ore
    // depth tied to actual distance from bedrock instead of drifting
    // shallower relative to the world floor just because this world
    // happens to have more empty sky above.
    struct OreVein {
        BlockType type;
        int y_min, y_max;               // full band a vein can appear in
        int y_common_min, y_common_max; // denser sub-band - most veins land here
        int veins_per_chunk;
        int max_vein_size;
    };

    // veins_per_chunk/max_vein_size bumped up noticeably from Beta 1.7.3's
    // own sparser numbers toward modern Minecraft's actual per-chunk ore
    // counts/blob sizes - too sparse to reliably notice at this engine's
    // per-chunk pace otherwise.
    constexpr OreVein ORE_VEINS[] = {
        {BlockType::CoalOre,     0, 127,   5,  60,  60,  17},
        {BlockType::IronOre,     0,  64,  10,  40,  40,   9},
        {BlockType::GoldOre,     0,  32,  14,  28,   4,   9},
        {BlockType::LapisOre,    0,  31,  10,  16,   3,   7},
        {BlockType::RedstoneOre, 0,  16,   8,  12,   8,   8},
        {BlockType::DiamondOre,  0,  16,   5,  12,   4,   6},

        // Exposed outcrops: a second, much sparser pair of entries for the
        // same two ore types, up in Hills' bare-stone peak band instead of
        // their usual deep one - HILLS_STONE_LINE (world Y 95, local 159)
        // is where generate_terrain() turns a Hills column's surface_block/
        // subsurface_block to Stone all the way up, so a vein landing here
        // can surface right at a peak's own visible top, the same way real
        // Minecraft mountainsides show a coal seam in the cliff face
        // itself rather than only ever appearing after digging down to the
        // deep band above. Everywhere else this band simply finds no Stone
        // to touch (ordinary grass/dirt land, or a Hills column below its
        // own stone line) and is a no-op, same as any other vein attempt
        // with nothing to land in.
        {BlockType::CoalOre,     159, 1882, 162, 176, 6, 6},
        {BlockType::IronOre,     159, 182, 162, 176, 4, 5},
    };

    // Underground Dirt/Gravel patches - not ores, but generated the exact
    // same way (small blobs replacing only Stone), same request as the ore
    // veins above.
    struct FillerPatch {
        BlockType type;
        int y_min, y_max;
        int patches_per_chunk;
        int max_patch_size;
    };

    constexpr FillerPatch FILLER_PATCHES[] = {
        {BlockType::Dirt,   0, 128, 8, 28},
        {BlockType::Gravel, 0, 128, 6, 24},
    };

    constexpr uint32_t ORE_SEED_SALT = 0x4F524553u; // "ORES" - decorrelates from cave_chunk_seed's own cave/ravine streams
    constexpr float ORE_COMMON_BAND_CHANCE = 0.7f; // how often a vein rolls its Y within the denser common band instead of the full range

    // A vein/patch is a short random walk (same shape idea as
    // carve_tunnel() above, just replacing Stone with `type` instead of
    // carving it to Air) - only ever touches Stone, so it can't eat into
    // Bedrock, an already-placed vein from earlier in this same pass, or
    // anything generate_terrain() itself put down (surface dirt, ore-free
    // subsurface layers, etc.).
    void place_vein(Chunk& chunk, std::mt19937_64& rng, BlockType type, int y_min, int y_max, int size) {
        int x = cave_random_int(rng, CHUNK_SIZE);
        int y = std::clamp(y_min + cave_random_int(rng, std::max(1, y_max - y_min + 1)), 0, CHUNK_HEIGHT - 1);
        int z = cave_random_int(rng, CHUNK_SIZE);
        for (int step = 0; step < size; ++step) {
            if (x >= 0 && x < CHUNK_SIZE && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_SIZE &&
                chunk.get_block(x, y, z) == BlockType::Stone) {
                chunk.set_block(x, y, z, type);
            }
            x += cave_random_int(rng, 3) - 1;
            y += cave_random_int(rng, 3) - 1;
            z += cave_random_int(rng, 3) - 1;
        }
    }

    // Round, ball-shaped cluster - a minority of vein attempts (see
    // BLOB_VEIN_CHANCE) fill a small ellipsoid outright instead of walking
    // place_vein()'s elongated random path, the same idea carve_ellipsoid()
    // above already uses for cave rooms, just filling Stone with `type`
    // rather than carving it to Air. Same Stone-only guard as place_vein().
    void place_blob_vein(Chunk& chunk, std::mt19937_64& rng, BlockType type, int y_min, int y_max, int radius) {
        int center_x = cave_random_int(rng, CHUNK_SIZE);
        int center_y = std::clamp(y_min + cave_random_int(rng, std::max(1, y_max - y_min + 1)), 0, CHUNK_HEIGHT - 1);
        int center_z = cave_random_int(rng, CHUNK_SIZE);
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    // Ellipsoid membership test, not a cube: (dx/r)^2 + (dy/r)^2 + (dz/r)^2 <= 1.
                    float nx = static_cast<float>(dx) / radius;
                    float ny = static_cast<float>(dy) / radius;
                    float nz = static_cast<float>(dz) / radius;
                    if (nx * nx + ny * ny + nz * nz > 1.0f) continue;

                    int x = center_x + dx, y = center_y + dy, z = center_z + dz;
                    if (x >= 0 && x < CHUNK_SIZE && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_SIZE &&
                        chunk.get_block(x, y, z) == BlockType::Stone) {
                        chunk.set_block(x, y, z, type);
                    }
                }
            }
        }
    }

    constexpr float BLOB_VEIN_CHANCE = 0.15f; // fraction of vein attempts that become a round blob instead of an elongated walk
    constexpr int BLOB_RADIUS_MIN = 2;
    constexpr int BLOB_RADIUS_MAX = 4;
}

void Chunk::generate_ores(uint32_t world_seed, int chunk_x, int chunk_z)
{
    std::mt19937_64 rng(cave_chunk_seed(world_seed ^ ORE_SEED_SALT, chunk_x, chunk_z));

    for (const OreVein& vein : ORE_VEINS) {
        for (int i = 0; i < vein.veins_per_chunk; ++i) {
            bool common_band = cave_random_double(rng) < ORE_COMMON_BAND_CHANCE;
            int y_min = common_band ? vein.y_common_min : vein.y_min;
            int y_max = common_band ? vein.y_common_max : vein.y_max;
            if (cave_random_double(rng) < BLOB_VEIN_CHANCE) {
                int radius = BLOB_RADIUS_MIN + cave_random_int(rng, BLOB_RADIUS_MAX - BLOB_RADIUS_MIN + 1);
                place_blob_vein(*this, rng, vein.type, y_min, y_max, radius);
            } else {
                int size = 1 + cave_random_int(rng, vein.max_vein_size);
                place_vein(*this, rng, vein.type, y_min, y_max, size);
            }
        }
    }

    for (const FillerPatch& patch : FILLER_PATCHES) {
        for (int i = 0; i < patch.patches_per_chunk; ++i) {
            int size = 1 + cave_random_int(rng, patch.max_patch_size);
            place_vein(*this, rng, patch.type, patch.y_min, patch.y_max, size);
        }
    }
}

void Chunk::carve_caves(uint32_t world_seed, int chunk_x, int chunk_z)
{
    for (int origin_x = chunk_x - CAVE_CHUNK_RADIUS; origin_x <= chunk_x + CAVE_CHUNK_RADIUS; ++origin_x) {
        for (int origin_z = chunk_z - CAVE_CHUNK_RADIUS; origin_z <= chunk_z + CAVE_CHUNK_RADIUS; ++origin_z) {
            std::mt19937_64 rng(cave_chunk_seed(world_seed, origin_x, origin_z));

            // Most chunks originate nothing at all.
            if (cave_random_int(rng, CAVE_CHUNK_RARITY) == 0) {
                int system_count = 1 + cave_random_int(rng, 3);
                for (int i = 0; i < system_count; ++i) {
                    carve_cave_system(*this, chunk_x, chunk_z, origin_x, origin_z, rng);
                }
            }

            // A separate RNG stream (own salted seed) so a chunk's ravine
            // roll isn't the same coin flip as its cave roll above.
            std::mt19937_64 ravine_rng(cave_chunk_seed(world_seed ^ RAVINE_SEED_SALT, origin_x, origin_z));
            if (cave_random_int(ravine_rng, RAVINE_CHUNK_RARITY) == 0) {
                double start_x = origin_x * CHUNK_SIZE + cave_random_double(ravine_rng) * CHUNK_SIZE;
                double start_y = ravine_start_y(ravine_rng);
                double start_z = origin_z * CHUNK_SIZE + cave_random_double(ravine_rng) * CHUNK_SIZE;
                int length = 40 + cave_random_int(ravine_rng, 40);
                carve_ravine(*this, chunk_x, chunk_z, ravine_rng, start_x, start_y, start_z, length);
            }
        }
    }
}

namespace {
    constexpr uint32_t CHUNK_FILE_MAGIC = 0x4D434348u; // "MCCH"
    constexpr uint32_t CHUNK_FILE_VERSION = 4u;
}

bool Chunk::save_to_file(const std::string& path) const
{
    std::filesystem::path target(path);
    std::filesystem::path tmp = target;
    tmp += ".tmp";

    std::error_code ec;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    uint32_t magic   = CHUNK_FILE_MAGIC;
    uint32_t version = CHUNK_FILE_VERSION;
    int32_t highest = static_cast<int32_t>(highest_block_y);
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(blocks.data()), blocks.size());
    out.write(reinterpret_cast<const char*>(fluid_level.data()), fluid_level.size());
    out.write(reinterpret_cast<const char*>(column_grass_tint.data()), column_grass_tint.size() * sizeof(Color));
    out.write(reinterpret_cast<const char*>(column_foliage_tint.data()), column_foliage_tint.size() * sizeof(Color));
    out.write(reinterpret_cast<const char*>(&highest), sizeof(highest));

    // Directional-block facing (see HorizontalDirection's own comment) -
    // sparse, so persisted the same shape it's kept in memory: a count
    // followed by that many (local index, direction) pairs, rather than a
    // parallel full-chunk array that would be all-default almost
    // everywhere. Version 3+ only - see load_from_file()'s own handling of
    // older files that predate this section entirely.
    uint32_t orientation_count = static_cast<uint32_t>(orientation.size());
    out.write(reinterpret_cast<const char*>(&orientation_count), sizeof(orientation_count));
    for (const auto& [local_index, direction] : orientation) {
        int32_t index32 = static_cast<int32_t>(local_index);
        uint8_t direction8 = static_cast<uint8_t>(direction);
        out.write(reinterpret_cast<const char*>(&index32), sizeof(index32));
        out.write(reinterpret_cast<const char*>(&direction8), sizeof(direction8));
    }
    // Extra shaped/multi-block per-instance state (see get_block_state()'s
    // own comment) - same sparse count-then-pairs shape as orientation
    // above, version 4+ only.
    uint32_t block_state_count = static_cast<uint32_t>(block_state.size());
    out.write(reinterpret_cast<const char*>(&block_state_count), sizeof(block_state_count));
    for (const auto& [local_index, packed] : block_state) {
        int32_t index32 = static_cast<int32_t>(local_index);
        out.write(reinterpret_cast<const char*>(&index32), sizeof(index32));
        out.write(reinterpret_cast<const char*>(&packed), sizeof(packed));
    }

    out.close();
    if (!out) return false;

    std::filesystem::rename(tmp, target, ec);
    return !ec;
}

bool Chunk::load_from_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t magic = 0;
    uint32_t version = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in || magic != CHUNK_FILE_MAGIC || version < 1u || version > CHUNK_FILE_VERSION) return false;

    in.read(reinterpret_cast<char*>(blocks.data()), blocks.size());
    in.read(reinterpret_cast<char*>(fluid_level.data()), fluid_level.size());
    in.read(reinterpret_cast<char*>(column_grass_tint.data()), column_grass_tint.size() * sizeof(Color));
    if (version >= 2u) {
        in.read(reinterpret_cast<char*>(column_foliage_tint.data()), column_foliage_tint.size() * sizeof(Color));
    } else {
        // Version 1 did not persist foliage color. Preserve the chunk and
        // give its leaves a sensible green fallback instead of discarding
        // player edits merely to regenerate biome tints.
        column_foliage_tint.fill(FOREST_FOLIAGE_TINT);
    }
    int32_t highest = 0;
    in.read(reinterpret_cast<char*>(&highest), sizeof(highest));
    if (!in) return false; // truncated - don't trust a partial read

    orientation.clear();
    if (version >= 3u) {
        uint32_t orientation_count = 0;
        in.read(reinterpret_cast<char*>(&orientation_count), sizeof(orientation_count));
        for (uint32_t i = 0; i < orientation_count && in; ++i) {
            int32_t index32 = 0;
            uint8_t direction8 = 0;
            in.read(reinterpret_cast<char*>(&index32), sizeof(index32));
            in.read(reinterpret_cast<char*>(&direction8), sizeof(direction8));
            if (direction8 <= static_cast<uint8_t>(HorizontalDirection::West)) {
                orientation[index32] = static_cast<HorizontalDirection>(direction8);
            }
        }
        if (!in) return false; // truncated - don't trust a partial read
    }

    block_state.clear();
    if (version >= 4u) {
        uint32_t block_state_count = 0;
        in.read(reinterpret_cast<char*>(&block_state_count), sizeof(block_state_count));
        for (uint32_t i = 0; i < block_state_count && in; ++i) {
            int32_t index32 = 0;
            uint16_t packed = 0;
            in.read(reinterpret_cast<char*>(&index32), sizeof(index32));
            in.read(reinterpret_cast<char*>(&packed), sizeof(packed));
            block_state[index32] = packed;
        }
        if (!in) return false; // truncated - don't trust a partial read
    }

    highest_block_y = highest;
    return true;
}

BlockType Chunk::get_block(int x, int y, int z) const
{
    return blocks[index(x, y, z)];
}

Color Chunk::get_foliage_tint(int x, int z) const
{
    return column_foliage_tint[z * CHUNK_SIZE + x];
}

Color Chunk::get_grass_tint(int x, int z) const
{
    return column_grass_tint[z * CHUNK_SIZE + x];
}

void Chunk::set_block(int x, int y, int z, BlockType type)
{
    blocks[index(x, y, z)] = type;
    // Only ever raises highest_block_y, never lowers it - see its
    // declaration in Chunk.hpp for why that's the safe direction to be
    // wrong in. Covers both generation (each column's own content) and any
    // later player-placed block above it (e.g. a tower).
    if (type != BlockType::Air && y > highest_block_y) {
        highest_block_y = y;
    }
    // Whatever used to be here (if anything) is gone now - stale
    // orientation would otherwise linger and could apply to a totally
    // different block later placed at the same position. `orientation` is
    // empty for the entire lifetime of the vast majority of chunks (only a
    // player-placed directional block ever adds to it), so the emptiness
    // check keeps this a no-op branch rather than a hash lookup on every
    // single set_block() call generate_terrain() itself makes.
    if (!orientation.empty()) orientation.erase(index(x, y, z));
    if (!block_state.empty()) block_state.erase(index(x, y, z));
}

HorizontalDirection Chunk::get_orientation(int x, int y, int z) const
{
    auto it = orientation.find(index(x, y, z));
    return it != orientation.end() ? it->second : HorizontalDirection::South;
}

void Chunk::set_orientation(int x, int y, int z, HorizontalDirection direction)
{
    orientation[index(x, y, z)] = direction;
}

uint16_t Chunk::get_block_state(int x, int y, int z) const
{
    auto it = block_state.find(index(x, y, z));
    return it != block_state.end() ? it->second : 0u;
}

void Chunk::set_block_state(int x, int y, int z, uint16_t packed)
{
    block_state[index(x, y, z)] = packed;
}

uint8_t Chunk::get_fluid_level(int x, int y, int z) const
{
    return fluid_level[index(x, y, z)];
}

void Chunk::set_fluid_level(int x, int y, int z, uint8_t level)
{
    fluid_level[index(x, y, z)] = level;
}

bool Chunk::is_opaque(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
        return false;
    }
    return !get_block_properties(get_block(x, y, z)).transparent;
}

int Chunk::get_sky_light(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
        // No neighbor-chunk data yet (same as is_opaque) - assume open,
        // sunlit space rather than reading as pitch black at chunk edges.
        return MAX_LIGHT;
    }
    if (y > highest_block_y) {
        // compute_lighting()'s top-down scan starts at highest_block_y, not
        // CHUNK_HEIGHT-1, since everything above it is guaranteed air in
        // every column of this chunk - so it never actually writes a value
        // up here. That's still genuinely open sky, same as the out-of-
        // chunk case above, not the light[]'s untouched 0 default.
        return MAX_LIGHT;
    }
    return light[index(x, y, z)] >> 4;
}

void Chunk::set_sky_light(int x, int y, int z, int value)
{
    uint8_t& cell = light[index(x, y, z)];
    cell = static_cast<uint8_t>((cell & 0x0F) | (value << 4));
}

int Chunk::get_block_light(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
        return 0;
    }
    return light[index(x, y, z)] & 0x0F;
}

void Chunk::set_block_light(int x, int y, int z, int value)
{
    uint8_t& cell = light[index(x, y, z)];
    cell = static_cast<uint8_t>((cell & 0xF0) | value);
}

int Chunk::get_light(int x, int y, int z) const
{
    int sky = get_sky_light(x, y, z);
    int block = get_block_light(x, y, z);
    return sky > block ? sky : block;
}

void Chunk::clear_lighting()
{
    light.fill(uint8_t{0});
}

void Chunk::compute_lighting()
{
    // Only y <= highest_block_y is ever read back (get_sky_light() reports
    // MAX_LIGHT, without touching the array, for anything above it) - and
    // since y is the slowest-varying index in Chunk::index(), every cell
    // with y <= highest_block_y occupies one contiguous prefix of this flat
    // array. No need to reset (or, below, scan) anything past that; called
    // after generate_terrain(), which has already set highest_block_y for
    // this call to use, including on a later re-light after a placed block
    // raised it.
    std::fill_n(light.begin(), (highest_block_y + 1) * CHUNK_SIZE * CHUNK_SIZE, uint8_t{0});

    using Cell = std::array<int, 3>;
    std::queue<Cell> skyQueue;
    std::queue<Cell> blockQueue;

    // Direct sky exposure: scan each column from the top, stop at the first
    // opaque block. Cells below it get lit later, if at all, by the BFS
    // spreading sideways from a neighboring open column. Starts at
    // highest_block_y, not CHUNK_HEIGHT-1 - every cell above that is
    // guaranteed air in every column of this chunk (get_sky_light() reports
    // MAX_LIGHT up there without this scan ever needing to visit it).
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = highest_block_y; y >= 0; --y) {
                if (is_opaque(x, y, z)) break;
                set_sky_light(x, y, z, MAX_LIGHT);
                skyQueue.push({x, y, z});
            }
        }
    }

    // Block light sources: any block with luminance > 0. Bounded the same
    // way - no block exists above highest_block_y to be a light source.
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y <= highest_block_y; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                int luminance = get_block_properties(get_block(x, y, z)).luminance;
                if (luminance > 0) {
                    set_block_light(x, y, z, luminance);
                    blockQueue.push({x, y, z});
                }
            }
        }
    }

    constexpr int OFFSETS[6][3] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};

    while (!skyQueue.empty()) {
        Cell cell = skyQueue.front();
        skyQueue.pop();
        int level = get_sky_light(cell[0], cell[1], cell[2]);

        for (const auto& offset : OFFSETS) {
            int nx = cell[0] + offset[0], ny = cell[1] + offset[1], nz = cell[2] + offset[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (is_opaque(nx, ny, nz)) continue;

            int newLevel = level - 1;
            if (newLevel > get_sky_light(nx, ny, nz)) {
                set_sky_light(nx, ny, nz, newLevel);
                if (newLevel > 0) skyQueue.push({nx, ny, nz});
            }
        }
    }

    while (!blockQueue.empty()) {
        Cell cell = blockQueue.front();
        blockQueue.pop();
        int level = get_block_light(cell[0], cell[1], cell[2]);

        for (const auto& offset : OFFSETS) {
            int nx = cell[0] + offset[0], ny = cell[1] + offset[1], nz = cell[2] + offset[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (is_opaque(nx, ny, nz)) continue;

            int newLevel = level - 1;
            if (newLevel > get_block_light(nx, ny, nz)) {
                set_block_light(nx, ny, nz, newLevel);
                if (newLevel > 0) blockQueue.push({nx, ny, nz});
            }
        }
    }
}

ChunkMeshBuildResult Chunk::build_mesh_data(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                                             const Chunk* northwest, const Chunk* northeast,
                                             const Chunk* southwest, const Chunk* southeast) const
{
    ChunkMeshBuildResult result;

    // Built up separately since they're drawn separately - see
    // transparent_layer_count()/draw_water(). One bucket per distinct
    // transparent BlockType, created on first use (bucket_for()).
    // result.opaque/result.water are filled in place; transparent_buckets
    // is a build-time-only accumulator (see its own comment) converted into
    // result.transparent once the scan below finishes.
    ChunkMeshBuffers& opaque_data = result.opaque;
    std::vector<TransparentBuildBucket> transparent_buckets;
    ChunkMeshBuffers& water_data = result.water;
    Neighborhood nb{this, west, east, north, south, northwest, northeast, southwest, southeast};

    // Accumulated per block (not per face - a block with more visible
    // faces shouldn't weigh more) into water_avg_y once the loop below
    // finishes - see its own comment in Chunk.hpp. Each transparent
    // bucket accumulates the same way into its own y_sum/y_count instead.
    double water_y_sum = 0.0;
    int water_y_count = 0;

    // A coordinate that steps outside this chunk's own 0..CHUNK_SIZE-1 range
    // is looked up in the appropriate neighbor instead of being treated as
    // open - that neighbor's own block data has already been generated by
    // the time build_mesh() runs. A null neighbor (the edge of the world),
    // same as stepping above/below the world on Y, reads as Air. A face's
    // own normal only ever steps one axis at a time, so the diagonal
    // neighbors in `nb` never come into play here (they matter for
    // vertex_ao/vertex_light below, whose corner samples can step two axes
    // at once).
    auto neighbor_block = [&](int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return BlockType::Air; // above/below the world, not a neighbor chunk

        const Chunk* neighbor = nullptr;
        if (x < 0)              { neighbor = west;  x += CHUNK_SIZE; }
        else if (x >= CHUNK_SIZE) { neighbor = east;  x -= CHUNK_SIZE; }
        else if (z < 0)          { neighbor = north; z += CHUNK_SIZE; }
        else if (z >= CHUNK_SIZE) { neighbor = south; z -= CHUNK_SIZE; }
        else return get_block(x, y, z); // still inside this chunk

        return neighbor == nullptr ? BlockType::Air : neighbor->get_block(x, y, z);
    };

    auto neighbor_fluid_level = [&](int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return FLUID_LEVEL_SOURCE;

        const Chunk* neighbor = nullptr;
        if (x < 0)                { neighbor = west;  x += CHUNK_SIZE; }
        else if (x >= CHUNK_SIZE) { neighbor = east;  x -= CHUNK_SIZE; }
        else if (z < 0)           { neighbor = north; z += CHUNK_SIZE; }
        else if (z >= CHUNK_SIZE) { neighbor = south; z -= CHUNK_SIZE; }
        else return get_fluid_level(x, y, z);

        return neighbor == nullptr ? FLUID_LEVEL_SOURCE : neighbor->get_fluid_level(x, y, z);
    };

    // Bounded to highest_block_y, not CHUNK_HEIGHT: everything above it is
    // guaranteed air in every column of this chunk, so there's nothing
    // there to ever emit a face.
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y <= highest_block_y; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                BlockType type = get_block(x, y, z);
                if (type == BlockType::Air) continue;

                // Mesh-local, not world-space: draw()'s transform matrix
                // places the whole mesh at this chunk's world position.
                Vector3 center = {x + 0.5f, y + 0.5f, z + 0.5f};

                const BlockProperties& properties = get_block_properties(type);
                // Translucent blocks need depth writes off; alpha-cutout
                // blocks do not. Render-layer selection is data-driven so
                // future flowers/leaves don't require enum special cases.
                ChunkMeshBuffers* mesh_data_ptr;
                if (properties.translucent) {
                    mesh_data_ptr = &water_data;
                    water_y_sum += y;
                    ++water_y_count;
                } else if (properties.transparent && !properties.cutout) {
                    TransparentBuildBucket& bucket = bucket_for(transparent_buckets, type);
                    bucket.y_sum += y;
                    ++bucket.y_count;
                    mesh_data_ptr = &bucket.data;
                } else {
                    // Alpha-cutout geometry is not translucent: its
                    // visible pixels belong in the depth-writing pass and
                    // chunk.fs discards only the holes. Treating it like
                    // glass made whole canopies order-dependent.
                    mesh_data_ptr = &opaque_data;
                }
                ChunkMeshBuffers& mesh_data = *mesh_data_ptr;

                if (properties.render_shape == BlockRenderShape::Cross) {
                    // No AO/face-direction shading for a cross shape (both
                    // are cube-face concepts) - shade stays flat 1.0, and
                    // the block's own single sky/block cell (no per-vertex
                    // "smooth lighting" sampling either, same as before)
                    // covers every corner of every cross quad alike.
                    float shade[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    float ao[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // no occlusion - cross shapes have none
                    // Raw, not floored - see vertex_light()'s own comment
                    // on why the MIN_LIGHT_FRACTION floor now lives in
                    // chunk.fs instead, applied after both day/night and
                    // the brightness slider's own gamma curve.
                    float sky = static_cast<float>(get_sky_light(x, y, z)) / static_cast<float>(MAX_LIGHT);
                    float block = static_cast<float>(get_block_light(x, y, z)) / static_cast<float>(MAX_LIGHT);
                    float sky_fraction[4] = {sky, sky, sky, sky};
                    float block_fraction[4] = {block, block, block, block};
                    Color tint = type == BlockType::ShortGrass
                        ? column_grass_tint[z * CHUNK_SIZE + x]
                        : properties.texture_tints[static_cast<int>(BlockFace::North)];
                    for (const Face& cross_face : CROSS_FACES) {
                        append_face(mesh_data, cross_face, center,
                            properties.texture_uvs[static_cast<int>(BlockFace::North)],
                            shade, sky_fraction, block_fraction, ao, tint);
                    }
                    continue;
                }

                if (properties.render_shape == BlockRenderShape::Shaped) {
                    // Stairs/trapdoors/doors/beds/cake: 0-2 boxes from
                    // get_block_shape(), each rendered as its own mini
                    // six-face cube (unit_cube_faces() at that box's own
                    // half-extents). Faces hidden by another box of the
                    // same shape or by a full opaque neighbor are culled,
                    // and visible corners get the same AO/smooth-lighting
                    // sampling ordinary cube faces use.
                    BlockInstanceState state = unpack_block_state(get_orientation(x, y, z), get_block_state(x, y, z));
                    BlockShapeBoxes shape = get_block_shape(type, state);

                    for (int b = 0; b < shape.count; ++b) {
                        const BoundingBox& box = shape.boxes[b];
                        Vector3 half = {
                            (box.max.x - box.min.x) * 0.5f,
                            (box.max.y - box.min.y) * 0.5f,
                            (box.max.z - box.min.z) * 0.5f,
                        };
                        // Local 0..1 box space -> mesh-local space centered
                        // on this cell's own center (same origin CUBE_FACES
                        // itself uses) - box.min/max are relative to the
                        // cell's min corner, `center` (x+0.5,y+0.5,z+0.5) is
                        // relative to the cell's own center, so the box's
                        // own center needs that same -0.5 recentering.
                        Vector3 box_center = {
                            center.x + (box.min.x + box.max.x) * 0.5f - 0.5f,
                            center.y + (box.min.y + box.max.y) * 0.5f - 0.5f,
                            center.z + (box.min.z + box.max.z) * 0.5f - 0.5f,
                        };
                        std::array<Face, 6> box_faces = unit_cube_faces(half);
                        for (int face = 0; face < 6; ++face) {
                            BlockFace block_face = static_cast<BlockFace>(face);
                            BoundingBox face_rect = face_rect_for_box(box, block_face);
                            if (shape_covers_face(shape.boxes.data(), shape.count, block_face,
                                                  face_plane_for_box(box, block_face), face_rect)) {
                                continue;
                            }

                            const Face& box_face = box_faces[face];
                            int nx = x + static_cast<int>(box_face.normal.x);
                            int ny = y + static_cast<int>(box_face.normal.y);
                            int nz = z + static_cast<int>(box_face.normal.z);
                            if (shaped_face_on_cell_boundary(box, block_face) &&
                                !get_block_properties(neighbor_block(nx, ny, nz)).transparent) {
                                continue;
                            }

                            // Tile choice, crop and orientation all live in
                            // shaped_face_texture() (core/BlockShape.hpp).
                            ShapedFaceTexture texture = shaped_face_texture(
                                type, state, properties, block_face, box);
                            if (texture.hidden) continue;
                            const float shade_value = texture.flat_shade ? 1.0f : FACE_DIRECTION_SHADE[face];
                            Vector3 face_center = Vector3Subtract(box_center, Vector3Scale(box_face.normal, texture.inset));
                            Face textured_face = box_face;
                            for (int turn = 0; turn < texture.quarter_turns; ++turn) {
                                Vector3 first = textured_face.v1;
                                textured_face.v1 = textured_face.v2;
                                textured_face.v2 = textured_face.v3;
                                textured_face.v3 = textured_face.v4;
                                textured_face.v4 = first;
                            }

                            Vector3 corners[4] = {textured_face.v1, textured_face.v2, textured_face.v3, textured_face.v4};
                            float shade[4];
                            float ao[4];
                            float sky_fraction[4];
                            float block_fraction[4];
                            for (int i = 0; i < 4; ++i) {
                                int ao_level = texture.flat_shade ? 3 : vertex_ao(nb, x, y, z, textured_face.normal, corners[i]);
                                VertexLight light = vertex_light(nb, x, y, z, textured_face.normal, corners[i]);
                                shade[i] = shade_value;
                                ao[i] = AO_BRIGHTNESS[ao_level];
                                sky_fraction[i] = light.sky;
                                block_fraction[i] = light.block;
                            }
                            append_face(mesh_data, textured_face, face_center, texture.uv,
                                shade, sky_fraction, block_fraction, ao, properties.texture_tints[face]);
                        }
                    }
                    continue;
                }

                if (type == BlockType::Water) {
                    const bool covered_by_water = neighbor_block(x, y + 1, z) == BlockType::Water;
                    const Rectangle water_uv = get_sample_safe_block_uv(properties.texture_uvs[static_cast<int>(BlockFace::Top)]);

                    auto water_height_at = [&](int sx, int sz) -> std::optional<float> {
                        if (neighbor_block(sx, y, sz) != BlockType::Water) return std::nullopt;
                        if (neighbor_block(sx, y + 1, sz) == BlockType::Water) return 1.0f;
                        return water_surface_height(neighbor_fluid_level(sx, y, sz));
                    };

                    auto corner_height = [&](int dx, int dz) {
                        float total = 0.0f;
                        int count = 0;
                        const int sample_offsets[4][2] = {{0, 0}, {dx, 0}, {0, dz}, {dx, dz}};
                        for (const auto& offset : sample_offsets) {
                            if (auto height = water_height_at(x + offset[0], z + offset[1])) {
                                total += *height;
                                ++count;
                            }
                        }
                        return count > 0 ? total / static_cast<float>(count) : water_surface_height(get_fluid_level(x, y, z));
                    };

                    const float nw = covered_by_water ? 1.0f : corner_height(-1, -1);
                    const float sw = covered_by_water ? 1.0f : corner_height(-1,  1);
                    const float se = covered_by_water ? 1.0f : corner_height( 1,  1);
                    const float ne = covered_by_water ? 1.0f : corner_height( 1, -1);

                    auto append_water_quad = [&](const Vector3 water_corners[4], Vector3 normal, BlockFace face,
                                                 const float u[4], const float v[4]) {
                        float shade[4];
                        float ao_strength[4];
                        float sky_fraction[4];
                        float block_fraction[4];
                        for (int i = 0; i < 4; ++i) {
                            int ao_level = vertex_ao(nb, x, y, z, normal, water_corners[i]);
                            VertexLight light = vertex_light(nb, x, y, z, normal, water_corners[i]);
                            shade[i] = FACE_DIRECTION_SHADE[static_cast<int>(face)];
                            ao_strength[i] = AO_BRIGHTNESS[ao_level];
                            sky_fraction[i] = light.sky;
                            block_fraction[i] = light.block;
                        }
                        append_custom_face(water_data, water_corners, normal, center, u, v,
                                           shade, sky_fraction, block_fraction, ao_strength,
                                           properties.texture_tints[static_cast<int>(face)]);
                    };

                    if (!covered_by_water) {
                        Vector3 top_corners[4] = {
                            {-HALF, nw - HALF, -HALF},
                            {-HALF, sw - HALF,  HALF},
                            { HALF, se - HALF,  HALF},
                            { HALF, ne - HALF, -HALF},
                        };
                        float u[4] = {water_uv.x, water_uv.x + water_uv.width, water_uv.x + water_uv.width, water_uv.x};
                        float v[4] = {water_uv.y, water_uv.y, water_uv.y + water_uv.height, water_uv.y + water_uv.height};
                        append_water_quad(top_corners, {0.0f, 1.0f, 0.0f}, BlockFace::Top, u, v);
                    }

                    struct WaterSide {
                        BlockFace face;
                        int dx;
                        int dz;
                        Vector3 normal;
                        float h0;
                        float h1;
                        Vector3 corners[4];
                    };
                    WaterSide sides[4] = {
                        {BlockFace::North, 0, -1, {0.0f, 0.0f, -1.0f}, nw, ne,
                         {{-HALF, nw - HALF, -HALF}, { HALF, ne - HALF, -HALF}, { HALF, -HALF, -HALF}, {-HALF, -HALF, -HALF}}},
                        {BlockFace::South, 0,  1, {0.0f, 0.0f,  1.0f}, se, sw,
                         {{ HALF, se - HALF,  HALF}, {-HALF, sw - HALF,  HALF}, {-HALF, -HALF,  HALF}, { HALF, -HALF,  HALF}}},
                        {BlockFace::East,  1,  0, {1.0f, 0.0f,  0.0f}, ne, se,
                         {{ HALF, ne - HALF, -HALF}, { HALF, se - HALF,  HALF}, { HALF, -HALF,  HALF}, { HALF, -HALF, -HALF}}},
                        {BlockFace::West, -1,  0, {-1.0f, 0.0f, 0.0f}, sw, nw,
                         {{-HALF, sw - HALF,  HALF}, {-HALF, nw - HALF, -HALF}, {-HALF, -HALF, -HALF}, {-HALF, -HALF,  HALF}}},
                    };
                    for (const WaterSide& side : sides) {
                        BlockType neighbor_type = neighbor_block(x + side.dx, y, z + side.dz);
                        if (neighbor_type == BlockType::Water || !get_block_properties(neighbor_type).transparent) continue;
                        if (!get_block_properties(neighbor_block(x, y - 1, z)).transparent &&
                            !get_block_properties(neighbor_block(x + side.dx, y - 1, z + side.dz)).transparent) {
                            continue;
                        }

                        Rectangle uv = get_sample_safe_block_uv(properties.texture_uvs[static_cast<int>(side.face)]);
                        float u[4] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
                        float v[4] = {
                            uv.y + uv.height * (1.0f - side.h0),
                            uv.y + uv.height * (1.0f - side.h1),
                            uv.y + uv.height,
                            uv.y + uv.height,
                        };
                        append_water_quad(side.corners, side.normal, side.face, u, v);
                    }

                    if (get_block_properties(neighbor_block(x, y - 1, z)).transparent &&
                        neighbor_block(x, y - 1, z) != BlockType::Water) {
                        const Face& bottom = CUBE_FACES[static_cast<int>(BlockFace::Bottom)];
                        Vector3 bottom_corners[4] = {bottom.v1, bottom.v2, bottom.v3, bottom.v4};
                        Rectangle uv = get_sample_safe_block_uv(properties.texture_uvs[static_cast<int>(BlockFace::Bottom)]);
                        float u[4] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
                        float v[4] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
                        append_water_quad(bottom_corners, bottom.normal, BlockFace::Bottom, u, v);
                    }
                    continue;
                }

                // Water-only: surface height depends on this cell's fluid
                // level, so sources/falling columns look nearly full while
                // levels 1..7 visually step down across the flow.
                float top_drop = 0.0f;
                float water_height = 1.0f;
                if (type == BlockType::Water) {
                    water_height = water_surface_height(get_fluid_level(x, y, z));
                    if (neighbor_block(x, y + 1, z) != BlockType::Water) {
                        top_drop = 1.0f - water_height;
                    }
                }

                for (int face = 0; face < 6; ++face) {
                    const Face& f = CUBE_FACES[face];

                    int nx = x + static_cast<int>(f.normal.x);
                    int ny = y + static_cast<int>(f.normal.y);
                    int nz = z + static_cast<int>(f.normal.z);
                    BlockType neighbor_type = neighbor_block(nx, ny, nz);

                    // Hidden-face culling: a face whose neighbor is opaque
                    // can never be seen, so it's left out of the mesh
                    // entirely rather than drawn and hidden behind it. A
                    // face between two blocks of the exact same transparent
                    // type (e.g. two water blocks, or two foliage/glass
                    // blocks) is skipped the same way - Minecraft doesn't
                    // draw the water-water or leaves-leaves (or glass-glass)
                    // boundary inside a solid body of it either, only where
                    // it meets something actually different (including
                    // Air). Different transparent types still show their
                    // shared face normally - glass against foliage, say.
                    // An inset side face (cactus - see BlockProperties::
                    // side_inset) sits inside its own cell rather than on
                    // the shared boundary, so a neighbor can't hide it:
                    // always drawn.
                    bool inset_side = properties.side_inset > 0.0f && face >= static_cast<int>(BlockFace::North);
                    if (!inset_side) {
                        if (!get_block_properties(neighbor_type).transparent) continue;
                        // Leaves deliberately keep faces against other
                        // leaves: their alpha layers and AO accumulate
                        // inward, making a dense canopy darker than its
                        // exposed outside.
                        if (properties.transparent && neighbor_type == type && properties.cull_same_faces) {
                            bool hidden_by_same_type = true;
                            if (type == BlockType::Water && face >= static_cast<int>(BlockFace::North)) {
                                float neighbor_height = water_surface_height(neighbor_fluid_level(nx, ny, nz));
                                hidden_by_same_type = neighbor_height >= water_height - 0.001f;
                            }
                            if (hidden_by_same_type) continue;
                        }
                    }
                    Vector3 face_center = inset_side
                        ? Vector3Subtract(center, Vector3Scale(f.normal, properties.side_inset))
                        : center;

                    Vector3 corners[4] = {f.v1, f.v2, f.v3, f.v4};
                    float shade[4];
                    float ao_strength[4];
                    float sky_fraction[4];
                    float block_fraction[4];
                    for (int i = 0; i < 4; ++i) {
                        int ao_level = vertex_ao(nb, x, y, z, f.normal, corners[i]);
                        VertexLight light = vertex_light(nb, x, y, z, f.normal, corners[i]);
                        shade[i] = FACE_DIRECTION_SHADE[face];
                        ao_strength[i] = AO_BRIGHTNESS[ao_level];
                        sky_fraction[i] = light.sky;
                        block_fraction[i] = light.block;
                    }

                    // A grass top's tint depends on this column's own blend
                    // of biomes (column_grass_tint, precomputed in
                    // generate_terrain) instead of the one fixed color
                    // blocks.json's texture_tints would give every Grass
                    // block regardless of where it is. Water's own tint
                    // deliberately does *not* vary with depth the same way
                    // - it stays blocks.json's one plain color everywhere,
                    // same as real Minecraft's water surface; depth instead
                    // darkens *visibility* (World::draw's underwater fog
                    // override, set_chunk_fog), not the water block itself.
                    Color tint = properties.texture_tints[face];
                    if (type == BlockType::Grass && face == static_cast<int>(BlockFace::Top)) {
                        tint = column_grass_tint[z * CHUNK_SIZE + x];
                    } else if (type == BlockType::Foliage) {
                        tint = column_foliage_tint[z * CHUNK_SIZE + x];
                    }

                    // A directional block's front art is always baked under
                    // texture_uvs[South] and everything else falls back to
                    // texture_uvs[East]'s plain side texture (see
                    // is_directional_block()'s own comment) - get_orientation()
                    // says which actual world-facing side this placed
                    // instance's front should appear on, so remap which of
                    // those two slots backs *this* face instead of always
                    // reading straight off texture_uvs[face].
                    int texture_face = face;
                    Rectangle face_uv;
                    bool face_uv_overridden = false;
                    if (block_is_directional(type) && face >= static_cast<int>(BlockFace::North)) {
                        HorizontalDirection facing = get_orientation(x, y, z);
                        bool is_front = face == static_cast<int>(BlockFace::North) + static_cast<int>(facing);
                        texture_face = is_front ? static_cast<int>(BlockFace::South) : static_cast<int>(BlockFace::East);

                        // A large/double chest's front/back faces get their
                        // own dedicated two-tile-wide art instead of the
                        // ordinary single-chest texture - which half
                        // (primary/secondary) picks left vs. right tile.
                        // Every other face keeps the plain single-chest
                        // side texture, same as texture_face above already
                        // gives it.
                        if (type == BlockType::Chest) {
                            uint16_t packed = get_block_state(x, y, z);
                            ChestPart part = static_cast<ChestPart>(
                                (packed & BlockStateBits::MULTIBLOCK_PART_MASK) >> BlockStateBits::MULTIBLOCK_PART_SHIFT);
                            if (part != ChestPart::Single) {
                                bool is_back = face == static_cast<int>(BlockFace::North) + (static_cast<int>(facing) ^ 1);
                                if (is_front) {
                                    face_uv = block_atlas_tile_uv(part == ChestPart::Primary ? 9 : 10, 2);
                                    face_uv_overridden = true;
                                } else if (is_back) {
                                    face_uv = block_atlas_tile_uv(part == ChestPart::Primary ? 9 : 10, 3);
                                    face_uv_overridden = true;
                                }
                            }
                        }
                    }
                    if (!face_uv_overridden) face_uv = properties.texture_uvs[texture_face];
                    float uv_v0 = 0.0f;
                    float uv_v1 = 1.0f;
                    if (type == BlockType::Water && face >= static_cast<int>(BlockFace::North) && top_drop > 0.0f) {
                        float visible_height = 1.0f - top_drop;
                        uv_v0 = 1.0f - visible_height;
                    }
                    append_face(mesh_data, f, face_center, face_uv,
                        shade, sky_fraction, block_fraction, ao_strength, tint, top_drop, uv_v0, uv_v1);
                }
            }
        }
    }

    result.water_avg_y = water_y_count > 0 ? static_cast<float>(water_y_sum / water_y_count) : 0.0f;

    // Convert each build-time bucket (double-precision y_sum/y_count) into
    // its ChunkMeshBuildResult counterpart (just the finished avg_y) - see
    // TransparentBuildBucket's own comment.
    for (TransparentBuildBucket& bucket : transparent_buckets) {
        if (bucket.data.positions.empty()) continue;

        ChunkMeshTransparentBucket out;
        out.type = bucket.type;
        out.avg_y = bucket.y_count > 0 ? static_cast<float>(bucket.y_sum / bucket.y_count) : 0.0f;
        out.data = std::move(bucket.data);
        result.transparent.push_back(std::move(out));
    }

    return result;
}

void Chunk::upload_mesh_data(ChunkMeshBuildResult&& result)
{
    if (mesh_uploaded) {
        UnloadMesh(mesh);
        mesh = Mesh{};
        mesh_uploaded = false;
    }
    for (TransparentLayer& layer : transparent_layers) {
        if (layer.uploaded) UnloadMesh(layer.mesh);
    }
    transparent_layers.clear();
    if (water_mesh_uploaded) {
        UnloadMesh(water_mesh);
        water_mesh = Mesh{};
        water_mesh_uploaded = false;
    }

    water_avg_y = result.water_avg_y;

    mesh_uploaded = upload_buffers(mesh, result.opaque);

    for (ChunkMeshTransparentBucket& bucket : result.transparent) {
        TransparentLayer layer;
        layer.avg_y = bucket.avg_y;
        layer.uploaded = upload_buffers(layer.mesh, bucket.data);
        if (layer.uploaded) transparent_layers.push_back(layer);
    }

    water_mesh_uploaded = upload_buffers(water_mesh, result.water);
}

void Chunk::build_mesh(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                        const Chunk* northwest, const Chunk* northeast,
                        const Chunk* southwest, const Chunk* southeast)
{
    upload_mesh_data(build_mesh_data(west, east, north, south, northwest, northeast, southwest, southeast));
}

void Chunk::draw() const
{
    if (!mesh_uploaded) return;

    Vector3 origin = get_position();
    DrawMesh(mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}

void Chunk::draw_transparent_layer(size_t index) const
{
    const TransparentLayer& layer = transparent_layers[index];
    if (!layer.uploaded) return;

    // Same material as draw()'s opaque mesh and draw_water() - only the GL
    // blend/depth state around this call differs, and that's World::draw()'s
    // job (see its own comment): every chunk's draw() needs to happen
    // before every chunk's draw_transparent_layer()/draw_water().
    Vector3 origin = get_position();
    DrawMesh(layer.mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}

void Chunk::draw_water() const
{
    if (!water_mesh_uploaded) return;

    // Same material (texture, shader, fog) as draw()'s opaque mesh - only
    // the GL blend/depth state around this call differs, and that's
    // World::draw()'s job, not this one's: every chunk's draw() needs to
    // happen before every chunk's draw_water() (see World::draw()'s own
    // comment), so batching that decision per-chunk here wouldn't work.
    Vector3 origin = get_position();
    DrawMesh(water_mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}
