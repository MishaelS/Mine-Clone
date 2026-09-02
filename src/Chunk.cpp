#include "Chunk.hpp"

#include "rlgl.h"

#include <array>
#include <cstdlib>
#include <queue>

namespace {
    constexpr float HALF = 0.5f;
    constexpr int FILL_CHANCE_PERCENT = 15;
    constexpr int MAX_LIGHT = 15;

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

    void draw_face(const Face& face, Vector3 center, Texture2D texture, const float brightness[4]) {
        rlSetTexture(texture.id);
        rlBegin(RL_QUADS);
            rlNormal3f(face.normal.x, face.normal.y, face.normal.z);

            // V=0 is the image's top row (raylib doesn't flip on load), so the
            // top edge of the face (v1, v2) must sample V=0, not V=1.
            unsigned char b0 = static_cast<unsigned char>(brightness[0] * 255.0f);
            rlColor4ub(b0, b0, b0, 255);
            rlTexCoord2f(0.0f, 0.0f);
            rlVertex3f(
                center.x + face.v1.x,
                center.y + face.v1.y,
                center.z + face.v1.z
            );

            unsigned char b1 = static_cast<unsigned char>(brightness[1] * 255.0f);
            rlColor4ub(b1, b1, b1, 255);
            rlTexCoord2f(1.0f, 0.0f);
            rlVertex3f(
                center.x + face.v2.x,
                center.y + face.v2.y,
                center.z + face.v2.z
            );

            unsigned char b2 = static_cast<unsigned char>(brightness[2] * 255.0f);
            rlColor4ub(b2, b2, b2, 255);
            rlTexCoord2f(1.0f, 1.0f);
            rlVertex3f(
                center.x + face.v3.x,
                center.y + face.v3.y,
                center.z + face.v3.z
            );

            unsigned char b3 = static_cast<unsigned char>(brightness[3] * 255.0f);
            rlColor4ub(b3, b3, b3, 255);
            rlTexCoord2f(0.0f, 1.0f);
            rlVertex3f(
                center.x + face.v4.x,
                center.y + face.v4.y,
                center.z + face.v4.z
            );
        rlEnd();
        rlSetTexture(0);
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

void Chunk::randomize()
{
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                bool filled = (rand() % 100) < FILL_CHANCE_PERCENT;
                if (!filled) {
                    set_block(x, y, z, BlockType::Air);
                    continue;
                }

                // Any non-air BlockType (index 0 is Air, skipped).
                constexpr int BLOCK_TYPE_COUNT = static_cast<int>(BlockType::Count);
                set_block(x, y, z, static_cast<BlockType>(1 + rand() % (BLOCK_TYPE_COUNT - 1)));
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

bool Chunk::is_solid(int x, int y, int z) const
{
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
        return false;
    }
    return get_block_properties(get_block(x, y, z)).solid;
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
        // No neighbor-chunk data yet (same as is_solid/is_opaque) — assume open,
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

int Chunk::vertex_ao(int x, int y, int z, Vector3 normal, Vector3 corner) const
{
    NeighborCells cells = compute_neighbor_cells(x, y, z, normal, corner);

    bool s1 = is_solid(cells.side1[0] , cells.side1[1] , cells.side1[2] );
    bool s2 = is_solid(cells.side2[0] , cells.side2[1] , cells.side2[2] );
    bool cc = is_solid(cells.corner[0], cells.corner[1], cells.corner[2]);

    // Two occupied edge-neighbors darken a vertex fully, even if the corner
    // is empty — otherwise convex corners get a visible bright seam.
    if (s1 && s2) return 0;
    return 3 - (static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cc));
}

float Chunk::vertex_light(int x, int y, int z, Vector3 normal, Vector3 corner) const
{
    NeighborCells cells = compute_neighbor_cells(x, y, z, normal, corner);

    int total = get_light(cells.base[0]  , cells.base[1]  , cells.base[2]  )
              + get_light(cells.side1[0] , cells.side1[1] , cells.side1[2] )
              + get_light(cells.side2[0] , cells.side2[1] , cells.side2[2] )
              + get_light(cells.corner[0], cells.corner[1], cells.corner[2]);

    return (total / 4.0f) / MAX_LIGHT;
}

void Chunk::draw() const
{
    Vector3 origin = get_position();
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                BlockType type = get_block(x, y, z);
                if (type == BlockType::Air) continue;

                Vector3 center = {
                    origin.x + x + 0.5f,
                    origin.y + y + 0.5f,
                    origin.z + z + 0.5f,
                };
                const BlockProperties& properties = get_block_properties(type);
                for (int face = 0; face < 6; ++face) {
                    const Face& f = CUBE_FACES[face];
                    Vector3 corners[4] = {f.v1, f.v2, f.v3, f.v4};
                    float brightness[4];
                    for (int i = 0; i < 4; ++i) {
                        int ao = vertex_ao(x, y, z, f.normal, corners[i]);
                        float lightFraction = vertex_light(x, y, z, f.normal, corners[i]);
                        brightness[i] = AO_BRIGHTNESS[ao] * lightFraction;
                    }
                    draw_face(f, center, properties.textures[face], brightness);
                }
            }
        }
    }
}
