#include "core/BlockShape.hpp"

#include <algorithm>
#include <cmath>

// The geometry of every BlockShapeKind, and the other pure shape helpers -
// nothing here reads the block registry, so the model editor draws blocks
// with exactly the game's shapes (it links this file alone).

namespace {
    // Real door/trapdoor panel thickness (3/16 of a block, same as vanilla).
    constexpr float PANEL_THICKNESS = 0.1875f;

    BlockShapeBoxes stairs_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 2;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 1.0f}}; // bottom slab, always present
        // Quarter step, on the footprint half `facing` points toward -
        // verify empirically against a placed stair from every approach
        // angle; flip the four cases below (a one-line change) if it reads
        // backwards, not a redesign.
        switch (state.facing) {
            case HorizontalDirection::South: result.boxes[1] = {{0.0f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::North: result.boxes[1] = {{0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.5f}}; break;
            case HorizontalDirection::East:  result.boxes[1] = {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::West:  result.boxes[1] = {{0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 1.0f}}; break;
        }
        return result;
    }

    BlockShapeBoxes slab_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = state.top_half
            ? BoundingBox{{0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
            : BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 1.0f}};
        return result;
    }

    BlockShapeBoxes trapdoor_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 1;
        if (!state.open) {
            result.boxes[0] = state.top_half
                ? BoundingBox{{0.0f, 1.0f - PANEL_THICKNESS, 0.0f}, {1.0f, 1.0f, 1.0f}}
                : BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, PANEL_THICKNESS, 1.0f}};
            return result;
        }
        // Swings up to vertical, flush against the wall on the hinge side -
        // opposite the stored facing (facing points from the hinge toward
        // the open room). Verify empirically, flip if backwards.
        switch (state.facing) {
            case HorizontalDirection::South: result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, PANEL_THICKNESS}}; break;
            case HorizontalDirection::North: result.boxes[0] = {{0.0f, 0.0f, 1.0f - PANEL_THICKNESS}, {1.0f, 1.0f, 1.0f}}; break;
            case HorizontalDirection::East:  result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {PANEL_THICKNESS, 1.0f, 1.0f}}; break;
            case HorizontalDirection::West:  result.boxes[0] = {{1.0f - PANEL_THICKNESS, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}; break;
        }
        return result;
    }

    // A door panel flush against one of the cell's four vertical edges.
    const BoundingBox PANEL_AT_MIN_X{{0.0f, 0.0f, 0.0f}, {PANEL_THICKNESS, 1.0f, 1.0f}};
    const BoundingBox PANEL_AT_MAX_X{{1.0f - PANEL_THICKNESS, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const BoundingBox PANEL_AT_MIN_Z{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, PANEL_THICKNESS}};
    const BoundingBox PANEL_AT_MAX_Z{{0.0f, 0.0f, 1.0f - PANEL_THICKNESS}, {1.0f, 1.0f, 1.0f}};

    // Vanilla's own door table (DoorBlock.getShape()). `facing` points toward
    // the player who placed it (direction_facing_player()), so a closed door
    // sits flush against the cell edge on that player's side; opening swings
    // it 90 degrees around the hinge corner, away from them into the cell.
    // Left hinge = the placing player's left. Every closed/open pair below
    // shares exactly that hinge corner - see door_hinge_corner().
    BlockShapeBoxes door_shape(const BlockInstanceState& state)
    {
        BlockShapeBoxes result;
        result.count = 1;
        const bool open = state.open;
        const bool right = state.hinge_right;
        switch (state.facing) {
            case HorizontalDirection::West:
                result.boxes[0] = !open ? PANEL_AT_MIN_X : (right ? PANEL_AT_MAX_Z : PANEL_AT_MIN_Z); break;
            case HorizontalDirection::North:
                result.boxes[0] = !open ? PANEL_AT_MIN_Z : (right ? PANEL_AT_MIN_X : PANEL_AT_MAX_X); break;
            case HorizontalDirection::East:
                result.boxes[0] = !open ? PANEL_AT_MAX_X : (right ? PANEL_AT_MIN_Z : PANEL_AT_MAX_Z); break;
            case HorizontalDirection::South:
                result.boxes[0] = !open ? PANEL_AT_MAX_Z : (right ? PANEL_AT_MAX_X : PANEL_AT_MIN_X); break;
        }
        return result;
    }

    // Vanilla's torch model (template_torch): two crossed pairs of full-size
    // planes at 7/16 and 9/16 - the torch art's 2px stick (columns 7-8)
    // lands on the same 2x2 footprint on all four, forming a solid-looking
    // column - plus a 2x2 cap on top and bottom at the stick's 10px height.
    // Only some faces of each box are drawn - see shaped_face_texture().
    constexpr float TORCH_NEAR   = 7.0f / 16.0f;
    constexpr float TORCH_FAR    = 9.0f / 16.0f;
    constexpr float TORCH_HEIGHT = 10.0f / 16.0f;

    // Always standing upright - on a wall the same model is moved and
    // tilted out of it by the torch's own "wall" state (BlockStateModel,
    // place_model_point()), as vanilla's wall_torch model does.
    BlockShapeBoxes torch_shape()
    {
        BlockShapeBoxes result;
        result.count = 3;
        result.boxes[0] = {{TORCH_NEAR, 0.0f, TORCH_NEAR}, {TORCH_FAR, TORCH_HEIGHT, TORCH_FAR}}; // caps
        result.boxes[1] = {{TORCH_NEAR, 0.0f, 0.0f}, {TORCH_FAR, 1.0f, 1.0f}};                    // West/East planes
        result.boxes[2] = {{0.0f, 0.0f, TORCH_NEAR}, {1.0f, 1.0f, TORCH_FAR}};                    // North/South planes
        return result;
    }

    // Turns a point of the north-wall layout (x, z about the cell's center)
    // to the wall `attachment` names: its +z, out of the wall, becomes the
    // way out of that one. Floor and ceiling: left as it is.
    Vector3 turn_to_wall(Vector3 p, BlockFace attachment)
    {
        const float dx = p.x - 0.5f, dz = p.z - 0.5f;
        switch (attachment) {
            case BlockFace::South: return {0.5f - dx, p.y, 0.5f - dz}; // out: -z
            case BlockFace::West:  return {0.5f + dz, p.y, 0.5f - dx}; // out: +x
            case BlockFace::East:  return {0.5f - dz, p.y, 0.5f + dx}; // out: -x
            default:               return p;
        }
    }

    // The model's tilt: `angle` degrees about X, its top leaning toward +z.
    Vector3 tilt(Vector3 v, float angle)
    {
        const float r = angle * DEG2RAD;
        const float c = std::cos(r), s = std::sin(r);
        return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    }
    BlockShapeBoxes bed_shape()
    {
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5625f, 1.0f}}; // low mattress, no facing dependency
        return result;
    }

    BlockShapeBoxes cake_shape(const BlockInstanceState& state)
    {
        constexpr float MARGIN = 1.0f / 16.0f;
        constexpr float SLICE_WIDTH = 2.0f / 16.0f; // real Minecraft's own per-bite width
        float eaten = state.bite_count * SLICE_WIDTH;

        BlockShapeBoxes result;
        result.count = 1;
        BoundingBox box{{MARGIN, 0.0f, MARGIN}, {1.0f - MARGIN, 0.5f, 1.0f - MARGIN}};
        // Shrinks from the edge `facing` points to - the side facing the
        // player who took the first bite (direction_facing_player() in
        // GameEngine), rather than vanilla's own fixed world direction.
        // That same face shows the "cut" cross-section texture - see
        // Chunk::build_mesh_data()'s Shaped branch.
        switch (state.facing) {
            case HorizontalDirection::South: box.max.z -= eaten; break;
            case HorizontalDirection::North: box.min.z += eaten; break;
            case HorizontalDirection::East:  box.max.x -= eaten; break;
            case HorizontalDirection::West:  box.min.x += eaten; break;
        }
        result.boxes[0] = box;
        return result;
    }

    bool rect_contains(float min_a, float max_a, float min_b, float max_b,
                       float inner_min_a, float inner_max_a, float inner_min_b, float inner_max_b)
    {
        constexpr float EPS = 0.0001f;
        return inner_min_a >= min_a - EPS && inner_max_a <= max_a + EPS &&
               inner_min_b >= min_b - EPS && inner_max_b <= max_b + EPS;
    }
}

BlockShapeBoxes shape_of_kind(BlockShapeKind kind, const BlockInstanceState& state)
{
    switch (kind) {
        case BlockShapeKind::Stairs:   return stairs_shape(state);
        case BlockShapeKind::Slab:     return slab_shape(state);
        case BlockShapeKind::Trapdoor: return trapdoor_shape(state);
        case BlockShapeKind::Door:     return door_shape(state);
        case BlockShapeKind::Bed:      return bed_shape();
        case BlockShapeKind::Cake:     return cake_shape(state);
        case BlockShapeKind::Torch:    return torch_shape();
        default:                       return BlockShapeBoxes{};
    }
}

int shape_state_count(BlockShapeKind kind)
{
    return kind == BlockShapeKind::Torch ? 2 : 1;
}

int shape_state_index(BlockShapeKind kind, const BlockInstanceState& state)
{
    if (kind == BlockShapeKind::Torch && state.attachment != BlockFace::Bottom && state.attachment != BlockFace::Top) return 1;
    return 0;
}

BlockInstanceState shape_state_example(BlockShapeKind kind, int index)
{
    BlockInstanceState state;
    state.facing = HorizontalDirection::North;
    if (kind == BlockShapeKind::Torch && index == 1) state.attachment = BlockFace::North;
    return state;
}

bool state_model_moves(const BlockStateModel& model)
{
    return model.angle != 0.0f || model.offset.x != 0.0f || model.offset.y != 0.0f || model.offset.z != 0.0f;
}

Vector3 place_model_point(const BlockStateModel& model, BlockFace attachment, Vector3 p)
{
    const Vector3 moved = {p.x + model.offset.x, p.y + model.offset.y, p.z + model.offset.z};
    const Vector3 turned = tilt({moved.x - model.pivot.x, moved.y - model.pivot.y, moved.z - model.pivot.z}, model.angle);
    return turn_to_wall({turned.x + model.pivot.x, turned.y + model.pivot.y, turned.z + model.pivot.z}, attachment);
}

Vector3 place_model_normal(const BlockStateModel& model, BlockFace attachment, Vector3 normal)
{
    const Vector3 turned = tilt(normal, model.angle);
    const Vector3 at = turn_to_wall({turned.x + 0.5f, turned.y, turned.z + 0.5f}, attachment);
    return {at.x - 0.5f, at.y, at.z - 0.5f};
}

BoundingBox place_hitbox(const BlockStateModel& model, BlockFace attachment)
{
    const Vector3 a = turn_to_wall(model.hitbox.min, attachment);
    const Vector3 b = turn_to_wall(model.hitbox.max, attachment);
    return {{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}};
}

BlockShapeBoxes item_shape_of_kind(BlockShapeKind kind)
{
    BlockInstanceState state;
    state.facing = HorizontalDirection::North;
    return shape_of_kind(kind, state);
}

ShapedFaceTexture torch_face_texture(Rectangle tile, BlockFace face, const BoundingBox& box)
{
    ShapedFaceTexture result;
    // Each box of torch_shape() contributes only its own faces: the cap
    // its top/bottom, each plane pair its two broad faces.
    result.flat_shade = true;
    const bool cap = box.max.y < 1.0f;
    const bool x_planes = !cap && box.max.z - box.min.z > 0.5f;
    const bool z_planes = !cap && box.max.x - box.min.x > 0.5f;
    auto pixels = [&](float column, float row) {
        return Rectangle{tile.x + tile.width * column / 16.0f, tile.y + tile.height * row / 16.0f,
                         tile.width * 2.0f / 16.0f, tile.height * 2.0f / 16.0f};
    };
    if (cap && face == BlockFace::Top) result.uv = pixels(7.0f, 6.0f);            // flame
    else if (cap && face == BlockFace::Bottom) result.uv = pixels(7.0f, 13.0f);   // stick's foot
    else if (x_planes && (face == BlockFace::West || face == BlockFace::East)) result.uv = tile;
    else if (z_planes && (face == BlockFace::North || face == BlockFace::South)) result.uv = tile;
    else result.hidden = true;
    return result;
}

Rectangle crop_tile_to_box(Rectangle tile, BlockFace face, const BoundingBox& box)
{
    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    switch (face) {
        case BlockFace::Top:    u0 = box.min.z;        u1 = box.max.z;        v0 = box.min.x;        v1 = box.max.x;        break;
        case BlockFace::Bottom: u0 = 1.0f - box.max.z; u1 = 1.0f - box.min.z; v0 = box.min.x;        v1 = box.max.x;        break;
        case BlockFace::North:  u0 = box.min.x;        u1 = box.max.x;        v0 = 1.0f - box.max.y; v1 = 1.0f - box.min.y; break;
        case BlockFace::South:  u0 = 1.0f - box.max.x; u1 = 1.0f - box.min.x; v0 = 1.0f - box.max.y; v1 = 1.0f - box.min.y; break;
        case BlockFace::East:   u0 = box.min.z;        u1 = box.max.z;        v0 = 1.0f - box.max.y; v1 = 1.0f - box.min.y; break;
        case BlockFace::West:   u0 = 1.0f - box.max.z; u1 = 1.0f - box.min.z; v0 = 1.0f - box.max.y; v1 = 1.0f - box.min.y; break;
    }
    return {
        tile.x + u0 * tile.width, tile.y + v0 * tile.height,
        (u1 - u0) * tile.width, (v1 - v0) * tile.height,
    };
}

bool shape_covers_face(const BoundingBox* boxes, int box_count, BlockFace face, float plane, const BoundingBox& rect)
{
    constexpr float EPS = 0.0001f;
    for (int i = 0; i < box_count; ++i) {
        const BoundingBox& b = boxes[i];
        switch (face) {
            case BlockFace::Top:
            case BlockFace::Bottom:
                if (std::fabs((face == BlockFace::Top ? b.min.y : b.max.y) - plane) <= EPS &&
                    rect_contains(b.min.x, b.max.x, b.min.z, b.max.z, rect.min.x, rect.max.x, rect.min.z, rect.max.z)) {
                    return true;
                }
                break;
            case BlockFace::North:
            case BlockFace::South:
                if (std::fabs((face == BlockFace::North ? b.max.z : b.min.z) - plane) <= EPS &&
                    rect_contains(b.min.x, b.max.x, b.min.y, b.max.y, rect.min.x, rect.max.x, rect.min.y, rect.max.y)) {
                    return true;
                }
                break;
            case BlockFace::East:
            case BlockFace::West:
                if (std::fabs((face == BlockFace::East ? b.min.x : b.max.x) - plane) <= EPS &&
                    rect_contains(b.min.z, b.max.z, b.min.y, b.max.y, rect.min.z, rect.max.z, rect.min.y, rect.max.y)) {
                    return true;
                }
                break;
        }
    }
    return false;
}
