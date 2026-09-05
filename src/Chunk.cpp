#include "Chunk.hpp"
#include "core/PerlinNoise.hpp"

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
    // octaves deep for detail, mapped onto a height band centered on
    // BASE_HEIGHT. The band has to fit inside the chunk's 0..CHUNK_SIZE-1
    // column (no vertical chunk stacking yet), so it's kept well within that.
    constexpr float NOISE_FREQUENCY = 1.0f / 96.0f;
    constexpr int NOISE_OCTAVES = 4;
    constexpr int BASE_HEIGHT = 8;
    constexpr int HEIGHT_VARIATION = 5;
    constexpr int DIRT_DEPTH = 3; // layers of dirt just under the grass top

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
        if (y < 0 || y >= CHUNK_SIZE) return false;
        const Chunk* chunk = nb.resolve(x, z);
        if (chunk == nullptr) return false;
        return get_block_properties(chunk->get_block(x, y, z)).solid;
    }

    // Light lookup, resolved the same way — a sample that steps outside the
    // chunk being meshed reads the neighbor's real computed light instead
    // of assuming full sky light. Out-of-range on y or a missing neighbor
    // still reads as open, sunlit space, same as before.
    int light_at(const Neighborhood& nb, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_SIZE) return MAX_LIGHT;
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
    // ever need anything other than white.
    void append_face(MeshData& mesh_data, const Face& face, Vector3 center, Rectangle uv, const float brightness[4], Color tint) {
        Vector3 corners[4] = {face.v1, face.v2, face.v3, face.v4};
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
            mesh_data.colors.push_back(255);
        }
    }

    // Every chunk's mesh samples the same block texture atlas, so they all
    // share one Material — built lazily so it's only touched once
    // Load_block_definitions() (and so the atlas texture) has already run.
    Material& get_chunk_material() {
        static Material material = [] {
            Material m = LoadMaterialDefault();
            SetMaterialTexture(&m, MATERIAL_MAP_DIFFUSE, get_block_atlas_texture());
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
}

void Chunk::generate_terrain(const PerlinNoise& noise)
{
    Vector3 origin = get_position();

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            float world_x = origin.x + x;
            float world_z = origin.z + z;
            float sample = noise.fractal(world_x * NOISE_FREQUENCY, world_z * NOISE_FREQUENCY, NOISE_OCTAVES);

            int height = BASE_HEIGHT + static_cast<int>(std::lround(sample * HEIGHT_VARIATION));
            height = std::clamp(height, 1, CHUNK_SIZE - 1);

            for (int y = 0; y < CHUNK_SIZE; ++y) {
                BlockType type;
                if (y == 0) {
                    type = BlockType::Bedrock;
                } else if (y > height) {
                    type = BlockType::Air;
                } else if (y == height) {
                    type = BlockType::Grass;
                } else if (y > height - DIRT_DEPTH) {
                    type = BlockType::Dirt;
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
}

bool Chunk::is_opaque(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
        return false;
    }
    return !get_block_properties(get_block(x, y, z)).transparent;
}

int Chunk::get_sky_light(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
        // No neighbor-chunk data yet (same as is_opaque) — assume open,
        // sunlit space rather than reading as pitch black at chunk edges.
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
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
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
    light.fill(0);

    using Cell = std::array<int, 3>;
    std::queue<Cell> skyQueue;
    std::queue<Cell> blockQueue;

    // Direct sky exposure: scan each column from the top, stop at the first
    // opaque block. Cells below it get lit later, if at all, by the BFS
    // spreading sideways from a neighboring open column.
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = CHUNK_SIZE - 1; y >= 0; --y) {
                if (is_opaque(x, y, z)) break;
                set_sky_light(x, y, z, MAX_LIGHT);
                skyQueue.push({x, y, z});
            }
        }
    }

    // Block light sources: any block with luminance > 0.
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
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
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
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
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
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

    MeshData mesh_data;
    Neighborhood nb{this, west, east, north, south, northwest, northeast, southwest, southeast};

    // Like is_opaque(), but a coordinate that steps outside this chunk's own
    // 0..CHUNK_SIZE-1 range is looked up in the appropriate neighbor instead
    // of being treated as open — that neighbor's own block data has already
    // been generated by the time build_mesh() runs. A null neighbor (the
    // edge of the world) still counts as open, same as before. A face's own
    // normal only ever steps one axis at a time, so the diagonal neighbors
    // in `nb` never come into play here (they matter for vertex_ao/
    // vertex_light below, whose corner samples can step two axes at once).
    auto neighbor_opaque = [&](int x, int y, int z) {
        if (y < 0 || y >= CHUNK_SIZE) return false; // no vertical chunk stacking yet

        const Chunk* neighbor = nullptr;
        if (x < 0)              { neighbor = west;  x += CHUNK_SIZE; }
        else if (x >= CHUNK_SIZE) { neighbor = east;  x -= CHUNK_SIZE; }
        else if (z < 0)          { neighbor = north; z += CHUNK_SIZE; }
        else if (z >= CHUNK_SIZE) { neighbor = south; z -= CHUNK_SIZE; }
        else return is_opaque(x, y, z); // still inside this chunk

        if (neighbor == nullptr) return false;
        return !get_block_properties(neighbor->get_block(x, y, z)).transparent;
    };

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                BlockType type = get_block(x, y, z);
                if (type == BlockType::Air) continue;

                // Mesh-local, not world-space: draw()'s transform matrix
                // places the whole mesh at this chunk's world position.
                Vector3 center = {x + 0.5f, y + 0.5f, z + 0.5f};

                const BlockProperties& properties = get_block_properties(type);
                for (int face = 0; face < 6; ++face) {
                    const Face& f = CUBE_FACES[face];

                    // Hidden-face culling: a face whose neighbor is opaque
                    // can never be seen, so it's left out of the mesh
                    // entirely rather than drawn and hidden behind it.
                    int nx = x + static_cast<int>(f.normal.x);
                    int ny = y + static_cast<int>(f.normal.y);
                    int nz = z + static_cast<int>(f.normal.z);
                    if (neighbor_opaque(nx, ny, nz)) continue;

                    Vector3 corners[4] = {f.v1, f.v2, f.v3, f.v4};
                    float brightness[4];
                    for (int i = 0; i < 4; ++i) {
                        int ao = vertex_ao(nb, x, y, z, f.normal, corners[i]);
                        float light_fraction = vertex_light(nb, x, y, z, f.normal, corners[i]);
                        brightness[i] = AO_BRIGHTNESS[ao] * light_fraction;
                    }
                    append_face(mesh_data, f, center, properties.texture_uvs[face], brightness, properties.texture_tints[face]);
                }
            }
        }
    }

    mesh.vertexCount = static_cast<int>(mesh_data.positions.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    if (mesh.vertexCount == 0) {
        return; // an all-air chunk (e.g. above the terrain height): nothing to draw
    }

    mesh.vertices = to_mesh_buffer(mesh_data.positions);
    mesh.normals = to_mesh_buffer(mesh_data.normals);
    mesh.texcoords = to_mesh_buffer(mesh_data.texcoords);
    mesh.colors = to_mesh_buffer(mesh_data.colors);

    UploadMesh(&mesh, false);
    mesh_uploaded = true;
}

void Chunk::draw() const
{
    if (!mesh_uploaded) return;

    Vector3 origin = get_position();
    DrawMesh(mesh, get_chunk_material(), MatrixTranslate(origin.x, origin.y, origin.z));
}
