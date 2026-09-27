#include "core/BlockShape.hpp"

#include <algorithm>
#include <cmath>

namespace {
    // The (x, z) cell corner a door pivots around - the corner its closed
    // and open panels (door_shape()) share.
    Vector2 door_hinge_corner(const BlockInstanceState& state)
    {
        const bool right = state.hinge_right;
        switch (state.facing) {
            case HorizontalDirection::West:  return right ? Vector2{0.0f, 1.0f} : Vector2{0.0f, 0.0f};
            case HorizontalDirection::North: return right ? Vector2{0.0f, 0.0f} : Vector2{1.0f, 0.0f};
            case HorizontalDirection::East:  return right ? Vector2{1.0f, 0.0f} : Vector2{1.0f, 1.0f};
            case HorizontalDirection::South: return right ? Vector2{1.0f, 1.0f} : Vector2{0.0f, 1.0f};
        }
        return {0.0f, 0.0f};
    }

    BlockShapeBoxes full_cube_shape()
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        return result;
    }

    BlockShapeBoxes plant_outline_shape()
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = {{2.0f / 16.0f, 0.0f, 2.0f / 16.0f},
                           {14.0f / 16.0f, 13.0f / 16.0f, 14.0f / 16.0f}};
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

namespace {
    // Where a side face's texture-left column lands along that face's own
    // horizontal axis, following crop_tile_to_box()'s mapping (North: x=0,
    // South: x=1, East: z=0, West: z=1).
    float texture_left_along(BlockFace face)
    {
        return (face == BlockFace::North || face == BlockFace::East) ? 0.0f : 1.0f;
    }

    void mirror_u(Rectangle& uv)
    {
        uv.x += uv.width;
        uv.width = -uv.width;
    }

    BlockFace side_face_toward(HorizontalDirection direction)
    {
        return static_cast<BlockFace>(static_cast<int>(BlockFace::North) + static_cast<int>(direction));
    }

    // North<->South, East<->West - HorizontalDirection's own order pairs them.
    HorizontalDirection opposite(HorizontalDirection direction)
    {
        return static_cast<HorizontalDirection>(static_cast<int>(direction) ^ 1);
    }
}

ShapedFaceTexture shaped_face_texture(BlockType type, const BlockInstanceState& state,
                                      const BlockProperties& properties, BlockFace face, const BoundingBox& box)
{
    const bool side_face = face != BlockFace::Top && face != BlockFace::Bottom;
    const BlockFace facing_face = side_face_toward(state.facing);
    const BlockFace back_face = side_face_toward(opposite(state.facing));
    const bool bed = properties.shape_kind == BlockShapeKind::Bed;

    Rectangle tile = properties.texture_uvs[static_cast<int>(face)];
    // A partly eaten cake shows its cross-section on the bitten side.
    if (properties.cut_texture_uv && state.bite_count > 0 && face == facing_face) {
        tile = *properties.cut_texture_uv;
    }
    // A bed half's outer end: the head's headboard faces `facing` (the head
    // cell sits that way from the foot - see World::place_bed()), the
    // foot's end faces the other way.
    if (bed && properties.end_texture_uv && face == (type == BlockType::BedHead ? facing_face : back_face)) {
        tile = *properties.end_texture_uv;
    }

    ShapedFaceTexture result;
    if (properties.shape_kind == BlockShapeKind::Torch) return torch_face_texture(tile, face, box);

    result.uv = crop_tile_to_box(tile, face, box);

    if (properties.shape_kind == BlockShapeKind::Door && side_face) {
        // Only the panel's two broad faces carry the hinge/handle art - its
        // thin edges don't. The art's hinges run down its left column;
        // mirror the face if that column doesn't land on the hinge edge, so
        // hinges sit on the hinge side and the handle on the free side from
        // both sides of the door, open or closed.
        const bool long_axis_x = (box.max.x - box.min.x) > (box.max.z - box.min.z);
        const bool face_spans_x = face == BlockFace::North || face == BlockFace::South;
        if (long_axis_x == face_spans_x) {
            const Vector2 hinge = door_hinge_corner(state);
            const float hinge_along = long_axis_x ? hinge.x : hinge.y;
            if (hinge_along != texture_left_along(face)) mirror_u(result.uv);
        }
    } else if (bed) {
        // The two halves' faces toward each other are internal - skip them.
        if (face == (type == BlockType::BedHead ? back_face : facing_face)) {
            result.hidden = true;
            return result;
        }
        // Planks underside at the top of the frame (5px up), above the legs.
        if (face == BlockFace::Bottom) result.inset = 5.0f / 16.0f;
        if (face == BlockFace::Top) {
            // The head top's pillow is drawn toward its texture's right
            // (+u). Unrotated, +u runs toward +z (South); each quarter turn
            // of the face's corners (see ShapedFaceTexture) turns it
            // clockwise from above: East, North, West.
            switch (state.facing) {
                case HorizontalDirection::South: result.quarter_turns = 0; break;
                case HorizontalDirection::East:  result.quarter_turns = 1; break;
                case HorizontalDirection::North: result.quarter_turns = 2; break;
                case HorizontalDirection::West:  result.quarter_turns = 3; break;
            }
        } else if (side_face && face != facing_face && face != back_face) {
            // A long side. Foot side + head side read as one strip, foot
            // end on the left, headboard on the right - so each face's
            // texture-left column belongs on the foot side of its cell.
            const DirectionOffset toward_head = horizontal_direction_offset(state.facing);
            const float foot_along = (toward_head.dx + toward_head.dz) > 0 ? 0.0f : 1.0f;
            if (texture_left_along(face) != foot_along) mirror_u(result.uv);
        }
    }
    return result;
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

    if (type == BlockType::ShortGrass || type == BlockType::OakSapling) return plant_outline_shape();
    // Its own hitbox in this state (a torch's, a cactus's), from its file.
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

