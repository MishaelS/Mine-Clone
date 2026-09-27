#include "core/BlockShape.hpp"

#include <algorithm>
#include <cmath>

namespace {
    BlockShapeBoxes full_cube_shape()
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        return result;
    }

    bool same_point(Vector3 a, Vector3 b)
    {
        constexpr float EPS = 0.0001f;
        return std::fabs(a.x - b.x) <= EPS && std::fabs(a.y - b.y) <= EPS && std::fabs(a.z - b.z) <= EPS;
    }

}

BlockInstanceState unpack_block_state(HorizontalDirection facing, uint16_t packed)
{
    BlockInstanceState state;
    state.facing = facing;
    state.open        = (packed & BlockStateBits::OPEN) != 0;
    state.top_half    = (packed & BlockStateBits::TOP_HALF) != 0;
    state.hinge_right = (packed & BlockStateBits::HINGE_RIGHT) != 0;
    state.bite_count  = static_cast<uint8_t>((packed & BlockStateBits::BITE_COUNT_MASK) >> BlockStateBits::BITE_COUNT_SHIFT);
    const uint16_t attachment = (packed & BlockStateBits::ATTACHMENT_MASK) >> BlockStateBits::ATTACHMENT_SHIFT;
    state.attachment = attachment == 0 ? BlockFace::Bottom : static_cast<BlockFace>(attachment - 1);
    return state;
}

uint16_t with_attachment(uint16_t packed, BlockFace attachment)
{
    const uint16_t stored = static_cast<uint16_t>(static_cast<uint16_t>(attachment) + 1);
    return static_cast<uint16_t>((packed & ~BlockStateBits::ATTACHMENT_MASK) |
                                 (stored << BlockStateBits::ATTACHMENT_SHIFT));
}

BlockFace attachment_from_hit_normal(Vector3 hit_normal)
{
    if (hit_normal.y > 0.5f) return BlockFace::Bottom;  // clicked a top face - stands on it
    if (hit_normal.y < -0.5f) return BlockFace::Top;    // clicked an underside - hangs from it
    if (hit_normal.x > 0.5f) return BlockFace::West;
    if (hit_normal.x < -0.5f) return BlockFace::East;
    if (hit_normal.z > 0.5f) return BlockFace::North;
    if (hit_normal.z < -0.5f) return BlockFace::South;
    return BlockFace::Bottom;
}

BlockShapeBoxes get_item_shape(BlockType type)
{
    BlockShapeBoxes shape = item_shape_of_kind(get_block_properties(type).shape_kind);
    // A cube drawn from its own parts is still a cube out of the world.
    if (shape.count == 0) shape = full_cube_shape();
    return shape;
}

ShapedFaceTexture shaped_face_texture(const BlockInstanceState& state,
                                      const BlockProperties& properties, BlockFace face, const BoundingBox& box)
{
    return shaped_kind_face_texture(properties.shape_kind, properties.pair_half, state, face, box,
                                    properties.texture_uvs[static_cast<int>(face)], properties.cut_texture_uv,
                                    properties.end_texture_uv);
}

bool block_has_custom_shape(BlockType type)
{
    return get_block_properties(type).has_custom_shape;
}

BlockShapeBoxes get_block_shape(BlockType type, const BlockInstanceState& state)
{
    return shape_of_kind(get_block_properties(type).shape_kind, state);
}

BlockShapeBoxes get_outline_shape(BlockType type, const BlockInstanceState& state)
{
    const BlockProperties& properties = get_block_properties(type);
    if (!properties.selectable) return BlockShapeBoxes{};

    // Its own hitbox in this state (a torch's, a plant's, a cactus's), from its file.
    const BlockStateModel& model = properties.state_models[static_cast<size_t>(shape_state_index(properties.shape_kind, state))];
    if (model.has_hitbox) {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = place_hitbox(model, state.attachment);
        return result;
    }
    if (properties.has_custom_shape) return get_block_shape(type, state);
    return full_cube_shape();
}

std::vector<OutlineEdge> outline_edges(const BlockShapeBoxes& shape)
{
    std::vector<OutlineEdge> edges;
    edges.reserve(static_cast<size_t>(shape.count) * 12);

    auto add_edge = [&edges](Vector3 from, Vector3 to) {
        for (const OutlineEdge& edge : edges) {
            if ((same_point(edge.from, from) && same_point(edge.to, to)) ||
                (same_point(edge.from, to) && same_point(edge.to, from))) {
                return;
            }
        }
        edges.push_back({from, to});
    };

    for (int i = 0; i < shape.count; ++i) {
        const BoundingBox& b = shape.boxes[i];
        Vector3 p[8] = {
            {b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z},
            {b.max.x, b.min.y, b.max.z}, {b.min.x, b.min.y, b.max.z},
            {b.min.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.min.z},
            {b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z},
        };
        add_edge(p[0], p[1]); add_edge(p[1], p[2]); add_edge(p[2], p[3]); add_edge(p[3], p[0]);
        add_edge(p[4], p[5]); add_edge(p[5], p[6]); add_edge(p[6], p[7]); add_edge(p[7], p[4]);
        add_edge(p[0], p[4]); add_edge(p[1], p[5]); add_edge(p[2], p[6]); add_edge(p[3], p[7]);
    }

    return edges;
}

