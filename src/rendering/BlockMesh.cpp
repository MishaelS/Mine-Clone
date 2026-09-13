#include "rendering/BlockMesh.hpp"
#include "rendering/EntityLighting.hpp"

#include "rlgl.h"

namespace {
    constexpr float H = 0.5f; // unit cube, -0.5..0.5 on every axis

    struct Face {
        Vector3 vertices[4]; // CCW as seen from outside
        BlockFace texture_face;
    };

    // Indexed the same as BlockFace/FACE_DIRECTION_SHADE (Top, Bottom,
    // North, South, East, West) - same North/South = -Z/+Z, East/West =
    // +X/-X convention Chunk.cpp's own CUBE_FACES uses.
    const Face FACES[6] = {
        {{{-H, H, -H}, {-H, H, H}, { H, H, H}, { H, H,-H}}, BlockFace::Top},
        {{{-H,-H,  H}, {-H,-H,-H}, { H,-H,-H}, { H,-H, H}}, BlockFace::Bottom},
        {{{-H, H, -H}, { H, H,-H}, { H,-H,-H}, {-H,-H,-H}}, BlockFace::North},
        {{{ H, H,  H}, {-H, H, H}, {-H,-H, H}, { H,-H, H}}, BlockFace::South},
        {{{ H, H, -H}, { H, H, H}, { H,-H, H}, { H,-H,-H}}, BlockFace::East},
        {{{-H, H,  H}, {-H, H,-H}, {-H,-H,-H}, {-H,-H, H}}, BlockFace::West},
    };

    // Two diagonal planes, duplicated with reversed winding so the texture
    // remains visible from all four horizontal directions while ordinary
    // back-face culling stays enabled.
    const Face CROSS_FACES[4] = {
        {{{-H, H,-H}, { H, H, H}, { H,-H, H}, {-H,-H,-H}}, BlockFace::North},
        {{{ H, H, H}, {-H, H,-H}, {-H,-H,-H}, { H,-H, H}}, BlockFace::North},
        {{{ H, H,-H}, {-H, H, H}, {-H,-H, H}, { H,-H,-H}}, BlockFace::North},
        {{{-H, H, H}, { H, H,-H}, { H,-H,-H}, {-H,-H, H}}, BlockFace::North},
    };
}

void draw_block_cube(BlockType type, unsigned char alpha, std::optional<Color> tint_override,
                     Color environment_tint)
{
    const Texture2D& atlas = get_block_atlas_texture();
    const BlockProperties& properties = get_block_properties(type);

    rlSetTexture(atlas.id);
    rlBegin(RL_QUADS);
    const Face* faces = properties.render_shape == BlockRenderShape::Cross ? CROSS_FACES : FACES;
    int face_count = properties.render_shape == BlockRenderShape::Cross ? 4 : 6;
    for (int face_number = 0; face_number < face_count; ++face_number) {
        const Face& face = faces[face_number];
        int face_index = static_cast<int>(face.texture_face);
        Rectangle uv = get_sample_safe_block_uv(properties.texture_uvs[face_index]);
        Color tint = multiply_tint(
            tint_override.value_or(properties.texture_tints[face_index]), environment_tint);
        float shade = properties.render_shape == BlockRenderShape::Cross
            ? 0.9f : FACE_DIRECTION_SHADE[face_index];
        rlColor4ub(static_cast<unsigned char>(tint.r * shade),
                   static_cast<unsigned char>(tint.g * shade),
                   static_cast<unsigned char>(tint.b * shade),
                   static_cast<unsigned char>((tint.a * alpha) / 255));
        float u[] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
        float v[] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
        for (int i = 0; i < 4; ++i) {
            rlTexCoord2f(u[i], v[i]);
            rlVertex3f(face.vertices[i].x, face.vertices[i].y, face.vertices[i].z);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

void draw_textured_cube(const Texture2D& texture, Rectangle uv, Color tint)
{
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    float u[] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
    float v[] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
    for (const Face& face : FACES) {
        for (int i = 0; i < 4; ++i) {
            rlTexCoord2f(u[i], v[i]);
            rlVertex3f(face.vertices[i].x, face.vertices[i].y, face.vertices[i].z);
        }
    }
    rlEnd();
    rlSetTexture(0);
}
