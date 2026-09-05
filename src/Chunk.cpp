#include "Chunk.hpp"
#include "core/TerrainNoise.hpp"

#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

namespace {
    constexpr float HALF = 0.5f;

    // Terrain shape: a wavelength-~96-block rolling hill signal, layered 4
    // octaves deep for detail (Beta 1.7.3-style layering: this is itself a
    // sum of multiple noise octaves, then World Generation layers biome
    // selection — a whole separate pair of noise maps, see TerrainNoise —
    // on top of that). Mapped onto a height band centered on each biome's
    // own BASE_HEIGHT (comfortably inside the chunk's own local
    // 0..CHUNK_HEIGHT-1 column). Chunk stays plainly 0-based internally
    // (see MIN_WORLD_Y in Chunk.hpp) — BiomeTerrain's heights below are
    // LOCAL, offset by -MIN_WORLD_Y (+64) from the world-space heights
    // they're meant to represent, so e.g. a base_height of 6 there means
    // world Y ~70, not local Y 70.
    constexpr float NOISE_FREQUENCY = 1.0f / 96.0f;
    constexpr int NOISE_OCTAVES = 4;

    // Sea level: any column whose terrain height falls below this fills the
    // gap with water up to it. World Y 64, same idea (and the same absolute
    // coordinate) as Minecraft's own sea level.
    constexpr int WATER_LEVEL = 64 - MIN_WORLD_Y;

    // Still water's top face sits slightly below a full block's own top,
    // same as real Minecraft (14/16 tall, i.e. 2/16 below the top) — applied
    // in append_face() via its top_drop parameter, only to the block's
    // upward-facing corners (see there), so this doesn't touch the actual
    // block grid Chunk::get_block/is_opaque/collision reasoning uses: a
    // water "block" still logically fills its full cell, only the rendered
    // mesh is shorter.
    constexpr float WATER_SURFACE_DROP = 2.0f / 16.0f;

    // Per-biome terrain shape and surface/subsurface blocks — the same
    // overall column structure (bedrock, subsurface, surface, water/air)
    // for every biome, just with each biome's own numbers and blocks
    // dropped in, matching Beta 1.7.3's approach of biomes carrying both a
    // look (surface blocks) and a characteristic terrain shape rather than
    // just a color. Deliberately gentler than modern Minecraft's Extreme
    // Hills (Beta 1.7.3 predates that terrain rework) — even Hills here
    // stays well short of dramatic cliffs. base_height/height_variation
    // aren't used directly any more (see generate_terrain: every biome's
    // numbers are blended by BiomeWeights instead of picking just one) —
    // surface_block/subsurface_block/surface_depth still are, for whichever
    // biome ends up dominant at a given column.
    struct BiomeTerrain {
        int base_height;             // local; see the comment above on the +64 offset
        int height_variation;        // +/- around base_height (an amplitude, not itself a height — no offset needed)
        int surface_depth;           // layers of subsurface_block just under the surface block
        BlockType surface_block;     // the single block at the very top of the column
        BlockType subsurface_block;
    };

    constexpr int DEFAULT_SURFACE_DEPTH = 3;

    BiomeTerrain biome_terrain(Biome biome) {
        switch (biome) {
            case Biome::Ocean:
                // Deep and mostly flat — world Y ~10..30, i.e. up to ~54
                // blocks below sea level (64) at its deepest, so there's
                // real deep water for World::draw's underwater fog override
                // to darken toward at its own deepest (see
                // UNDERWATER_FOG_MAX_DEPTH in World.cpp). Gravel throughout,
                // same as Sea's own floor (see the "дно" requirement) — the
                // difference between the two is depth and beach material,
                // not the floor itself.
                return {20 - MIN_WORLD_Y, 10, 4, BlockType::Gravel, BlockType::Gravel};
            case Biome::Sea:
                // Shallow and calm — world Y ~56..64, mostly right at sea
                // level, so land slopes gently down into it (see the beach
                // override in generate_terrain for the actual shoreline
                // strip) instead of dropping to Ocean's deep floor.
                return {60 - MIN_WORLD_Y, 4, 3, BlockType::Gravel, BlockType::Gravel};
            case Biome::Desert:
                // Flat and dry, with deeper sand than other biomes' subsurface
                // layer before hitting stone — world Y ~56..68, mostly right
                // around sea level.
                return {62 - MIN_WORLD_Y, 6, 6, BlockType::Sand, BlockType::Sand};
            case Biome::Forest:
                // Noticeably hillier than Plains but not dramatic — world Y ~54..90.
                return {72 - MIN_WORLD_Y, 18, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
            case Biome::Hills:
                // The roughest terrain this generates — world Y ~50..114 —
                // but still gentle by modern-Minecraft standards, on purpose.
                // Its tallest peaks break through HILLS_STONE_LINE into bare
                // stone (see generate_terrain), so surface_block here only
                // actually shows up below that line.
                return {82 - MIN_WORLD_Y, 32, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
            case Biome::Plains:
            default:
                // The gentlest biome — world Y ~58..78.
                return {68 - MIN_WORLD_Y, 10, DEFAULT_SURFACE_DEPTH, BlockType::Grass, BlockType::Dirt};
        }
    }

    // Above this world height, Hills' surface turns to bare stone instead
    // of grass/dirt — a simple tree-line/rocky-peak effect for its tallest
    // terrain, the one place this generator lets a biome's own surface
    // block depend on height rather than purely on position.
    constexpr int HILLS_STONE_LINE = 95 - MIN_WORLD_Y;

    // Rivers: TerrainNoise::river() is an ordinary noise field (roughly
    // [-1, 1]) — wherever its *absolute value* drops under RIVER_WIDTH, the
    // blended height above is pulled down toward RIVER_BED, tracing that
    // field's zero-contour the way a real river winds rather than running
    // straight. RIVER_WIDTH is in noise units, not blocks — TerrainNoise's
    // own RIVER_FREQUENCY is what actually sets the width in blocks.
    constexpr float RIVER_WIDTH = 0.04f;
    constexpr int RIVER_BED = WATER_LEVEL - 3; // a few blocks under sea level, so a river reliably fills with water

    // Beach: land within a few blocks of sea level, close enough to Sea or
    // Ocean to notice, gets a shoreline material instead of its own
    // biome's usual surface block — sand for a calm Sea coastline, gravel
    // for a "wild" Ocean one with no Sea buffer, matching how the two
    // differ everywhere else (Sea = calm/sandy, Ocean = deep/rocky).
    constexpr int BEACH_HEIGHT_ABOVE_WATER = 3;
    constexpr float BEACH_COAST_WEIGHT = 0.05f; // how much Sea+Ocean weight counts as "close enough" to be a coast

    // Grass top tint per biome — same idea as Minecraft's own per-biome
    // grass color, applied here since the block's own texture tile is a
    // deliberately colorless overlay (see blocks.json's grass "color").
    // Forest and Hills deliberately share the same, slightly darker green;
    // Plains reads noticeably lighter. Desert/Ocean/Sea never generate a
    // Grass block at all, so they don't need their own tint.
    constexpr Color PLAINS_GRASS_TINT = {180, 220, 130, 255};
    constexpr Color FOREST_GRASS_TINT = {124, 189, 107, 255}; // also Hills

    // Blends the two grass tints above by how much Plains vs. Forest/Hills
    // influence this column, so a border between them fades the color
    // gradually instead of switching at whichever point dominant_biome()
    // happens to flip — the same idea as blending terrain height itself.
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

    // AO level (0..3, from vertex_ao) -> brightness multiplier.
    constexpr float AO_BRIGHTNESS[4] = {0.5f, 0.65f, 0.8f, 1.0f};

    struct Face {
        Vector3 v1, v2, v3, v4; // corners, CCW as seen from outside
        Vector3 normal;
    };

    // Indexed by BlockFace (Top, Bottom, North, South, East, West). North/South
    // are -Z/+Z, East/West are +X/-X.
    const std::array<Face, 6> CUBE_FACES = {{
        { {-HALF,  HALF, -HALF}, {-HALF,  HALF,  HALF}, { HALF,  HALF,  HALF}, { HALF,  HALF, -HALF}, { 0.0f,  1.0f,  0.0f} }, // Top
        { {-HALF, -HALF,  HALF}, {-HALF, -HALF, -HALF}, { HALF, -HALF, -HALF}, { HALF, -HALF,  HALF}, { 0.0f, -1.0f,  0.0f} }, // Bottom
        { {-HALF,  HALF, -HALF}, { HALF,  HALF, -HALF}, { HALF, -HALF, -HALF}, {-HALF, -HALF, -HALF}, { 0.0f,  0.0f, -1.0f} }, // North
        { { HALF,  HALF,  HALF}, {-HALF,  HALF,  HALF}, {-HALF, -HALF,  HALF}, { HALF, -HALF,  HALF}, { 0.0f,  0.0f,  1.0f} }, // South
        { { HALF,  HALF, -HALF}, { HALF,  HALF,  HALF}, { HALF, -HALF,  HALF}, { HALF, -HALF, -HALF}, { 1.0f,  0.0f,  0.0f} }, // East
        { {-HALF,  HALF,  HALF}, {-HALF,  HALF, -HALF}, {-HALF, -HALF, -HALF}, {-HALF, -HALF,  HALF}, {-1.0f,  0.0f,  0.0f} }, // West
    }};

    // The 9 chunks (this one plus its 8 border neighbors) a face-corner
    // AO/light sample might land in. A face's own normal steps one cell
    // past one edge; a diagonal corner-of-corner sample (used for AO/light
    // at a convex vertex) steps past two edges at once when the block is
    // also at the chunk's edge on that other axis, landing in a diagonal
    // neighbor rather than a side one. Any entry may be null — the edge of
    // the loaded world — same as a missing side neighbor.
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

    // Light lookup, resolved the same way — a sample that steps outside the
    // chunk being meshed reads the neighbor's real computed light instead
    // of assuming full sky light. Out-of-range on y or a missing neighbor
    // still reads as open, sunlit space, same as before.
    int light_at(const Neighborhood& nb, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_HEIGHT) return MAX_LIGHT;
        const Chunk* chunk = nb.resolve(x, z);
        if (chunk == nullptr) return MAX_LIGHT;
        return chunk->get_light(x, y, z);
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
        // is empty — otherwise convex corners get a visible bright seam.
        if (s1 && s2) return 0;
        return 3 - (static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cc));
    }

    // Average light (0..1) of the same three neighbor cells used for AO, plus
    // the cell right outside the face — the same per-vertex sampling
    // Minecraft calls "smooth lighting".
    float vertex_light(const Neighborhood& nb, int x, int y, int z, Vector3 normal, Vector3 corner) {
        NeighborCells cells = compute_neighbor_cells(x, y, z, normal, corner);

        int total = light_at(nb, cells.base[0]  , cells.base[1]  , cells.base[2]  )
                  + light_at(nb, cells.side1[0] , cells.side1[1] , cells.side1[2] )
                  + light_at(nb, cells.side2[0] , cells.side2[1] , cells.side2[2] )
                  + light_at(nb, cells.corner[0], cells.corner[1], cells.corner[2]);

        return (total / 4.0f) / MAX_LIGHT;
    }

    // Growable CPU-side buffers a chunk's mesh is assembled into, one block
    // face at a time, before a single upload to the GPU.
    struct MeshData {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<unsigned char> colors;
    };

    // Appends one face as two triangles (0,1,2) and (0,2,3) — the same quad,
    // split for a Mesh's plain (non-quad) triangle list. `tint` (typically
    // WHITE) is multiplied into each vertex color alongside AO/light
    // brightness — see BlockProperties::texture_tints for why a face would
    // ever need anything other than white. `top_drop` lowers this face's own
    // upward-facing corners (any corner at local y > 0, i.e. Top's own 4
    // corners, or a side face's top edge — never Bottom's, which are all at
    // y < 0) by that many world units — see WATER_SURFACE_DROP, the only
    // current caller that passes anything other than the default 0.
    void append_face(MeshData& mesh_data, const Face& face, Vector3 center, Rectangle uv, const float brightness[4], Color tint, float top_drop = 0.0f) {
        Vector3 corners[4] = {face.v1, face.v2, face.v3, face.v4};
        if (top_drop != 0.0f) {
            for (Vector3& corner : corners) {
                if (corner.y > 0.0f) corner.y -= top_drop;
            }
        }
        // V=0 is the image's top row (raylib doesn't flip on load), so the
        // top edge of the face (corners 0, 1) must sample V=0, not V=1.
        float u[4] = {uv.x,              uv.x + uv.width, uv.x + uv.width, uv.x};
        float v[4] = {uv.y,              uv.y,             uv.y + uv.height, uv.y + uv.height};

        static constexpr int TRIANGLE[6] = {0, 1, 2, 0, 2, 3};
        for (int corner : TRIANGLE) {
            mesh_data.positions.push_back(center.x + corners[corner].x);
            mesh_data.positions.push_back(center.y + corners[corner].y);
            mesh_data.positions.push_back(center.z + corners[corner].z);

            mesh_data.normals.push_back(face.normal.x);
            mesh_data.normals.push_back(face.normal.y);
            mesh_data.normals.push_back(face.normal.z);

            mesh_data.texcoords.push_back(u[corner]);
            mesh_data.texcoords.push_back(v[corner]);

            mesh_data.colors.push_back(static_cast<unsigned char>(brightness[corner] * tint.r));
            mesh_data.colors.push_back(static_cast<unsigned char>(brightness[corner] * tint.g));
            mesh_data.colors.push_back(static_cast<unsigned char>(brightness[corner] * tint.b));
            // Not scaled by brightness, unlike the color channels — alpha is
            // this face's opacity (see BlockProperties::translucent), not
            // part of its shading.
            mesh_data.colors.push_back(tint.a);
        }
    }

    // Loaded once by load_chunk_shader() (called from GameEngine's
    // constructor, alongside Load_block_definitions()/FontManager::get()) —
    // not lazily, so it's clear from the startup sequence exactly when the
    // GL context it needs is required to already exist, same as those.
    // assets/shaders/chunk.{vs,fs}: raylib's own default mesh shader
    // (texture*vertexColor, so AO/tint already baked into vertex colors by
    // Chunk::build_mesh keeps working unchanged) plus linear distance fog.
    Shader chunk_shader{};

    // Every chunk's mesh samples the same block texture atlas, so they all
    // share one Material — built lazily so it's only touched once
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

    // Copies a std::vector into a malloc'd buffer sized to match — Mesh
    // fields must be malloc-compatible since UnloadMesh() frees them with
    // RL_FREE (== free() with raylib's default allocator).
    template <typename T>
    T* to_mesh_buffer(const std::vector<T>& data) {
        T* buffer = static_cast<T*>(std::malloc(data.size() * sizeof(T)));
        std::memcpy(buffer, data.data(), data.size() * sizeof(T));
        return buffer;
    }
}

void load_chunk_shader()
{
    chunk_shader = LoadShader(ASSETS_PATH "shaders/chunk.vs", ASSETS_PATH "shaders/chunk.fs");
}

void set_chunk_fog(Vector3 camera_position, Color fog_color, float fog_start, float fog_end)
{
    // Looked up by name once, not on every call — GetShaderLocation() does
    // a string lookup each time, wasted work for a location that never
    // moves once the shader's compiled.
    static int camera_loc = GetShaderLocation(chunk_shader, "cameraPosition");
    static int color_loc  = GetShaderLocation(chunk_shader, "fogColor");
    static int start_loc  = GetShaderLocation(chunk_shader, "fogStart");
    static int end_loc    = GetShaderLocation(chunk_shader, "fogEnd");

    SetShaderValue(chunk_shader, camera_loc, &camera_position, SHADER_UNIFORM_VEC3);

    float color[3] = {fog_color.r / 255.0f, fog_color.g / 255.0f, fog_color.b / 255.0f};
    SetShaderValue(chunk_shader, color_loc, color, SHADER_UNIFORM_VEC3);
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

void unload_chunk_fog_shader()
{
    UnloadShader(chunk_shader);
}

int Chunk::index(int x, int y, int z)
{
    return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
}

Chunk::Chunk(Vector3 position) : WorldObject(position)
{
    blocks.fill(BlockType::Air);
}

Chunk::~Chunk()
{
    if (mesh_uploaded) {
        UnloadMesh(mesh);
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
            // alone, not of the terrain height about to be generated) —
            // every biome's own height range/amplitude is blended by these
            // instead of picking just one, so crossing a border changes
            // terrain gradually instead of at a seam.
            BiomeWeights weights = noise.biome_weights(world_x, world_z);
            Biome dominant = dominant_biome(weights);
            column_grass_tint[z * CHUNK_SIZE + x] = grass_tint_for_weights(weights);

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
            // banks slope into it instead of a sudden drop) — near a Desert
            // border (weights.desert neither ~0 nor ~1), so rivers
            // specifically cut through arid land rather than appearing
            // between every pair of biomes, and also near any Sea/Ocean
            // coastline, so a river that reaches the coast keeps carving
            // smoothly into it instead of stopping dead right at the
            // shoreline — the same noise field's course now visibly empties
            // into the sea instead of vanishing at the biome border.
            bool near_desert_border = weights.desert > 0.1f && weights.desert < 0.9f;
            bool near_coast = (weights.sea + weights.ocean) > BEACH_COAST_WEIGHT;
            if (near_desert_border || near_coast) {
                float river_distance = std::fabs(noise.river(world_x, world_z));
                float carve = std::clamp(1.0f - river_distance / RIVER_WIDTH, 0.0f, 1.0f);
                height_f = height_f * (1.0f - carve) + RIVER_BED * carve;
            }

            int height = std::clamp(static_cast<int>(std::lround(height_f)), 1, CHUNK_HEIGHT - 1);

            // The dominant biome's own surface blocks — except Hills, whose
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
            // biome would otherwise put there (grass, etc.) — sand for a
            // calm Sea coastline, gravel for a "wild" Ocean one. Only
            // applies on the land side (Sea/Ocean columns already get
            // their own gravel floor from biome_terrain() above).
            bool is_land = dominant != Biome::Sea && dominant != Biome::Ocean;
            if (is_land && near_coast && height <= WATER_LEVEL + BEACH_HEIGHT_ABOVE_WATER) {
                bool wild_coast = weights.ocean > weights.sea;
                surface_block = wild_coast ? BlockType::Gravel : BlockType::Sand;
                subsurface_block = surface_block;
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
        }
    }
}

BlockType Chunk::get_block(int x, int y, int z) const
{
    return blocks[index(x, y, z)];
}

void Chunk::set_block(int x, int y, int z, BlockType type)
{
    blocks[index(x, y, z)] = type;
    // Only ever raises highest_block_y, never lowers it — see its
    // declaration in Chunk.hpp for why that's the safe direction to be
    // wrong in. Covers both generation (each column's own content) and any
    // later player-placed block above it (e.g. a tower).
    if (type != BlockType::Air && y > highest_block_y) {
        highest_block_y = y;
    }
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
        // No neighbor-chunk data yet (same as is_opaque) — assume open,
        // sunlit space rather than reading as pitch black at chunk edges.
        return MAX_LIGHT;
    }
    if (y > highest_block_y) {
        // compute_lighting()'s top-down scan starts at highest_block_y, not
        // CHUNK_HEIGHT-1, since everything above it is guaranteed air in
        // every column of this chunk — so it never actually writes a value
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

void Chunk::compute_lighting()
{
    // Only y <= highest_block_y is ever read back (get_sky_light() reports
    // MAX_LIGHT, without touching the array, for anything above it) — and
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
    // highest_block_y, not CHUNK_HEIGHT-1 — every cell above that is
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
    // way — no block exists above highest_block_y to be a light source.
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

void Chunk::build_mesh(const Chunk* west, const Chunk* east, const Chunk* north, const Chunk* south,
                        const Chunk* northwest, const Chunk* northeast,
                        const Chunk* southwest, const Chunk* southeast)
{
    if (mesh_uploaded) {
        UnloadMesh(mesh);
        mesh = Mesh{};
        mesh_uploaded = false;
    }
    if (water_mesh_uploaded) {
        UnloadMesh(water_mesh);
        water_mesh = Mesh{};
        water_mesh_uploaded = false;
    }

    // Built up separately since they're drawn separately — see draw_water().
    MeshData opaque_data;
    MeshData water_data;
    Neighborhood nb{this, west, east, north, south, northwest, northeast, southwest, southeast};

    // A coordinate that steps outside this chunk's own 0..CHUNK_SIZE-1 range
    // is looked up in the appropriate neighbor instead of being treated as
    // open — that neighbor's own block data has already been generated by
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
                MeshData& mesh_data = properties.translucent ? water_data : opaque_data;

                // Water-only: a block with Water directly above it is
                // interior to a body of water, not its surface (and its Top
                // face is never actually meshed anyway — same-translucent-
                // type culling below skips it) — only a true surface block
                // gets the lowered top geometry (top_drop, WATER_SURFACE_
                // DROP) real still water has in Minecraft.
                float top_drop = 0.0f;
                if (type == BlockType::Water && neighbor_block(x, y + 1, z) != BlockType::Water) {
                    top_drop = WATER_SURFACE_DROP;
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
                    // face between two blocks of the same translucent type
                    // (e.g. two water blocks) is skipped the same way —
                    // Minecraft doesn't draw the water-water (or
                    // glass-glass) boundary inside a solid body of it
                    // either, only where it meets something actually
                    // different.
                    if (!get_block_properties(neighbor_type).transparent) continue;
                    if (properties.translucent && neighbor_type == type) continue;

                    Vector3 corners[4] = {f.v1, f.v2, f.v3, f.v4};
                    float brightness[4];
                    for (int i = 0; i < 4; ++i) {
                        int ao = vertex_ao(nb, x, y, z, f.normal, corners[i]);
                        float light_fraction = vertex_light(nb, x, y, z, f.normal, corners[i]);
                        brightness[i] = AO_BRIGHTNESS[ao] * light_fraction;
                    }

                    // A grass top's tint depends on this column's own blend
                    // of biomes (column_grass_tint, precomputed in
                    // generate_terrain) instead of the one fixed color
                    // blocks.json's texture_tints would give every Grass
                    // block regardless of where it is. Water's own tint
                    // deliberately does *not* vary with depth the same way
                    // — it stays blocks.json's one plain color everywhere,
                    // same as real Minecraft's water surface; depth instead
                    // darkens *visibility* (World::draw's underwater fog
                    // override, set_chunk_fog), not the water block itself.
                    Color tint = properties.texture_tints[face];
                    if (type == BlockType::Grass && face == static_cast<int>(BlockFace::Top)) {
                        tint = column_grass_tint[z * CHUNK_SIZE + x];
                    }
                    append_face(mesh_data, f, center, properties.texture_uvs[face], brightness, tint, top_drop);
                }
            }
        }
    }

    mesh.vertexCount = static_cast<int>(opaque_data.positions.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    if (mesh.vertexCount > 0) {
        mesh.vertices = to_mesh_buffer(opaque_data.positions);
        mesh.normals = to_mesh_buffer(opaque_data.normals);
        mesh.texcoords = to_mesh_buffer(opaque_data.texcoords);
        mesh.colors = to_mesh_buffer(opaque_data.colors);
        UploadMesh(&mesh, false);
        mesh_uploaded = true;
    }

    water_mesh.vertexCount = static_cast<int>(water_data.positions.size() / 3);
    water_mesh.triangleCount = water_mesh.vertexCount / 3;
    if (water_mesh.vertexCount > 0) {
        water_mesh.vertices = to_mesh_buffer(water_data.positions);
        water_mesh.normals = to_mesh_buffer(water_data.normals);
        water_mesh.texcoords = to_mesh_buffer(water_data.texcoords);
        water_mesh.colors = to_mesh_buffer(water_data.colors);
        UploadMesh(&water_mesh, false);
        water_mesh_uploaded = true;
    }
}

void Chunk::draw() const
{
    if (!mesh_uploaded) return;

    Vector3 origin = get_position();
    DrawMesh(mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}

void Chunk::draw_water() const
{
    if (!water_mesh_uploaded) return;

    // Same material (texture, shader, fog) as draw()'s opaque mesh — only
    // the GL blend/depth state around this call differs, and that's
    // World::draw()'s job, not this one's: every chunk's draw() needs to
    // happen before every chunk's draw_water() (see World::draw()'s own
    // comment), so batching that decision per-chunk here wouldn't work.
    Vector3 origin = get_position();
    DrawMesh(water_mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}
