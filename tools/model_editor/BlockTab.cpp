// The model editor's "Blocks" tab: every full-cube block the game loads
// from assets/blocks/<name>.json (content/BlockFile.hpp) - listed on the
// left, turned about in the middle, its properties, faces and textures on
// the right.
#include "ModelEditor.hpp"
#include "EditorStyle.hpp"
#include "EditorText.hpp"
#include "model/EntityModelRenderer.hpp"
#include "rendering/BlockIcon.hpp"
#include "core/BlockShape.hpp"

#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>

namespace {

    using editor_text::tr;
    using editor_text::tr_format;
    using namespace editor_style;

    constexpr float BLOCK_LIST_WIDTH  = 290.0f;
    constexpr float BLOCK_PANEL_WIDTH = 390.0f;
    constexpr float LIST_ROW          = 30.0f;

    // Each face's brightness in the preview, like the game's own face shading.
    constexpr float FACE_SHADE[6] = {1.0f, 0.5f, 0.8f, 0.8f, 0.6f, 0.6f};
    // The six faces' names.
    constexpr const char* FACE_KEYS[6] = {"editor.block_face.top",   "editor.block_face.bottom", "editor.block_face.north",
                                          "editor.block_face.south", "editor.block_face.east",   "editor.block_face.west"};
    // Each face's corners exactly as the game's chunk mesher lays a cube out
    // (Chunk.cpp's unit_cube_faces(), counter-clockwise from outside), a
    // unit cube about the origin; textured u = 0,1,1,0 and v = 0,0,1,1 over
    // them, as the game does - so a face's picture sits the same way here.
    constexpr Vector3 FACE_CORNERS[6][4] = {
        {{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}},     // top
        {{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}}, // bottom
        {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}, // north (-Z)
        {{0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}},     // south (+Z)
        {{0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}},     // east (+X)
        {{-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}}, // west (-X)
    };
    constexpr float CORNER_U[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    constexpr float CORNER_V[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr Vector3 FACE_NORMALS[6] = {{0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}};

    Color gui_color(int control, int property) {
        return GetColor(static_cast<unsigned int>(GuiGetStyle(control, property)));
    }

    // The game's own name for a block ("block.<name>"), or its id.
    std::string display_name(const std::string& name) {
        const std::string key = "block." + name;
        const std::string& text = tr(key);
        return text == key ? name : text;
    }

    std::string lower(std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // One 16px tile of terrain.png, in pixels.
    Rectangle tile_source(int x, int y) {
        return {x * 16.0f, y * 16.0f, 16.0f, 16.0f};
    }

    constexpr float AO_BRIGHTNESS[4] = {0.5f, 0.65f, 0.8f, 1.0f}; // Chunk.cpp's vertex AO -> brightness

    constexpr Color HITBOX_COLOR = {90, 220, 255, 255};
    constexpr Color PIVOT_COLOR  = {255, 80, 200, 255};

    // A few lines of small grey (or red, when something's wrong) text,
    // wrapped to `width`; returns the y under them.
    float draw_hint(float x, float y, float width, const std::string& text, bool error)
    {
        const Color color = error ? Color{230, 90, 80, 255} : Fade(gui_color(DEFAULT, TEXT_COLOR_NORMAL), 0.7f);
        std::string line, word;
        auto flush = [&] {
            DrawTextEx(editor_text::font(), line.c_str(), {x, y + 2}, 14, 1, color);
            y += 18;
            line.clear();
        };
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != ' ') {
                word += text[i];
                continue;
            }
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && MeasureTextEx(editor_text::font(), candidate.c_str(), 14, 1).x > width) {
                flush();
                line = word;
            } else {
                line = candidate;
            }
            word.clear();
        }
        if (!line.empty()) flush();
        return y + 2;
    }

    // Its shape - a fluid drawn here as the plain cube it is out of the
    // game's own fluid code.
    BlockShapeKind kind_of(const block_file::BlockFile& block)
    {
        const BlockShapeKind kind = static_cast<BlockShapeKind>(block.shape);
        return kind == BlockShapeKind::Fluid ? BlockShapeKind::Cube : kind;
    }

    // A tile a block of some shape has besides its six faces, listed as
    // more rows under them (face index 6 on): a cake's cut, a bed half's
    // end, a joining block's four halves of its wide art.
    struct TileSlot {
        const char* key;
        int* x;
        int* y;
        bool* has;   // set once a tile is picked - nullptr: always there
        int outline; // the face the preview frames for it
    };
    std::vector<TileSlot> extra_slots(block_file::BlockFile& block)
    {
        std::vector<TileSlot> slots;
        const BlockShapeKind kind = kind_of(block);
        if (kind == BlockShapeKind::Cake) slots.push_back({"editor.block_face.cut", &block.cut_x, &block.cut_y, &block.has_cut, 2});
        if (kind == BlockShapeKind::Bed) {
            // Shown facing north: the head's end is its north face, the foot's its south.
            slots.push_back({"editor.block_face.end", &block.end_x, &block.end_y, &block.has_end, block.half == 1 ? 2 : 3});
        }
        if (kind == BlockShapeKind::Cube && block.directional && block.joins) {
            constexpr const char* KEYS[4] = {"editor.block_face.joined_front_right", "editor.block_face.joined_front_left",
                                             "editor.block_face.joined_back_right", "editor.block_face.joined_back_left"};
            for (size_t i = 0; i < 4; ++i) slots.push_back({KEYS[i], &block.joined[i][0], &block.joined[i][1], nullptr, i < 2 ? 3 : 2});
        }
        return slots;
    }
    std::vector<TileSlot> extra_slots(const block_file::BlockFile& block)
    {
        return extra_slots(const_cast<block_file::BlockFile&>(block));
    }

    // Face or extra slot `index`'s name.
    std::string slot_name(const block_file::BlockFile& block, int index)
    {
        if (index < 6) return tr(FACE_KEYS[index]);
        const std::vector<TileSlot> slots = extra_slots(block);
        return index - 6 < static_cast<int>(slots.size()) ? tr(slots[static_cast<size_t>(index - 6)].key) : std::string();
    }

    // How it's shown here: facing north - also how one placed while looking
    // north stands - in its state `index` (a torch: 0 on the floor, 1 on the
    // north wall).
    BlockInstanceState display_state(const block_file::BlockFile& block, int index)
    {
        const int count = shape_state_count(kind_of(block));
        return shape_state_example(kind_of(block), std::clamp(index, 0, count - 1));
    }

    // Its model placement in `state` (moved/tilted, its own hitbox).
    BlockStateModel state_model_of(const block_file::BlockFile& block, const BlockInstanceState& state)
    {
        return block_file::to_state_model(block.states[static_cast<size_t>(shape_state_index(kind_of(block), state))]);
    }

    // Its boxes in `state`: one full cube, or its shape's (core/BlockShape).
    BlockShapeBoxes block_boxes(const block_file::BlockFile& block, const BlockInstanceState& state)
    {
        if (kind_of(block) != BlockShapeKind::Cube) return shape_of_kind(kind_of(block), state);
        BlockShapeBoxes cube;
        cube.count = 1;
        cube.boxes[0] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        return cube;
    }

    // What the crosshair aims at in `state` (the game's get_outline_shape()):
    // nothing if it can't be aimed at, else its own hitbox, else its shape,
    // else the whole cell.
    BlockShapeBoxes hitbox_boxes(const block_file::BlockFile& block, const BlockInstanceState& state)
    {
        if (!block.selectable) return BlockShapeBoxes{}; // nothing to aim at (water)
        const BlockStateModel model = state_model_of(block, state);
        if (!model.has_hitbox) return block_boxes(block, state);
        BlockShapeBoxes result;
        result.count = 1;
        result.boxes[0] = place_hitbox(model, state.attachment);
        return result;
    }

    // A box's twelve edges, a hair outside it.
    void draw_box_edges(const BoundingBox& box, Vector3 origin, Color color)
    {
        constexpr float GROW = 0.003f;
        DrawBoundingBox({Vector3Add(origin, Vector3Subtract(box.min, {GROW, GROW, GROW})),
                         Vector3Add(origin, Vector3Add(box.max, {GROW, GROW, GROW}))},
                        color);
    }

    // A few blocks in a little world - what "In the game" shows.
    struct Scene {
        struct Cell {
            int x, y, z;
            const block_file::BlockFile* block;
            BlockInstanceState state{};
            int joined = 0; // see emit_block()
        };
        std::vector<Cell> cells;

        const block_file::BlockFile* at(int x, int y, int z) const
        {
            for (const Cell& cell : cells) {
                if (cell.x == x && cell.y == y && cell.z == z) return cell.block;
            }
            return nullptr;
        }
        bool solid(int x, int y, int z) const
        {
            const block_file::BlockFile* block = at(x, y, z);
            return block && block->solid;
        }
    };

    // Chunk.cpp's vertex_ao(): 0 (darkest) .. 3 from the two cells along
    // the corner's edges and the diagonal one, just outside the face.
    int vertex_ao(const Scene& scene, int x, int y, int z, Vector3 normal, Vector3 corner)
    {
        const int n[3] = {static_cast<int>(normal.x), static_cast<int>(normal.y), static_cast<int>(normal.z)};
        const int c[3] = {corner.x > 0.0f ? 1 : -1, corner.y > 0.0f ? 1 : -1, corner.z > 0.0f ? 1 : -1};
        int axis1 = -1, axis2 = -1;
        for (int axis = 0; axis < 3; ++axis) {
            if (n[axis] == 0) (axis1 == -1 ? axis1 : axis2) = axis;
        }
        const int base[3] = {x + n[0], y + n[1], z + n[2]};
        int side1[3] = {base[0], base[1], base[2]};
        side1[axis1] += c[axis1];
        int side2[3] = {base[0], base[1], base[2]};
        side2[axis2] += c[axis2];
        int diagonal[3] = {side1[0], side1[1], side1[2]};
        diagonal[axis2] += c[axis2];
        const bool s1 = scene.solid(side1[0], side1[1], side1[2]);
        const bool s2 = scene.solid(side2[0], side2[1], side2[2]);
        const bool cc = scene.solid(diagonal[0], diagonal[1], diagonal[2]);
        if (s1 && s2) return 0;
        return 3 - (static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cc));
    }

    // Where a box's face lies along its own axis, and whether that's the
    // cell's edge (where a neighbor can hide it).
    float face_plane(const BoundingBox& box, int face)
    {
        switch (face) {
            case 0: return box.max.y;
            case 1: return box.min.y;
            case 2: return box.min.z;
            case 3: return box.max.z;
            case 4: return box.max.x;
            default: return box.min.x;
        }
    }
    bool on_cell_edge(const BoundingBox& box, int face)
    {
        const float plane = face_plane(box, face);
        return (face == 0 || face == 3 || face == 4) ? plane >= 0.999f : plane <= 0.001f;
    }

    // A box's corner for FACE_CORNERS[face][corner] (the same corner order).
    Vector3 box_corner(const BoundingBox& box, int face, int corner)
    {
        const Vector3 sign = FACE_CORNERS[face][corner];
        return {sign.x < 0.0f ? box.min.x : box.max.x, sign.y < 0.0f ? box.min.y : box.max.y, sign.z < 0.0f ? box.min.z : box.max.z};
    }

    // A point of its model where it's drawn in `state` (0..1 cell space).
    Vector3 placed_point(const block_file::BlockFile& block, const BlockInstanceState& state, Vector3 p)
    {
        const BlockStateModel model = state_model_of(block, state);
        if (kind_of(block) == BlockShapeKind::Cube || !state_model_moves(model)) return p;
        return place_model_point(model, state.attachment, p);
    }

    // Whether it's drawn from its own parts (BlockFile::elements).
    bool has_elements(const block_file::BlockFile& block)
    {
        return !block.elements.empty() && block_file::elements_allowed(block.shape);
    }

    // A part's box in cell units (0..1), whichever way round its corners are.
    BoundingBox element_box(const block_file::Element& element)
    {
        return {{std::min(element.from.x, element.to.x) / 16.0f, std::min(element.from.y, element.to.y) / 16.0f,
                 std::min(element.from.z, element.to.z) / 16.0f},
                {std::max(element.from.x, element.to.x) / 16.0f, std::max(element.from.y, element.to.y) / 16.0f,
                 std::max(element.from.z, element.to.z) / 16.0f}};
    }

    // Whether emit_block() draws face `f` of `box` at all.
    bool face_drawn(const block_file::BlockFile& block, const BlockShapeBoxes& boxes, const BoundingBox& box, int f)
    {
        if (shape_covers_face(boxes.boxes.data(), boxes.count, static_cast<BlockFace>(f), face_plane(box, f), box)) return false;
        if (kind_of(block) == BlockShapeKind::Torch && torch_face_texture({0, 0, 1, 1}, static_cast<BlockFace>(f), box).hidden) return false;
        return true;
    }

    // The face of it nearest along `ray` (the block centered on the origin):
    // which box or part, which face.
    struct PickedFace {
        int part;
        int face;
    };
    std::optional<PickedFace> face_under_ray(const block_file::BlockFile& block, const BlockInstanceState& state, Vector3 origin, Ray ray)
    {
        const bool parts = has_elements(block);
        const BlockShapeBoxes boxes = block_boxes(block, state);
        const int count = parts ? static_cast<int>(block.elements.size()) : boxes.count;
        std::optional<PickedFace> face;
        float nearest = 1e9f;
        for (int b = 0; b < count; ++b) {
            const BoundingBox box = parts ? element_box(block.elements[static_cast<size_t>(b)]) : boxes.boxes[static_cast<size_t>(b)];
            for (int f = 0; f < 6; ++f) {
                if (parts ? !block.elements[static_cast<size_t>(b)].faces[static_cast<size_t>(f)].enabled
                          : !face_drawn(block, boxes, box, f)) {
                    continue;
                }
                Vector3 corners[4];
                for (int c = 0; c < 4; ++c) {
                    corners[c] = Vector3Add(origin, placed_point(block, state, box_corner(box, f, c)));
                }
                const RayCollision hit = GetRayCollisionQuad(ray, corners[0], corners[1], corners[2], corners[3]);
                if (hit.hit && hit.distance < nearest) {
                    nearest = hit.distance;
                    face = PickedFace{b, f};
                }
            }
        }
        return face;
    }

    // Draws `block` in `state` with its cell's min corner at `origin`,
    // exactly as the game meshes it: each box's faces, cropped to the box
    // (a torch's special ones), leaving out any its own boxes cover. With a
    // `scene` it stands in (at cell cx, cy, cz) it is also culled against
    // its neighbors and lit per corner (AO) the way daylight lights blocks;
    // alone, just direction-shaded.
    // A plant's two crossed planes (Chunk.cpp's CROSS_FACES - each plane
    // twice, once per side), its north tile evenly lit.
    constexpr Vector3 CROSS_CORNERS[4][4] = {
        {{0, 1, 0}, {1, 1, 1}, {1, 0, 1}, {0, 0, 0}},
        {{1, 1, 1}, {0, 1, 0}, {0, 0, 0}, {1, 0, 1}},
        {{1, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 0}},
        {{0, 1, 1}, {1, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    };
    void emit_cross(const block_file::BlockFile& block, Vector3 origin)
    {
        const block_file::Face& face = block.faces[2];
        const Rectangle uv = {face.tile_x / 16.0f, face.tile_y / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f};
        for (const auto& quad : CROSS_CORNERS) {
            for (int c = 0; c < 4; ++c) {
                const Vector3 p = Vector3Add(origin, quad[c]);
                rlColor4ub(face.tint.r, face.tint.g, face.tint.b, face.tint.a);
                rlTexCoord2f(uv.x + CORNER_U[c] * uv.width, uv.y + CORNER_V[c] * uv.height);
                rlVertex3f(p.x, p.y, p.z);
            }
        }
    }

    // emit_block() for a block drawn from its own parts: each enabled face
    // with its own piece of the side's tile, from outside, inside or both
    // (its normal setting) - as the game's mesher does.
    void emit_elements(const block_file::BlockFile& block, const BlockInstanceState& state, Vector3 origin,
                       const Scene* scene, int cx, int cy, int cz)
    {
        const BlockStateModel model = state_model_of(block, state);
        const bool placed = state_model_moves(model);
        for (const block_file::Element& element : block.elements) {
            const BoundingBox box = element_box(element);
            for (int f = 0; f < 6; ++f) {
                const block_file::ElementFace& element_face = element.faces[static_cast<size_t>(f)];
                if (!element_face.enabled) continue;
                const Vector3 normal = FACE_NORMALS[f];
                if (scene && element_face.normal == 0 && on_cell_edge(box, f)) {
                    const block_file::BlockFile* neighbor = scene->at(cx + static_cast<int>(normal.x), cy + static_cast<int>(normal.y),
                                                                      cz + static_cast<int>(normal.z));
                    if (neighbor && !neighbor->transparent) continue;
                }
                const block_file::Face& face = block.faces[static_cast<size_t>(f)];
                const std::array<float, 4> pixels = block_file::face_uv(element, f);
                const Rectangle uv = {(face.tile_x * 16.0f + pixels[0]) / 256.0f, (face.tile_y * 16.0f + pixels[1]) / 256.0f,
                                      (pixels[2] - pixels[0]) / 256.0f, (pixels[3] - pixels[1]) / 256.0f};
                for (int side = 0; side < 2; ++side) {
                    const bool inward = side == 1;
                    if (inward ? element_face.normal == 0 : element_face.normal == 1) continue;
                    float light[4];
                    for (int c = 0; c < 4; ++c) {
                        const float ao = scene && element.shade && !inward
                                             ? AO_BRIGHTNESS[vertex_ao(*scene, cx, cy, cz, normal, FACE_CORNERS[f][c])]
                                             : 1.0f;
                        light[c] = (element.shade ? FACE_SHADE[inward ? (f ^ 1) : f] : 1.0f) * ao;
                    }
                    // Turned round: the other winding, each corner keeping its texel.
                    constexpr int OUTWARD[4] = {0, 1, 2, 3};
                    constexpr int INWARD[4] = {0, 3, 2, 1};
                    for (int k = 0; k < 4; ++k) {
                        const int c = inward ? INWARD[k] : OUTWARD[k];
                        Vector3 p = box_corner(box, f, c);
                        if (placed) p = place_model_point(model, state.attachment, p);
                        p = Vector3Add(origin, p);
                        rlColor4ub(static_cast<unsigned char>(face.tint.r * light[c]), static_cast<unsigned char>(face.tint.g * light[c]),
                                   static_cast<unsigned char>(face.tint.b * light[c]), face.tint.a);
                        rlTexCoord2f(uv.x + CORNER_U[c] * uv.width, uv.y + CORNER_V[c] * uv.height);
                        rlVertex3f(p.x, p.y, p.z);
                    }
                }
            }
        }
    }

    // One tile of terrain.png in 0..1 atlas units.
    Rectangle atlas_tile(int x, int y)
    {
        return {x / 16.0f, y / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f};
    }

    // `joined`: a block that joins sideways (a chest) drawn as a half of its
    // wide block - 1 the half with its partner on the right of its facing
    // (ChestPart::Primary), 2 the other; 0 alone.
    void emit_block(const block_file::BlockFile& block, const BlockInstanceState& state, Vector3 origin,
                    const Scene* scene, int cx, int cy, int cz, int joined = 0)
    {
        if (has_elements(block)) {
            emit_elements(block, state, origin, scene, cx, cy, cz);
            return;
        }
        if (kind_of(block) == BlockShapeKind::Cross) {
            emit_cross(block, origin);
            return;
        }
        const BlockShapeKind kind = kind_of(block);
        const BlockShapeBoxes boxes = block_boxes(block, state);
        // Moved and tilted in this state (a torch on a wall), as the game does.
        const BlockStateModel model = state_model_of(block, state);
        const bool placed = kind != BlockShapeKind::Cube && state_model_moves(model);
        for (int b = 0; b < boxes.count; ++b) {
            const BoundingBox& box = boxes.boxes[static_cast<size_t>(b)];
            for (int f = 0; f < 6; ++f) {
                if (shape_covers_face(boxes.boxes.data(), boxes.count, static_cast<BlockFace>(f), face_plane(box, f), box)) continue;
                const Vector3 normal = FACE_NORMALS[f];
                // Hidden by its neighbor, as the chunk mesher decides.
                if (scene) {
                    const block_file::BlockFile* neighbor = scene->at(cx + static_cast<int>(normal.x), cy + static_cast<int>(normal.y),
                                                                      cz + static_cast<int>(normal.z));
                    if (kind == BlockShapeKind::Cube) {
                        const bool inset_side = block.side_inset > 0 && f >= 2;
                        if (!inset_side && neighbor) {
                            if (!neighbor->transparent) continue;
                            if (block.transparent && neighbor == &block && !block.keep_same_faces) continue;
                        }
                    } else if (on_cell_edge(box, f) && neighbor && !neighbor->transparent) {
                        continue;
                    }
                }

                // Which tile: a directional cube's front is its south face,
                // every other side its east one - a joined half's front and
                // back its piece of the wide art; a shaped block's face
                // cropped, turned and mirrored as the game does
                // (shaped_kind_face_texture(): a bitten cake's cut, a bed's
                // end, a door's hinge side).
                int texture_face = f;
                if (block.directional && kind == BlockShapeKind::Cube && f >= 2) texture_face = f == 3 ? 3 : 4;
                const block_file::Face& face = block.faces[texture_face];
                Rectangle tile = atlas_tile(face.tile_x, face.tile_y);
                if (kind == BlockShapeKind::Cube && block.joins && joined != 0 && (f == 2 || f == 3)) {
                    const std::array<int, 2>& wide = block.joined[static_cast<size_t>((f == 3 ? 0 : 2) + joined - 1)];
                    tile = atlas_tile(wide[0], wide[1]);
                }
                Rectangle uv = tile;
                bool flat = false;
                int turns = 0;
                float inset = 0.0f;
                if (kind != BlockShapeKind::Cube) {
                    const ShapedFaceTexture shaped = shaped_kind_face_texture(
                        kind, block.half, state, static_cast<BlockFace>(f), box, tile,
                        block.has_cut ? std::optional<Rectangle>(atlas_tile(block.cut_x, block.cut_y)) : std::nullopt,
                        block.has_end ? std::optional<Rectangle>(atlas_tile(block.end_x, block.end_y)) : std::nullopt);
                    if (shaped.hidden) continue;
                    uv = shaped.uv;
                    flat = shaped.flat_shade;
                    turns = shaped.quarter_turns;
                    inset = shaped.inset;
                }

                // Slot k sits at corner (k + turns) - a turned face (a bed's
                // top) keeps its texture corners and moves its positions.
                float light[4];
                for (int c = 0; c < 4; ++c) {
                    const int corner = (c + turns) % 4;
                    const float ao = scene && !flat ? AO_BRIGHTNESS[vertex_ao(*scene, cx, cy, cz, normal, FACE_CORNERS[f][corner])] : 1.0f;
                    light[c] = (flat ? 1.0f : FACE_SHADE[f]) * ao;
                }
                // Split along the brighter diagonal, as the game does.
                const int first = light[1] + light[3] > light[0] + light[2] ? 1 : 0;
                for (int k = 0; k < 4; ++k) {
                    const int c = (first + k) % 4;
                    Vector3 p = box_corner(box, f, (c + turns) % 4);
                    // A cactus-like cube's sides drawn inward; a bed's underside up at its frame.
                    if (kind == BlockShapeKind::Cube && f >= 2 && block.side_inset > 0) {
                        p = Vector3Subtract(p, Vector3Scale(normal, block.side_inset / 16.0f));
                    }
                    if (inset != 0.0f) p = Vector3Subtract(p, Vector3Scale(normal, inset));
                    if (placed) p = place_model_point(model, state.attachment, p);
                    p = Vector3Add(origin, p);
                    rlColor4ub(static_cast<unsigned char>(face.tint.r * light[c]), static_cast<unsigned char>(face.tint.g * light[c]),
                               static_cast<unsigned char>(face.tint.b * light[c]), face.tint.a);
                    rlTexCoord2f(uv.x + CORNER_U[c] * uv.width, uv.y + CORNER_V[c] * uv.height);
                    rlVertex3f(p.x, p.y, p.z);
                }
            }
        }
    }
}

namespace {
    // What's drawn beside a block to make it whole: a two-cell block's
    // other half (its file's "pair"), or - the "large" view - a second one
    // joined to it (a large chest). `offset` is its cell from the block's.
    struct Companion {
        const block_file::BlockFile* block;
        Vector3 offset;
        int joined_self;  // emit_block()'s `joined` for the block itself...
        int joined_other; // ...and for the companion
    };
    std::optional<Companion> companion_of(const std::vector<block_file::BlockFile>& blocks, const block_file::BlockFile& block,
                                          const BlockInstanceState& state, bool large_view)
    {
        const BlockShapeKind kind = kind_of(block);
        if (is_pair_kind(kind)) {
            for (const block_file::BlockFile& other : blocks) {
                if (other.name != block.partner || &other == &block) continue;
                const FaceOffset step = pair_partner_offset(kind, block.half, state.facing);
                return Companion{&other, {static_cast<float>(step.dx), static_cast<float>(step.dy), static_cast<float>(step.dz)}, 0, 0};
            }
            return std::nullopt;
        }
        // Joined along +x: seen from the front (south), the right one has
        // its partner on the right of its facing - the primary half.
        if (kind == BlockShapeKind::Cube && block.directional && block.joins && large_view) return Companion{&block, {1, 0, 0}, 2, 1};
        return std::nullopt;
    }

    // How much farther the preview's camera stands back: a pair is twice as big.
    float preview_zoom(const std::optional<Companion>& companion)
    {
        return companion ? 1.6f : 1.0f;
    }

    // Where the preview puts a block's cell so it and its companion are
    // centered on the orbit point.
    Vector3 preview_origin(const std::optional<Companion>& companion)
    {
        const Vector3 center = {-0.5f, -0.5f, -0.5f};
        return companion ? Vector3Subtract(center, Vector3Scale(companion->offset, 0.5f)) : center;
    }
}

namespace {
    // Here a block gets an animate tick this often a game tick - about as
    // often as one a few steps from the player does in the game.
    constexpr float EDITOR_ANIMATE_CHANCE = 0.15f;

    // The state its particles come off in: as shown, a directional block's
    // front toward the south (where the preview draws it).
    BlockInstanceState particle_state(const block_file::BlockFile& block, const BlockInstanceState& state)
    {
        BlockInstanceState result = state;
        if (block.directional && kind_of(block) == BlockShapeKind::Cube) result.facing = HorizontalDirection::South;
        return result;
    }

    void draw_particles(const std::vector<block_particles::Particle>& particles, const Camera3D& camera, const Texture2D& sheet,
                        const Texture2D& atlas)
    {
        for (const block_particles::Particle& particle : particles) {
            const float size = block_particles::draw_size(particle);
            DrawBillboardRec(camera, block_particles::from_sheet(particle) ? sheet : atlas, block_particles::source(particle),
                             particle.position, Vector2{size, size}, block_particles::draw_color(particle));
        }
    }
}

// ---------------------------------------------------------------- Setup --

const Texture2D& ModelEditor::terrain_atlas()
{
    if (preview_blocks_atlas.id == 0) {
        preview_blocks_atlas = LoadTexture(ASSETS_PATH "sprites/terrain.png");
        SetTextureFilter(preview_blocks_atlas, TEXTURE_FILTER_POINT);
    }
    return preview_blocks_atlas;
}

const Texture2D& ModelEditor::items_atlas()
{
    if (preview_items_atlas.id == 0) {
        preview_items_atlas = LoadTexture(ASSETS_PATH "sprites/items.png");
        SetTextureFilter(preview_items_atlas, TEXTURE_FILTER_POINT);
    }
    return preview_items_atlas;
}

const Texture2D& ModelEditor::particle_sheet()
{
    if (preview_particle_sheet.id == 0) {
        preview_particle_sheet = LoadTexture((std::string(ASSETS_PATH) + block_particles::SHEET_PATH).c_str());
        SetTextureFilter(preview_particle_sheet, TEXTURE_FILTER_POINT);
    }
    return preview_particle_sheet;
}

void ModelEditor::load_blocks()
{
    blocks = block_file::load_all();
    block_dirty.assign(blocks.size(), false);
    blocks_loaded = true;
    block_undo_stack.clear();
    block_redo_stack.clear();
    select_block(blocks.empty() ? -1 : 0);
}

void ModelEditor::select_block(int index)
{
    commit_block_history(true);
    selected_block = index;
    editing_widget = -1;
    if (index >= 0) {
        block_committed = blocks[static_cast<size_t>(index)];
        block_committed_index = index;
    } else {
        block_committed_index = -1;
    }
}

void ModelEditor::save_block(int index)
{
    if (index < 0 || index >= static_cast<int>(blocks.size())) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(index)];
    const std::string path = block_file::directory() + block.name + ".json";
    if (block_file::save(block, path)) {
        block_dirty[static_cast<size_t>(index)] = false;
        set_status(tr_format("editor.block_saved", {display_name(block.name)}));
    } else {
        set_status(tr_format("editor.block_save_failed", {path}));
    }
}

void ModelEditor::save_all_blocks()
{
    int saved = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (!block_dirty[i]) continue;
        if (block_file::save(blocks[i], block_file::directory() + blocks[i].name + ".json")) {
            block_dirty[i] = false;
            ++saved;
        }
    }
    set_status(tr_format("editor.blocks_saved_all", {std::to_string(saved)}));
}

// -------------------------------------------------------------- History --

void ModelEditor::mark_block_dirty()
{
    if (selected_block < 0) return;
    block_dirty[static_cast<size_t>(selected_block)] = true;
    block_uncommitted = true;
}

void ModelEditor::commit_block_history(bool force)
{
    if (!block_uncommitted || block_committed_index < 0) return;
    const bool mouse_busy = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (!force && (mouse_busy || typing())) return; // still in the middle of this change
    block_undo_stack.push_back({block_committed_index, block_committed});
    if (block_undo_stack.size() > 200) block_undo_stack.erase(block_undo_stack.begin());
    block_redo_stack.clear();
    if (selected_block >= 0) block_committed = blocks[static_cast<size_t>(selected_block)];
    block_committed_index = selected_block;
    block_uncommitted = false;
}

void ModelEditor::block_undo()
{
    commit_block_history(true);
    if (block_undo_stack.empty()) {
        set_status(tr("editor.nothing_to_undo"));
        return;
    }
    const auto [index, state] = block_undo_stack.back();
    block_undo_stack.pop_back();
    block_redo_stack.push_back({index, blocks[static_cast<size_t>(index)]});
    blocks[static_cast<size_t>(index)] = state;
    block_dirty[static_cast<size_t>(index)] = true;
    selected_block = index;
    block_committed = state;
    block_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.undone", {std::to_string(block_undo_stack.size())}));
}

void ModelEditor::block_redo()
{
    commit_block_history(true);
    if (block_redo_stack.empty()) {
        set_status(tr("editor.nothing_to_redo"));
        return;
    }
    const auto [index, state] = block_redo_stack.back();
    block_redo_stack.pop_back();
    block_undo_stack.push_back({index, blocks[static_cast<size_t>(index)]});
    blocks[static_cast<size_t>(index)] = state;
    block_dirty[static_cast<size_t>(index)] = true;
    selected_block = index;
    block_committed = state;
    block_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.redone", {std::to_string(block_redo_stack.size())}));
}

// ------------------------------------------------------------ Particles --

void ModelEditor::update_block_particles(float dt)
{
    if (particles_block != selected_block) {
        preview_particles.clear();
        game_particles.clear();
        particles_block = selected_block;
    }
    auto random = [this](float minimum, float maximum) {
        particle_random ^= particle_random << 13;
        particle_random ^= particle_random >> 17;
        particle_random ^= particle_random << 5;
        return minimum + (maximum - minimum) * static_cast<float>(particle_random & 0x00ffffffu) / static_cast<float>(0x01000000u);
    };

    // Move them; "in the game" a falling leaf lands on the grass (its top at y -0.5).
    for (block_particles::Particle& particle : preview_particles) block_particles::step(particle, dt);
    for (block_particles::Particle& particle : game_particles) {
        const Vector3 before = particle.position;
        block_particles::step(particle, dt);
        if (block_particles::falls(particle) && particle.position.y < -0.49f) {
            particle.position = {before.x, -0.49f, before.z};
            particle.velocity = {0, 0, 0};
        }
    }
    auto dead = [](const block_particles::Particle& particle) { return particle.age >= particle.lifetime; };
    preview_particles.erase(std::remove_if(preview_particles.begin(), preview_particles.end(), dead), preview_particles.end());
    game_particles.erase(std::remove_if(game_particles.begin(), game_particles.end(), dead), game_particles.end());
    if (selected_block < 0) return;

    // New ones, on the game's 20/s clock.
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
    const BlockInstanceState state = display_state(block, block_state_view);
    const BlockInstanceState emit_state = particle_state(block, state);
    const BlockStateModel model = state_model_of(block, state);
    const std::vector<BlockParticleEmitter> emitters = block_file::to_emitters(block.particles);
    const block_file::Face& side = block.faces[2];
    const Rectangle tile = {side.tile_x * 16.0f, side.tile_y * 16.0f, 16.0f, 16.0f};
    auto emit = [&](std::vector<block_particles::Particle>& into, Vector3 cell, bool air_below) {
        if (random(0.0f, 1.0f) >= EDITOR_ANIMATE_CHANCE) return;
        for (const BlockParticleEmitter& emitter : emitters) {
            if (emitter.only_above_air && !air_below) continue;
            if (random(0.0f, 1.0f) >= emitter.chance) continue;
            const Color color = emitter.kind == BlockParticleKind::Leaf ? side.tint : emitter.color;
            for (int n = 0; n < emitter.count; ++n) {
                const Vector3 unit = {random(-1.0f, 1.0f), random(-1.0f, 1.0f), random(-1.0f, 1.0f)};
                const Vector3 at = Vector3Add(cell, block_particles::emit_point(emitter, unit, model, emit_state, block.directional));
                into.push_back(block_particles::make(emitter.kind, at, color, tile, random));
            }
        }
    };
    particle_clock = std::min(particle_clock + dt, 0.25f);
    while (particle_clock >= 0.05f) {
        particle_clock -= 0.05f;
        if (emitters.empty()) continue;
        emit(preview_particles, preview_origin(companion_of(blocks, block, state, block_large_view)), true);
        // The two copies "in the game" stand on grass.
        emit(game_particles, {-0.5f, -0.5f, -0.5f}, false);
        emit(game_particles, {0.5f, -0.5f, -0.5f}, false);
    }
    constexpr size_t MAX_PARTICLES = 512;
    if (preview_particles.size() > MAX_PARTICLES) preview_particles.erase(preview_particles.begin(), preview_particles.end() - MAX_PARTICLES);
    if (game_particles.size() > MAX_PARTICLES) game_particles.erase(game_particles.begin(), game_particles.end() - MAX_PARTICLES);
}

// ---------------------------------------------------------------- Frame --

void ModelEditor::run_blocks_frame()
{
    if (!blocks_loaded) load_blocks();
    SetMouseCursor(MOUSE_CURSOR_DEFAULT); // a number field under the mouse sets its own

    const float width = static_cast<float>(GetScreenWidth()), height = static_cast<float>(GetScreenHeight());
    const Rectangle list = {0, HEADER_HEIGHT, BLOCK_LIST_WIDTH, height - HEADER_HEIGHT};
    const Rectangle panel = {width - BLOCK_PANEL_WIDTH, HEADER_HEIGHT, BLOCK_PANEL_WIDTH, height - HEADER_HEIGHT};
    const Rectangle view = {BLOCK_LIST_WIDTH, HEADER_HEIGHT, width - BLOCK_LIST_WIDTH - BLOCK_PANEL_WIDTH, height - HEADER_HEIGHT};

    if (!typing()) {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
                          IsKeyDown(KEY_RIGHT_SUPER);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool z = IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z);
        const bool y = IsKeyPressed(KEY_Y) || IsKeyPressedRepeat(KEY_Y);
        if (ctrl && IsKeyPressed(KEY_S)) save_block(selected_block);
        if (ctrl && z && !shift) block_undo();
        else if (ctrl && ((z && shift) || y)) block_redo();
    }

    update_block_camera(view);
    update_block_particles(GetFrameTime());
    draw_block_preview(view);

    BeginDrawing();
    ClearBackground(gui_color(DEFAULT, BACKGROUND_COLOR));
    DrawTexturePro(block_view_texture.texture,
                   {0, 0, static_cast<float>(block_view_texture.texture.width), -static_cast<float>(block_view_texture.texture.height)},
                   view, {0, 0}, 0.0f, WHITE);
    label({view.x + PAD, view.y + view.height - ROW - 4, view.width - PAD * 2, ROW}, tr("editor.block_view_hint"));
    if (selected_block >= 0) {
        const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
        label({view.x + PAD, view.y + PAD, view.width - PAD * 2, ROW},
              display_name(block.name) + "  (" + block.name + ", id " + std::to_string(block.id) + ")   " +
                  slot_name(block, selected_face));
        // Which of its states is shown (a torch: on the floor, on a wall).
        const int states = shape_state_count(kind_of(block));
        if (block_state_view >= states) block_state_view = 0;
        for (int s = 0; states > 1 && s < states; ++s) {
            bool shown = block_state_view == s;
            GuiToggle({view.x + PAD + s * (130 + GAP), view.y + PAD + ROW + GAP, 130, ROW},
                      tr(std::string("editor.block_state.") + block_file::state_id(block.shape, s)).c_str(), &shown);
            if (shown) block_state_view = s;
        }
        // A block that joins sideways: alone, or joined into its wide block.
        const bool joins = kind_of(block) == BlockShapeKind::Cube && block.directional && block.joins;
        for (int v = 0; joins && v < 2; ++v) {
            bool shown = block_large_view == (v == 1);
            GuiToggle({view.x + PAD + v * (130 + GAP), view.y + PAD + ROW + GAP, 130, ROW},
                      tr(v == 0 ? "editor.block_view_single" : "editor.block_view_large").c_str(), &shown);
            if (shown) block_large_view = v == 1;
        }
        GuiCheckBox({view.x + PAD, view.y + PAD + (ROW + GAP) * (states > 1 || joins ? 2 : 1) + 4, ROW - 8, ROW - 8},
                    tr("editor.hitbox_show").c_str(), &show_hitbox);
    }
    // Over the view's top right corner: how the game will show it.
    const float side = std::min(340.0f, view.width * 0.45f);
    const Rectangle game_box = {view.x + view.width - side - PAD, view.y + PAD, side, side * 0.68f};
    draw_block_game_view(game_box);
    const float icon_box_height = 16.0f * 2.0f * game_ui_scale + ROW + PAD * 3.0f;
    draw_block_inventory_icon({game_box.x, game_box.y + game_box.height + GAP, side, icon_box_height});
    draw_block_list(list);
    // The right panel: its tabs, then the open one.
    const Rectangle panel_tabs = {panel.x, panel.y, panel.width, ROW + PAD};
    const Rectangle panel_body = {panel.x, panel.y + panel_tabs.height, panel.width, panel.height - panel_tabs.height};
    if (block_panel_tab == 0) draw_block_properties(panel_body);
    else if (block_panel_tab == 1) draw_block_model_panel(panel_body);
    else if (block_panel_tab == 2) draw_block_hitbox_panel(panel_body);
    else draw_block_particles_panel(panel_body);
    GuiPanel(panel_tabs, nullptr);
    {
        const char* tab_keys[4] = {"editor.block_tab_properties", "editor.block_tab_model", "editor.block_tab_hitbox",
                                   "editor.block_tab_particles"};
        const float tab_width = (panel_tabs.width - PAD * 2 - GAP * 3) / 4.0f;
        // Four across: a size smaller, so each name fits.
        const int text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
        GuiSetStyle(DEFAULT, TEXT_SIZE, text_size * 13 / 16);
        for (int t = 0; t < 4; ++t) {
            bool open = block_panel_tab == t;
            GuiToggle({panel_tabs.x + PAD + t * (tab_width + GAP), panel_tabs.y + PAD * 0.5f, tab_width, ROW}, tr(tab_keys[t]).c_str(), &open);
            if (open) block_panel_tab = t;
        }
        GuiSetStyle(DEFAULT, TEXT_SIZE, text_size);
    }
    draw_blocks_top_bar(top_bar_rect());
    draw_tabs();
    EndDrawing();
    commit_block_history();
}

void ModelEditor::draw_blocks_top_bar(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    float x = bounds.x + PAD;
    const float y = bounds.y + (bounds.height - ROW) * 0.5f;
    GuiSetState(selected_block >= 0 ? STATE_NORMAL : STATE_DISABLED);
    if (GuiButton({x, y, 210, ROW}, tr("editor.block_save").c_str())) save_block(selected_block);
    x += 210 + GAP;
    GuiSetState(STATE_NORMAL);
    if (GuiButton({x, y, 170, ROW}, tr("editor.block_save_all").c_str())) save_all_blocks();
    x += 170 + GAP * 3;

    int unsaved = 0;
    for (bool dirty_block : block_dirty) unsaved += dirty_block ? 1 : 0;
    std::string line = unsaved > 0 ? tr_format("editor.blocks_unsaved", {std::to_string(unsaved)}) : tr("editor.blocks_restart_hint");
    if (GetTime() - status_time < 4.0) line = status;
    label({x, y, bounds.width - x - PAD, ROW}, line);
}

// ----------------------------------------------------------------- List --

void ModelEditor::draw_block_list(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    // Search by the game's name or the id.
    label({bounds.x + PAD, bounds.y + PAD, 70, ROW}, tr("editor.blocks_search"));
    string_field({bounds.x + PAD + 70, bounds.y + PAD, bounds.width - PAD * 2 - 70, ROW}, block_search);

    std::vector<int> shown;
    const std::string needle = lower(block_search);
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (needle.empty() || lower(blocks[i].name).find(needle) != std::string::npos ||
            lower(display_name(blocks[i].name)).find(needle) != std::string::npos) {
            shown.push_back(static_cast<int>(i));
        }
    }

    const Rectangle area = {bounds.x, bounds.y + PAD * 2 + ROW, bounds.width, bounds.height - PAD * 2 - ROW};
    Rectangle view{};
    const float content_height = static_cast<float>(shown.size()) * LIST_ROW + PAD;
    GuiScrollPanel(area, nullptr, {0, 0, area.width - 14, content_height}, &block_list_scroll, &view);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const Texture2D& atlas = terrain_atlas();
    const Vector2 mouse = GetMousePosition();
    const bool mouse_inside = CheckCollisionPointRec(mouse, view);
    for (size_t row = 0; row < shown.size(); ++row) {
        const int index = shown[row];
        const block_file::BlockFile& block = blocks[static_cast<size_t>(index)];
        const Rectangle r = {view.x, view.y + block_list_scroll.y + row * LIST_ROW, view.width, LIST_ROW};
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        const bool hovered = mouse_inside && CheckCollisionPointRec(mouse, r);
        if (index == selected_block) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
        else if (hovered) DrawRectangleRec(r, Fade(WHITE, 0.06f));
        // Its top face as an icon.
        const block_file::Face& top = block.faces[0];
        DrawTexturePro(atlas, tile_source(top.tile_x, top.tile_y), {r.x + 6, r.y + 3, 24, 24}, {0, 0}, 0.0f, top.tint);
        const std::string text = display_name(block.name) + (block_dirty[static_cast<size_t>(index)] ? " *" : "");
        label({r.x + 34, r.y, r.width - 40, r.height}, text);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && index != selected_block) select_block(index);
    }
    EndScissorMode();
}

// ----------------------------------------------------------- Properties --

void ModelEditor::draw_block_properties(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 900.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_panel_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    const float label_w = 200.0f;
    float y = view.y + block_panel_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto combo = [&](const std::string& caption, int& value, const char* const* ids, int count, const char* key_prefix) {
        label({x, y, label_w, ROW}, caption);
        std::string items;
        for (int i = 0; i < count; ++i) items += (i ? ";" : "") + tr(std::string(key_prefix) + ids[i]);
        int chosen = value;
        GuiComboBox({x + label_w, y, width - label_w, ROW}, items.c_str(), &chosen);
        if (chosen != value) {
            value = chosen;
            mark_block_dirty();
        }
        y += ROW + GAP;
    };
    auto number = [&](const std::string& caption, float& value, float min_value, float max_value) {
        label({x, y, label_w, ROW}, caption);
        if (float_field({x + label_w, y, width - label_w, ROW}, value, min_value, max_value)) mark_block_dirty();
        y += ROW + GAP;
    };
    auto whole = [&](const std::string& caption, int& value, int min_value, int max_value) {
        label({x, y, label_w, ROW}, caption);
        if (int_field({x + label_w, y, width - label_w, ROW}, value, min_value, max_value)) mark_block_dirty();
        y += ROW + GAP;
    };
    auto small_hint = [&](const std::string& text, bool error = false) { y = draw_hint(x, y, width, text, error); };
    auto flag = [&](const std::string& caption, bool& value) {
        bool checked = value;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, caption.c_str(), &checked);
        if (checked != value) {
            value = checked;
            mark_block_dirty();
        }
        y += ROW + 2;
    };

    // What it is and how it behaves.
    section(tr("editor.block_properties"));
    combo(tr("editor.block_sound"), block.sound, block_file::SOUND_IDS, block_file::SOUND_COUNT, "editor.sound.");
    combo(tr("editor.block_tool"), block.tool, block_file::TOOL_IDS, block_file::TOOL_COUNT, "editor.tool.");
    number(tr("editor.block_hardness"), block.hardness, 0.0f, 1000.0f);
    number(tr("editor.block_density"), block.density, 0.0f, 50.0f);
    whole(tr("editor.block_luminance"), block.luminance, 0, 15);
    whole(tr("editor.block_side_inset"), block.side_inset, 0, 8);
    y += GAP;
    flag(tr("editor.block_solid"), block.solid);
    flag(tr("editor.block_selectable"), block.selectable);
    flag(tr("editor.block_replaceable"), block.replaceable);
    flag(tr("editor.block_transparent"), block.transparent);
    flag(tr("editor.block_translucent"), block.translucent);
    flag(tr("editor.block_cutout"), block.cutout);
    flag(tr("editor.block_keep_same_faces"), block.keep_same_faces);
    flag(tr("editor.block_damages_on_touch"), block.damages_on_touch);
    flag(tr("editor.block_directional"), block.directional);

    // Its shape, and what that shape needs.
    y += GAP;
    section(tr("editor.block_shape_section"));
    {
        if (block.shape >= block_file::FILE_SHAPE_COUNT) block.shape = 0;
        std::string items;
        for (int i = 0; i < block_file::FILE_SHAPE_COUNT; ++i) items += (i ? ";" : "") + tr(std::string("editor.shape.") + block_file::SHAPE_IDS[i]);
        label({x, y, label_w, ROW}, tr("editor.block_shape"));
        int chosen = block.shape;
        GuiComboBox({x + label_w, y, width - label_w, ROW}, items.c_str(), &chosen);
        if (chosen != block.shape) {
            block.shape = chosen;
            if (selected_face >= 6) selected_face = 0;
            for (int s = 0; s < MAX_BLOCK_STATES; ++s) block.states[static_cast<size_t>(s)] = block_file::default_state(block.shape, s);
            block.elements = block_file::default_elements(block.shape);
            block_state_view = 0;
            selected_element = 0;
            mark_block_dirty();
        }
        y += ROW + GAP;
        small_hint(tr(std::string("editor.shape_hint.") + block_file::SHAPE_IDS[block.shape]));
    }
    {
        // Its soil: the blocks it can stand on, by name, comma separated.
        std::string soil;
        for (size_t i = 0; i < block.placed_on.size(); ++i) soil += (i ? ", " : "") + block.placed_on[i];
        label({x, y, label_w, ROW}, tr("editor.block_placed_on"));
        if (string_field({x + label_w, y, width - label_w, ROW}, soil)) {
            block.placed_on.clear();
            std::string name;
            for (size_t i = 0; i <= soil.size(); ++i) {
                const char c = i < soil.size() ? soil[i] : ',';
                if (c == ',' || c == ' ' || c == ';') {
                    if (!name.empty()) block.placed_on.push_back(name);
                    name.clear();
                } else {
                    name += c;
                }
            }
            mark_block_dirty();
        }
        y += ROW + 2;
        std::string unknown;
        for (const std::string& name : block.placed_on) {
            bool found = false;
            for (const block_file::BlockFile& other : blocks) found = found || other.name == name;
            if (!found) unknown += (unknown.empty() ? "" : ", ") + name;
        }
        if (!unknown.empty()) small_hint(tr_format("editor.block_placed_on_unknown", {unknown}), true);
        else small_hint(tr(block.placed_on.empty() ? "editor.block_placed_on_anywhere" : "editor.block_placed_on_hint"));
    }
    if (kind_of(block) == BlockShapeKind::Slab) {
        // What two halves in one cell become - another block, by name.
        label({x, y, label_w, ROW}, tr("editor.block_double"));
        if (string_field({x + label_w, y, width - label_w, ROW}, block.double_block)) mark_block_dirty();
        y += ROW + 2;
        std::string found;
        for (const block_file::BlockFile& other : blocks) {
            if (other.name == block.double_block) found = display_name(other.name);
        }
        small_hint(block.double_block.empty() ? tr("editor.block_double_none")
                   : found.empty()            ? tr("editor.block_double_missing")
                                              : "= " + found,
                   !block.double_block.empty() && found.empty());
    }
    if (is_pair_kind(kind_of(block))) {
        // Which half this is, and its other half - by name.
        label({x, y, label_w, ROW}, tr("editor.block_half"));
        const std::string halves = tr(std::string("editor.half.") + block_file::half_id(block.shape, 0)) + ";" +
                                   tr(std::string("editor.half.") + block_file::half_id(block.shape, 1));
        int chosen_half = block.half;
        GuiComboBox({x + label_w, y, width - label_w, ROW}, halves.c_str(), &chosen_half);
        if (chosen_half != block.half) {
            block.half = chosen_half;
            mark_block_dirty();
        }
        y += ROW + GAP;
        label({x, y, label_w, ROW}, tr("editor.block_partner"));
        if (string_field({x + label_w, y, width - label_w, ROW}, block.partner)) mark_block_dirty();
        y += ROW + 2;
        const block_file::BlockFile* partner = nullptr;
        for (const block_file::BlockFile& other : blocks) {
            if (other.name == block.partner && &other != &block) partner = &other;
        }
        if (!partner) small_hint(tr("editor.block_partner_missing"), true);
        else if (partner->partner != block.name) small_hint(tr_format("editor.block_partner_mismatch", {display_name(partner->name)}), true);
        else if (partner->half == block.half) small_hint(tr("editor.block_partner_same_half"), true);
        else small_hint("= " + display_name(partner->name));
        flag(tr("editor.block_pair_item"), block.item);
        small_hint(tr(block.item ? "editor.block_pair_item_hint" : "editor.block_pair_not_item_hint"));
    }
    if (kind_of(block) == BlockShapeKind::Cube && block.directional) {
        // Two of it side by side, facing the same way, become one wide block.
        flag(tr("editor.block_joins"), block.joins);
        if (block.joins) small_hint(tr("editor.block_joins_hint"));
    }

    // Its six faces and any extra tiles (a cake's cut, a bed's end, a large
    // chest's halves): pick one, then its tile below.
    y += GAP;
    section(tr("editor.block_faces"));
    const Texture2D& atlas = terrain_atlas();
    const std::vector<TileSlot> extras = extra_slots(block);
    const int face_rows = 6 + static_cast<int>(extras.size());
    if (selected_face >= face_rows) selected_face = 0;
    const bool extra_picked = selected_face >= 6;
    constexpr float ROW_LABEL = 205.0f;
    for (int f = 0; f < face_rows; ++f) {
        const TileSlot* slot = f >= 6 ? &extras[static_cast<size_t>(f - 6)] : nullptr;
        const int tile_x = slot ? *slot->x : block.faces[f].tile_x;
        const int tile_y = slot ? *slot->y : block.faces[f].tile_y;
        const Color tint = slot ? block.faces[3].tint : block.faces[f].tint; // an extra tile takes its side's tint
        bool picked = f == selected_face;
        GuiToggle({x, y, ROW_LABEL, ROW}, (slot ? tr(slot->key) : tr(FACE_KEYS[f])).c_str(), &picked);
        if (picked) selected_face = f;
        if (!slot || !slot->has || *slot->has) {
            DrawTexturePro(atlas, tile_source(tile_x, tile_y), {x + ROW_LABEL + 8, y + 1, ROW - 2, ROW - 2}, {0, 0}, 0.0f, tint);
            label({x + ROW_LABEL + 8 + ROW + 4, y, 90, ROW}, std::to_string(tile_x) + ", " + std::to_string(tile_y));
        } else {
            label({x + ROW_LABEL + 8, y, width - ROW_LABEL - 8, ROW}, tr("editor.block_cut_none"));
        }
        y += ROW + 2;
    }
    y += GAP;
    const float half = (width - GAP) * 0.5f;
    GuiSetState(extra_picked ? STATE_DISABLED : STATE_NORMAL);
    if (GuiButton({x, y, half, ROW}, tr("editor.block_to_all").c_str()) && !extra_picked) {
        for (int f = 0; f < 6; ++f) block.faces[f] = block.faces[selected_face];
        mark_block_dirty();
    }
    if (GuiButton({x + half + GAP, y, half, ROW}, tr("editor.block_to_sides").c_str()) && !extra_picked) {
        for (int f = 2; f < 6; ++f) block.faces[f] = block.faces[selected_face];
        mark_block_dirty();
    }
    GuiSetState(STATE_NORMAL);
    y += ROW + GAP;

    // The picked face's tint: its swatch, then red, green, blue, alpha (an
    // extra tile has none of its own - it takes its side's).
    if (!extra_picked) {
        block_file::Face& face = block.faces[selected_face];
        label({x, y, width - ROW - GAP, ROW}, tr("editor.block_tint"));
        DrawRectangleRec({x + width - ROW, y, ROW, ROW}, face.tint);
        DrawRectangleLinesEx({x + width - ROW, y, ROW, ROW}, 1.0f, gui_color(DEFAULT, LINE_COLOR));
        y += ROW + GAP;
        const float field = (width - GAP * 3) / 4.0f;
        unsigned char* channels[4] = {&face.tint.r, &face.tint.g, &face.tint.b, &face.tint.a};
        for (int c = 0; c < 4; ++c) {
            float value = *channels[c];
            if (float_field({x + c * (field + GAP), y, field, ROW}, value, 0.0f, 255.0f)) {
                *channels[c] = static_cast<unsigned char>(std::lround(value));
                mark_block_dirty();
            }
        }
        y += ROW + GAP;
        // Recolored per biome in the world (the tint above is then its color
        // in the inventory and here).
        combo(tr("editor.block_biome"), face.biome, block_file::BIOME_IDS, block_file::BIOME_COUNT, "editor.biome.");
        y += GAP;
    }

    // A tile picker over a whole 16x16 atlas; `picked` outlines the current
    // tile, `used` (optional) the other ones; returns a clicked tile.
    auto atlas_picker = [&](const Texture2D& texture, int picked_x, int picked_y, bool show_picked,
                            const std::vector<std::pair<int, int>>& used) -> std::optional<std::pair<int, int>> {
        const float cell = std::floor(width / block_file::ATLAS_TILES);
        const Rectangle grid = {x, y, cell * block_file::ATLAS_TILES, cell * block_file::ATLAS_TILES};
        DrawRectangleRec(grid, Color{30, 30, 34, 255});
        DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)}, grid, {0, 0}, 0.0f, WHITE);
        for (const auto& [ux, uy] : used) {
            DrawRectangleLinesEx({grid.x + ux * cell, grid.y + uy * cell, cell, cell}, 1.0f, Fade(WHITE, 0.5f));
        }
        if (show_picked) {
            DrawRectangleLinesEx({grid.x + picked_x * cell - 1, grid.y + picked_y * cell - 1, cell + 2, cell + 2}, 2.0f, SELECTION);
        }
        std::optional<std::pair<int, int>> clicked;
        const Vector2 mouse = GetMousePosition();
        if (mouse_inside && CheckCollisionPointRec(mouse, grid)) {
            const int tx = std::clamp(static_cast<int>((mouse.x - grid.x) / cell), 0, block_file::ATLAS_TILES - 1);
            const int ty = std::clamp(static_cast<int>((mouse.y - grid.y) / cell), 0, block_file::ATLAS_TILES - 1);
            DrawRectangleLinesEx({grid.x + tx * cell, grid.y + ty * cell, cell, cell}, 1.0f, YELLOW);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clicked = std::make_pair(tx, ty);
        }
        y += grid.height + PAD;
        return clicked;
    };

    // The terrain atlas: click a tile to put it on the picked face.
    section(tr("editor.block_atlas"));
    {
        std::vector<std::pair<int, int>> used;
        for (int f = 0; f < 6; ++f) used.push_back({block.faces[f].tile_x, block.faces[f].tile_y});
        const TileSlot* slot = extra_picked ? &extras[static_cast<size_t>(selected_face - 6)] : nullptr;
        const int px = slot ? *slot->x : block.faces[selected_face].tile_x;
        const int py = slot ? *slot->y : block.faces[selected_face].tile_y;
        if (auto clicked = atlas_picker(atlas, px, py, !slot || !slot->has || *slot->has, used)) {
            if (slot) {
                if (slot->has) *slot->has = true;
                *slot->x = clicked->first;
                *slot->y = clicked->second;
            } else {
                block.faces[selected_face].tile_x = clicked->first;
                block.faces[selected_face].tile_y = clicked->second;
            }
            mark_block_dirty();
        }
    }

    // In the inventory: its 3D look, or a flat sprite picked off items.png.
    section(tr("editor.block_icon_section"));
    {
        bool flat = block.item_sprite_x >= 0;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.block_flat_icon").c_str(), &flat);
        if (flat != (block.item_sprite_x >= 0)) {
            block.item_sprite_x = flat ? 0 : -1;
            block.item_sprite_y = flat ? 0 : -1;
            mark_block_dirty();
        }
        y += ROW + GAP;
        if (flat) {
            if (auto clicked = atlas_picker(items_atlas(), block.item_sprite_x, block.item_sprite_y, true, {})) {
                block.item_sprite_x = clicked->first;
                block.item_sprite_y = clicked->second;
                mark_block_dirty();
            }
        }
    }

    content_height = y - (view.y + block_panel_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// --------------------------------------------------------------- Hitbox --

void ModelEditor::draw_block_hitbox_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 600.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_hitbox_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
    const BlockShapeKind kind = kind_of(block);

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    float y = view.y + block_hitbox_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto hint = [&](const std::string& text) { y = draw_hint(x, y, width, text, false); };
    // A caption, then its X, Y and Z fields side by side; true once changed.
    auto vector_row = [&](const std::string& caption, Vector3& value, float min_value, float max_value) {
        label({x, y, width, ROW}, caption);
        y += ROW;
        constexpr const char* AXES[3] = {"X", "Y", "Z"};
        constexpr Color AXIS_COLORS[3] = {{230, 90, 90, 255}, {120, 210, 90, 255}, {90, 150, 240, 255}};
        float* parts[3] = {&value.x, &value.y, &value.z};
        const float field = (width - GAP * 2) / 3.0f;
        bool changed = false;
        for (int i = 0; i < 3; ++i) {
            const float fx = x + i * (field + GAP);
            DrawTextEx(editor_text::font(), AXES[i], {fx + 2, y + 5}, 16, 1, AXIS_COLORS[i]);
            if (float_field({fx + 16, y, field - 16, ROW}, *parts[i], min_value, max_value)) changed = true;
        }
        y += ROW + GAP;
        return changed;
    };

    section(tr("editor.hitbox_section"));
    {
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.hitbox_show").c_str(), &show_hitbox);
        y += ROW + GAP;
    }

    // Which state is edited (the one the preview shows).
    const int states = shape_state_count(kind);
    if (block_state_view >= states) block_state_view = 0;
    if (states > 1) {
        label({x, y, width, ROW}, tr("editor.block_state"));
        y += ROW;
        const float toggle = (width - GAP * (states - 1)) / static_cast<float>(states);
        for (int s = 0; s < states; ++s) {
            bool shown = block_state_view == s;
            GuiToggle({x + s * (toggle + GAP), y, toggle, ROW},
                      tr(std::string("editor.block_state.") + block_file::state_id(block.shape, s)).c_str(), &shown);
            if (shown) block_state_view = s;
        }
        y += ROW + GAP;
    }
    block_file::StateModel& state = block.states[static_cast<size_t>(block_state_view)];

    // The box: where it lies (its min corner) and how big, in texture pixels.
    if (kind != BlockShapeKind::Cube && kind != BlockShapeKind::Torch && kind != BlockShapeKind::Cross) {
        hint(tr("editor.hitbox_is_shape"));
    } else {
        if (kind == BlockShapeKind::Cube || kind == BlockShapeKind::Cross) {
            bool own = state.has_hitbox;
            GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.hitbox_own").c_str(), &own);
            if (own != state.has_hitbox) {
                state.has_hitbox = own;
                mark_block_dirty();
            }
            y += ROW + GAP;
        } else {
            state.has_hitbox = true; // a torch always aims at its own small box
        }
        if (state.has_hitbox) {
            Vector3 position = state.hitbox_from;
            Vector3 size = Vector3Subtract(state.hitbox_to, state.hitbox_from);
            if (vector_row(tr("editor.hitbox_position"), position, 0.0f, 16.0f)) {
                // Moving keeps its size: it stops at the cell's far side.
                position = {std::min(position.x, 16.0f - size.x), std::min(position.y, 16.0f - size.y), std::min(position.z, 16.0f - size.z)};
                state.hitbox_from = position;
                state.hitbox_to = Vector3Add(position, size);
                mark_block_dirty();
            }
            if (vector_row(tr("editor.hitbox_size"), size, 0.0f, 16.0f)) {
                size = {std::min(size.x, 16.0f - position.x), std::min(size.y, 16.0f - position.y), std::min(size.z, 16.0f - position.z)};
                state.hitbox_to = Vector3Add(state.hitbox_from, size);
                mark_block_dirty();
            }
            hint(tr(kind == BlockShapeKind::Torch ? "editor.hitbox_units_wall" : "editor.hitbox_units"));
        } else {
            hint(tr("editor.hitbox_whole"));
        }
    }

    // Where its model sits in this state: moved, then tilted about the pivot.
    if (kind == BlockShapeKind::Torch) {
        y += GAP;
        section(tr("editor.model_place_section"));
        if (vector_row(tr("editor.model_offset"), state.offset, -16.0f, 16.0f)) mark_block_dirty();
        if (vector_row(tr("editor.model_pivot"), state.pivot, -16.0f, 32.0f)) mark_block_dirty();
        label({x, y, 200, ROW}, tr("editor.model_angle"));
        if (float_field({x + 200, y, width - 200, ROW}, state.angle, -90.0f, 90.0f)) mark_block_dirty();
        y += ROW + GAP;
        hint(tr("editor.model_place_hint"));
    }

    y += GAP;
    if (GuiButton({x, y, width, ROW}, tr("editor.block_state_reset").c_str())) {
        state = block_file::default_state(block.shape, block_state_view);
        mark_block_dirty();
    }
    y += ROW + PAD;

    content_height = y - (view.y + block_hitbox_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// ---------------------------------------------------------------- Model --

void ModelEditor::draw_block_model_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 800.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_model_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    float y = view.y + block_model_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto hint = [&](const std::string& text) { y = draw_hint(x, y, width, text, false); };
    // A caption, then its fields side by side, each with a short colored
    // name; true once one changed.
    auto fields_row = [&](const std::string& caption, float* const* values, const char* const* names, int count, float min_value,
                          float max_value) {
        label({x, y, width, ROW}, caption);
        y += ROW;
        constexpr Color NAME_COLORS[4] = {{230, 90, 90, 255}, {120, 210, 90, 255}, {90, 150, 240, 255}, {220, 200, 90, 255}};
        const float field = (width - GAP * (count - 1)) / static_cast<float>(count);
        const float name_width = std::strlen(names[0]) > 1 ? 24.0f : 16.0f;
        bool changed = false;
        for (int i = 0; i < count; ++i) {
            const float fx = x + i * (field + GAP);
            DrawTextEx(editor_text::font(), names[i], {fx + 2, y + 5}, 16, 1, NAME_COLORS[i % 4]);
            if (float_field({fx + name_width, y, field - name_width, ROW}, *values[i], min_value, max_value)) changed = true;
        }
        y += ROW + GAP;
        return changed;
    };
    auto check = [&](const std::string& caption, bool& value) {
        bool checked = value;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, caption.c_str(), &checked);
        y += ROW + 2;
        if (checked == value) return false;
        value = checked;
        mark_block_dirty();
        return true;
    };

    section(tr("editor.model_section"));
    if (!block_file::elements_allowed(block.shape)) {
        hint(tr("editor.model_kind_only"));
    } else {
        // A cube may be drawn from parts instead - starting from one whole
        // cube; a torch always is.
        if (kind_of(block) == BlockShapeKind::Cube) {
            bool own = !block.elements.empty();
            if (check(tr("editor.model_own"), own)) {
                block.elements.clear();
                if (own) {
                    block_file::Element whole;
                    whole.name = "cube";
                    block.elements.push_back(whole);
                }
                selected_element = 0;
            }
        }
        if (block.elements.empty()) hint(tr("editor.model_none"));
    }

    if (has_elements(block)) {
        selected_element = std::clamp(selected_element, 0, static_cast<int>(block.elements.size()) - 1);
        hint(tr("editor.model_parts_hint"));

        // Its parts - pick one to edit.
        for (size_t i = 0; i < block.elements.size(); ++i) {
            const block_file::Element& element = block.elements[i];
            const std::string caption = element.name.empty() ? tr_format("editor.model_part", {std::to_string(i + 1)}) : element.name;
            bool picked = static_cast<int>(i) == selected_element;
            GuiToggle({x, y, width, ROW}, caption.c_str(), &picked);
            if (picked) selected_element = static_cast<int>(i);
            y += ROW + 2;
        }
        y += GAP;
        const float third = (width - GAP * 2) / 3.0f;
        if (GuiButton({x, y, third, ROW}, tr("editor.model_add").c_str())) {
            block_file::Element part;
            part.from = {6, 0, 6};
            part.to = {10, 8, 10};
            block.elements.push_back(part);
            selected_element = static_cast<int>(block.elements.size()) - 1;
            mark_block_dirty();
        }
        if (GuiButton({x + third + GAP, y, third, ROW}, tr("editor.model_copy").c_str())) {
            block_file::Element copy = block.elements[static_cast<size_t>(selected_element)];
            if (!copy.name.empty()) copy.name += "_copy";
            block.elements.insert(block.elements.begin() + selected_element + 1, copy);
            ++selected_element;
            mark_block_dirty();
        }
        GuiSetState(block.elements.size() > 1 ? STATE_NORMAL : STATE_DISABLED);
        if (GuiButton({x + (third + GAP) * 2, y, third, ROW}, tr("editor.model_delete").c_str()) && block.elements.size() > 1) {
            block.elements.erase(block.elements.begin() + selected_element);
            selected_element = std::min(selected_element, static_cast<int>(block.elements.size()) - 1);
            mark_block_dirty();
        }
        GuiSetState(STATE_NORMAL);
        y += ROW + GAP * 2;

        // The picked part: its name, its box, its shading.
        block_file::Element& element = block.elements[static_cast<size_t>(selected_element)];
        section(tr("editor.model_part_section"));
        label({x, y, 110, ROW}, tr("editor.model_name"));
        if (string_field({x + 110, y, width - 110, ROW}, element.name)) mark_block_dirty();
        y += ROW + GAP;
        const char* xyz[3] = {"X", "Y", "Z"};
        float* from[3] = {&element.from.x, &element.from.y, &element.from.z};
        float* to[3] = {&element.to.x, &element.to.y, &element.to.z};
        if (fields_row(tr("editor.model_from"), from, xyz, 3, -16.0f, 32.0f)) mark_block_dirty();
        if (fields_row(tr("editor.model_to"), to, xyz, 3, -16.0f, 32.0f)) mark_block_dirty();
        check(tr("editor.model_shade"), element.shade);
        hint(tr("editor.model_shade_hint"));

        // Its faces: which are drawn and which way each looks.
        y += GAP;
        section(tr("editor.model_faces_section"));
        if (selected_face >= 6) selected_face = 0;
        std::string normals;
        for (int n = 0; n < block_file::NORMAL_COUNT; ++n) normals += (n ? ";" : "") + tr(std::string("editor.normal.") + block_file::NORMAL_IDS[n]);
        for (int f = 0; f < 6; ++f) {
            block_file::ElementFace& face = element.faces[static_cast<size_t>(f)];
            bool enabled = face.enabled;
            GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, nullptr, &enabled);
            if (enabled != face.enabled) {
                face.enabled = enabled;
                mark_block_dirty();
            }
            bool picked = f == selected_face;
            GuiToggle({x + ROW, y, 150, ROW}, tr(FACE_KEYS[f]).c_str(), &picked);
            if (picked) selected_face = f;
            GuiSetState(face.enabled ? STATE_NORMAL : STATE_DISABLED);
            int normal = face.normal;
            GuiComboBox({x + ROW + 150 + GAP, y, width - ROW - 150 - GAP, ROW}, normals.c_str(), &normal);
            if (normal != face.normal && face.enabled) {
                face.normal = normal;
                mark_block_dirty();
            }
            GuiSetState(STATE_NORMAL);
            y += ROW + 2;
        }
        hint(tr("editor.model_normals_hint"));

        // The picked face's piece of its side's tile.
        y += GAP;
        section(tr_format("editor.model_uv_section", {tr(FACE_KEYS[selected_face])}));
        block_file::ElementFace& face = element.faces[static_cast<size_t>(selected_face)];
        bool automatic = face.auto_uv;
        if (check(tr("editor.model_auto_uv"), automatic)) {
            if (!automatic) face.uv = block_file::face_uv(element, selected_face); // start from what it shows now
            face.auto_uv = automatic;
        }
        std::array<float, 4> uv = block_file::face_uv(element, selected_face);
        const char* uv_names[4] = {"U1", "V1", "U2", "V2"};
        float* uv_values[4] = {&uv[0], &uv[1], &uv[2], &uv[3]};
        if (fields_row(tr("editor.model_uv"), uv_values, uv_names, 4, 0.0f, 16.0f)) {
            face.auto_uv = false;
            face.uv = uv;
            mark_block_dirty();
        }

        // The side's tile, big: drag over it to pick the piece, a pixel at a time.
        const block_file::Face& side = block.faces[static_cast<size_t>(selected_face)];
        const float cell = std::floor(std::min(width, 256.0f) / 16.0f);
        const Rectangle tile = {x, y, cell * 16.0f, cell * 16.0f};
        DrawRectangleRec(tile, Color{30, 30, 34, 255});
        DrawTexturePro(terrain_atlas(), tile_source(side.tile_x, side.tile_y), tile, {0, 0}, 0.0f, side.tint);
        for (int i = 1; i < 16; ++i) {
            DrawLineV({tile.x + i * cell, tile.y}, {tile.x + i * cell, tile.y + tile.height}, Fade(WHITE, 0.06f));
            DrawLineV({tile.x, tile.y + i * cell}, {tile.x + tile.width, tile.y + i * cell}, Fade(WHITE, 0.06f));
        }
        const Rectangle piece = {tile.x + std::min(uv[0], uv[2]) * cell, tile.y + std::min(uv[1], uv[3]) * cell,
                                 std::fabs(uv[2] - uv[0]) * cell, std::fabs(uv[3] - uv[1]) * cell};
        DrawRectangleLinesEx(piece, 2.0f, SELECTION);
        const Vector2 mouse = GetMousePosition();
        auto pixel_at = [&](Vector2 point) {
            return Vector2{std::clamp(std::floor((point.x - tile.x) / cell), 0.0f, 15.0f), std::clamp(std::floor((point.y - tile.y) / cell), 0.0f, 15.0f)};
        };
        if (mouse_inside && CheckCollisionPointRec(mouse, tile) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            uv_dragging = true;
            uv_drag_from = pixel_at(mouse);
        }
        if (uv_dragging) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const Vector2 here = pixel_at(mouse);
                face.auto_uv = false;
                face.uv = {std::min(uv_drag_from.x, here.x), std::min(uv_drag_from.y, here.y), std::max(uv_drag_from.x, here.x) + 1.0f,
                           std::max(uv_drag_from.y, here.y) + 1.0f};
                mark_block_dirty();
            } else {
                uv_dragging = false;
            }
        }
        y += tile.height + GAP;
        hint(tr("editor.model_uv_hint"));
    }

    content_height = y - (view.y + block_model_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// ------------------------------------------------------------ Particles --

void ModelEditor::draw_block_particles_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 700.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_particles_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    const float label_w = 200.0f;
    float y = view.y + block_particles_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto hint = [&](const std::string& text) { y = draw_hint(x, y, width, text, false); };
    // A caption, then its X, Y and Z fields side by side; true once changed.
    auto vector_row = [&](const std::string& caption, Vector3& value, float min_value, float max_value) {
        label({x, y, width, ROW}, caption);
        y += ROW;
        constexpr const char* AXES[3] = {"X", "Y", "Z"};
        constexpr Color AXIS_COLORS[3] = {{230, 90, 90, 255}, {120, 210, 90, 255}, {90, 150, 240, 255}};
        float* parts[3] = {&value.x, &value.y, &value.z};
        const float field = (width - GAP * 2) / 3.0f;
        bool changed = false;
        for (int i = 0; i < 3; ++i) {
            const float fx = x + i * (field + GAP);
            DrawTextEx(editor_text::font(), AXES[i], {fx + 2, y + 5}, 16, 1, AXIS_COLORS[i]);
            if (float_field({fx + 16, y, field - 16, ROW}, *parts[i], min_value, max_value)) changed = true;
        }
        y += ROW + GAP;
        return changed;
    };

    section(tr("editor.particles_section"));
    hint(tr("editor.particles_hint"));

    // Its emitters - pick one to edit.
    if (block.particles.empty()) hint(tr("editor.particles_none"));
    selected_emitter = std::clamp(selected_emitter, 0, std::max(0, static_cast<int>(block.particles.size()) - 1));
    for (size_t i = 0; i < block.particles.size(); ++i) {
        const block_file::ParticleEmitter& emitter = block.particles[i];
        auto whole = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
        const std::string caption = tr(std::string("editor.particle.") + block_file::PARTICLE_IDS[std::clamp(emitter.kind, 0, 3)]) + "  (" +
                                    whole(emitter.at.x) + ", " + whole(emitter.at.y) + ", " + whole(emitter.at.z) + ")";
        bool picked = static_cast<int>(i) == selected_emitter;
        GuiToggle({x, y, width, ROW}, caption.c_str(), &picked);
        if (picked) selected_emitter = static_cast<int>(i);
        y += ROW + 2;
    }
    y += GAP;
    const float third = (width - GAP * 2) / 3.0f;
    if (GuiButton({x, y, third, ROW}, tr("editor.particles_add").c_str())) {
        block_file::ParticleEmitter emitter;
        emitter.at = {8, 16, 8};
        block.particles.push_back(emitter);
        selected_emitter = static_cast<int>(block.particles.size()) - 1;
        mark_block_dirty();
    }
    GuiSetState(block.particles.empty() ? STATE_DISABLED : STATE_NORMAL);
    if (GuiButton({x + third + GAP, y, third, ROW}, tr("editor.model_copy").c_str()) && !block.particles.empty()) {
        const block_file::ParticleEmitter copy = block.particles[static_cast<size_t>(selected_emitter)];
        block.particles.insert(block.particles.begin() + selected_emitter + 1, copy);
        ++selected_emitter;
        mark_block_dirty();
    }
    if (GuiButton({x + (third + GAP) * 2, y, third, ROW}, tr("editor.model_delete").c_str()) && !block.particles.empty()) {
        block.particles.erase(block.particles.begin() + selected_emitter);
        selected_emitter = std::max(0, selected_emitter - 1);
        mark_block_dirty();
    }
    GuiSetState(STATE_NORMAL);
    y += ROW + GAP * 2;

    if (!block.particles.empty()) {
        block_file::ParticleEmitter& emitter = block.particles[static_cast<size_t>(selected_emitter)];
        section(tr("editor.particle_section"));
        // What kind.
        {
            std::string kinds;
            for (int k = 0; k < block_file::PARTICLE_COUNT; ++k) kinds += (k ? ";" : "") + tr(std::string("editor.particle.") + block_file::PARTICLE_IDS[k]);
            label({x, y, label_w, ROW}, tr("editor.particle_kind"));
            int chosen = emitter.kind;
            GuiComboBox({x + label_w, y, width - label_w, ROW}, kinds.c_str(), &chosen);
            if (chosen != emitter.kind) {
                emitter.kind = chosen;
                mark_block_dirty();
            }
            y += ROW + GAP;
        }
        // Where, and how far around.
        if (vector_row(tr("editor.particle_at"), emitter.at, -16.0f, 32.0f)) mark_block_dirty();
        if (vector_row(tr("editor.particle_spread"), emitter.spread, 0.0f, 16.0f)) mark_block_dirty();
        hint(tr("editor.particle_at_hint"));
        // How often, how many.
        label({x, y, label_w, ROW}, tr("editor.particle_chance"));
        if (float_field({x + label_w, y, width - label_w, ROW}, emitter.chance, 0.0f, 1.0f)) mark_block_dirty();
        y += ROW + GAP;
        label({x, y, label_w, ROW}, tr("editor.particle_count"));
        if (int_field({x + label_w, y, width - label_w, ROW}, emitter.count, 1, 16)) mark_block_dirty();
        y += ROW + GAP;
        // Its color (a leaf has its block's own).
        if (emitter.kind == 3) {
            hint(tr("editor.particle_leaf_color"));
        } else {
            label({x, y, width - ROW - GAP, ROW}, tr("editor.particle_color"));
            DrawRectangleRec({x + width - ROW, y, ROW, ROW}, emitter.color);
            DrawRectangleLinesEx({x + width - ROW, y, ROW, ROW}, 1.0f, gui_color(DEFAULT, LINE_COLOR));
            y += ROW + GAP;
            const float field = (width - GAP * 3) / 4.0f;
            unsigned char* channels[4] = {&emitter.color.r, &emitter.color.g, &emitter.color.b, &emitter.color.a};
            for (int c = 0; c < 4; ++c) {
                float value = *channels[c];
                if (float_field({x + c * (field + GAP), y, field, ROW}, value, 0.0f, 255.0f)) {
                    *channels[c] = static_cast<unsigned char>(std::lround(value));
                    mark_block_dirty();
                }
            }
            y += ROW + GAP;
        }
        // Only from an open underside (leaves).
        bool above_air = emitter.only_above_air;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.particle_above_air").c_str(), &above_air);
        if (above_air != emitter.only_above_air) {
            emitter.only_above_air = above_air;
            mark_block_dirty();
        }
        y += ROW + GAP;
        hint(tr("editor.particle_rate_hint"));
    }

    content_height = y - (view.y + block_particles_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// -------------------------------------------------------------- Preview --

void ModelEditor::update_block_camera(Rectangle view)
{
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, view);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (over && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        block_view_dragging = IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || alt;
        block_press_position = mouse;
    }
    if (block_view_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 delta = GetMouseDelta();
            block_yaw -= delta.x * 0.008f;
            block_pitch = std::clamp(block_pitch + delta.y * 0.008f, -1.5f, 1.5f);
        } else {
            block_view_dragging = false;
        }
    }
    if (over) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) block_distance = std::clamp(block_distance * std::pow(0.88f, wheel), 1.2f, 8.0f);
    }

    // A plain click on a face picks it.
    if (over && !block_view_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
        Vector2Distance(block_press_position, mouse) < 4.0f && selected_block >= 0) {
        const block_file::BlockFile& picked_block = blocks[static_cast<size_t>(selected_block)];
        const float distance = block_distance * preview_zoom(companion_of(blocks, picked_block, display_state(picked_block, block_state_view), block_large_view));
        Camera3D cam{};
        cam.target = {0, 0, 0};
        cam.position = {distance * std::cos(block_pitch) * std::sin(block_yaw), distance * std::sin(block_pitch),
                        distance * std::cos(block_pitch) * std::cos(block_yaw)};
        cam.up = {0, 1, 0};
        cam.fovy = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        const Ray ray = GetScreenToWorldRayEx(Vector2Subtract(mouse, {view.x, view.y}), cam, static_cast<int>(view.width),
                                              static_cast<int>(view.height));
        // The nearest face it draws under the mouse (where the model is
        // moved/tilted, where it's drawn).
        const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
        const BlockInstanceState state = display_state(block, block_state_view);
        const Vector3 origin = preview_origin(companion_of(blocks, block, state, block_large_view));
        if (const std::optional<PickedFace> picked = face_under_ray(block, state, origin, ray)) {
            selected_face = picked->face;
            if (has_elements(block)) selected_element = picked->part;
        }
    }
}

void ModelEditor::draw_block_preview(Rectangle view)
{
    const int width = std::max(1, static_cast<int>(view.width));
    const int height = std::max(1, static_cast<int>(view.height));
    if (block_view_texture.id == 0 || block_view_texture.texture.width != width || block_view_texture.texture.height != height) {
        if (block_view_texture.id != 0) UnloadRenderTexture(block_view_texture);
        block_view_texture = LoadRenderTexture(width, height);
    }

    float distance = block_distance;
    if (selected_block >= 0) {
        const block_file::BlockFile& shown = blocks[static_cast<size_t>(selected_block)];
        distance *= preview_zoom(companion_of(blocks, shown, display_state(shown, block_state_view), block_large_view));
    }
    Camera3D cam{};
    cam.target = {0, 0, 0};
    cam.position = {distance * std::cos(block_pitch) * std::sin(block_yaw), distance * std::sin(block_pitch),
                    distance * std::cos(block_pitch) * std::cos(block_yaw)};
    cam.up = {0, 1, 0};
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(block_view_texture);
    ClearBackground(VIEWPORT_BACKGROUND);
    BeginMode3D(cam);
    // A floor grid a little under it, for a sense of up.
    for (int i = -3; i <= 3; ++i) {
        DrawLine3D({static_cast<float>(i), -0.5f, -3.0f}, {static_cast<float>(i), -0.5f, 3.0f}, Fade(WHITE, 0.08f));
        DrawLine3D({-3.0f, -0.5f, static_cast<float>(i)}, {3.0f, -0.5f, static_cast<float>(i)}, Fade(WHITE, 0.08f));
    }
    if (selected_block >= 0) {
        const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
        const BlockInstanceState state = display_state(block, block_state_view);
        const Texture2D& atlas = terrain_atlas();
        // With its other half / its joined twin, the pair centered on the orbit point.
        const std::optional<Companion> companion = companion_of(blocks, block, state, block_large_view);
        const Vector3 origin = preview_origin(companion);
        // A cutout block's far faces show through its holes; a block made of
        // parts is shown the way the game draws it - back faces culled - so
        // which way each face looks (its normal setting) can be seen.
        const bool parts = has_elements(block);
        BeginShaderMode(entity_cutout_shader()); // see-through pixels (leaves, glass) stay see-through
        if (!parts) rlDisableBackfaceCulling();
        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
        emit_block(block, state, origin, nullptr, 0, 0, 0, companion ? companion->joined_self : 0);
        if (companion) {
            emit_block(*companion->block, state, Vector3Add(origin, companion->offset), nullptr, 0, 0, 0, companion->joined_other);
        }
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        EndShaderMode();

        draw_particles(preview_particles, cam, particle_sheet(), atlas);
        rlDrawRenderBatchActive();

        // Its emitters (while they're edited): each point, the picked one's
        // spread as a box - where they are on the block as it's shown.
        if (block_panel_tab == 3) {
            const std::vector<BlockParticleEmitter> emitters = block_file::to_emitters(block.particles);
            const BlockInstanceState emit_state = particle_state(block, state);
            const BlockStateModel model = state_model_of(block, state);
            auto at = [&](const BlockParticleEmitter& emitter, Vector3 unit) {
                return Vector3Add(origin, block_particles::emit_point(emitter, unit, model, emit_state, block.directional));
            };
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            for (size_t i = 0; i < emitters.size(); ++i) {
                const bool picked = static_cast<int>(i) == selected_emitter;
                DrawSphere(at(emitters[i], {0, 0, 0}), picked ? 0.025f : 0.018f, picked ? YELLOW : Fade(YELLOW, 0.5f));
                if (!picked) continue;
                constexpr int EDGES[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                auto corner = [&](int c) {
                    return at(emitters[i], {c & 1 ? 1.0f : -1.0f, c & 2 ? 1.0f : -1.0f, c & 4 ? 1.0f : -1.0f});
                };
                for (const auto& edge : EDGES) DrawLine3D(corner(edge[0]), corner(edge[1]), Fade(YELLOW, 0.8f));
            }
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }

        // On a wall: that wall, see-through, behind it (the north one).
        if (state.attachment != BlockFace::Bottom && state.attachment != BlockFace::Top) {
            rlDisableDepthMask();
            DrawCube({0.0f, 0.0f, -1.0f}, 1.0f, 1.0f, 1.0f, Fade(GRAY, 0.18f));
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            DrawCubeWires({0.0f, 0.0f, -1.0f}, 1.0f, 1.0f, 1.0f, Fade(WHITE, 0.3f));
        }

        // The picked face outlined on every box that draws it, just off the
        // surface (a cake's cut: its north side, where it's first bitten) -
        // made of parts: on the picked part, which is framed too, with its
        // faces' normals (while its model is being edited).
        const std::vector<TileSlot> extras = extra_slots(block);
        const int outlined = selected_face < 6 ? selected_face
                           : selected_face - 6 < static_cast<int>(extras.size()) ? extras[static_cast<size_t>(selected_face - 6)].outline
                                                                                 : 0;
        const Vector3 lift = Vector3Scale(FACE_NORMALS[outlined], 0.004f);
        if (parts) {
            selected_element = std::clamp(selected_element, 0, static_cast<int>(block.elements.size()) - 1);
            const block_file::Element& element = block.elements[static_cast<size_t>(selected_element)];
            const BoundingBox box = element_box(element);
            auto at = [&](Vector3 p) { return Vector3Add(origin, placed_point(block, state, p)); };
            if (block_panel_tab == 1) {
                for (int f = 0; f < 6; ++f) {
                    for (int c = 0; c < 4; ++c) DrawLine3D(at(box_corner(box, f, c)), at(box_corner(box, f, (c + 1) % 4)), Fade(SELECTION, 0.45f));
                }
                for (int f = 0; f < 6; ++f) {
                    const block_file::ElementFace& face = element.faces[static_cast<size_t>(f)];
                    if (!face.enabled) continue;
                    Vector3 middle = {0, 0, 0};
                    for (int c = 0; c < 4; ++c) middle = Vector3Add(middle, Vector3Scale(box_corner(box, f, c), 0.25f));
                    const Color color = f == selected_face ? YELLOW : Fade(YELLOW, 0.55f);
                    if (face.normal != 1) DrawLine3D(at(middle), at(Vector3Add(middle, Vector3Scale(FACE_NORMALS[f], 0.18f))), color);
                    if (face.normal != 0) DrawLine3D(at(middle), at(Vector3Subtract(middle, Vector3Scale(FACE_NORMALS[f], 0.18f))), color);
                }
            }
            if (element.faces[static_cast<size_t>(outlined)].enabled) {
                for (int c = 0; c < 4; ++c) {
                    DrawLine3D(at(Vector3Add(box_corner(box, outlined, c), lift)), at(Vector3Add(box_corner(box, outlined, (c + 1) % 4), lift)),
                               SELECTION);
                }
            }
        }
        const BlockShapeBoxes boxes = block_boxes(block, state);
        for (int b = 0; b < (parts ? 0 : boxes.count); ++b) {
            const BoundingBox& box = boxes.boxes[static_cast<size_t>(b)];
            if (!face_drawn(block, boxes, box, outlined)) continue;
            for (int c = 0; c < 4; ++c) {
                DrawLine3D(Vector3Add(origin, placed_point(block, state, Vector3Add(box_corner(box, outlined, c), lift))),
                           Vector3Add(origin, placed_point(block, state, Vector3Add(box_corner(box, outlined, (c + 1) % 4), lift))),
                           SELECTION);
            }
        }

        // What the crosshair aims at.
        if (show_hitbox) {
            const BlockShapeBoxes hitbox = hitbox_boxes(block, state);
            for (int b = 0; b < hitbox.count; ++b) draw_box_edges(hitbox.boxes[static_cast<size_t>(b)], origin, HITBOX_COLOR);
        }

        // The pivot its model tilts about, and the tilt's axis - over
        // everything, while they're being edited.
        if (block_panel_tab == 2 && kind_of(block) == BlockShapeKind::Torch) {
            const Vector3 pivot = Vector3Add(origin, state_model_of(block, state).pivot);
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            DrawLine3D(Vector3Subtract(pivot, {0.3f, 0.0f, 0.0f}), Vector3Add(pivot, {0.3f, 0.0f, 0.0f}), Fade(PIVOT_COLOR, 0.8f));
            DrawSphere(pivot, 0.022f, PIVOT_COLOR);
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
    }
    EndMode3D();
    EndTextureMode();
}

// ------------------------------------------------------- As in the game --

namespace {
    constexpr Color GAME_SKY = {128, 172, 222, 255};

    // Every block of the scene: opaque and cutout ones first, then
    // (`translucent_pass`) the blended ones.
    void draw_scene(const Scene& scene, const Texture2D& atlas, bool translucent_pass)
    {
        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
        for (const Scene::Cell& cell : scene.cells) {
            if (cell.block->translucent != translucent_pass) continue;
            emit_block(*cell.block, cell.state,
                       {static_cast<float>(cell.x) - 0.5f, static_cast<float>(cell.y) - 0.5f, static_cast<float>(cell.z) - 0.5f},
                       &scene, cell.x, cell.y, cell.z, cell.joined);
        }
        rlEnd();
        rlSetTexture(0);
    }

    // A titled box to draw a little preview in; returns its inside.
    Rectangle preview_box(Rectangle bounds, const std::string& title)
    {
        DrawRectangleRec(bounds, Color{24, 24, 28, 235});
        DrawRectangleLinesEx(bounds, 1.0f, gui_color(DEFAULT, BORDER_COLOR_NORMAL));
        DrawTextEx(editor_text::font(), title.c_str(), {bounds.x + 8, bounds.y + 5}, 16, 1, gui_color(DEFAULT, TEXT_COLOR_NORMAL));
        return {bounds.x + 4, bounds.y + ROW, bounds.width - 8, bounds.height - ROW - 4};
    }
}

void ModelEditor::draw_block_game_view(Rectangle bounds)
{
    const Rectangle inside = preview_box(bounds, tr("editor.block_in_game"));
    if (selected_block < 0) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const int width = std::max(1, static_cast<int>(inside.width));
    const int height = std::max(1, static_cast<int>(inside.height));
    if (block_game_texture.id == 0 || block_game_texture.texture.width != width || block_game_texture.texture.height != height) {
        if (block_game_texture.id != 0) UnloadRenderTexture(block_game_texture);
        block_game_texture = LoadRenderTexture(width, height);
    }

    // Two of it side by side on a grass floor.
    const block_file::BlockFile* floor = nullptr;
    for (const block_file::BlockFile& candidate : blocks) {
        if (candidate.name == "grass") floor = &candidate;
    }
    if (!floor) floor = &block;
    Scene scene;
    const BlockInstanceState state = display_state(block, block_state_view);
    for (int x = -3; x <= 4; ++x) {
        for (int z = -3; z <= 3; ++z) scene.cells.push_back({x, -1, z, floor, {}, 0});
    }
    // Joined into one wide block, or each with its other half.
    const std::optional<Companion> companion = companion_of(blocks, block, state, block_large_view);
    if (companion && companion->block == &block) {
        scene.cells.push_back({0, 0, 0, &block, state, companion->joined_self});
        scene.cells.push_back({1, 0, 0, &block, state, companion->joined_other});
    } else {
        for (int x = 0; x <= 1; ++x) {
            scene.cells.push_back({x, 0, 0, &block, state, 0});
            if (companion) {
                scene.cells.push_back({x + static_cast<int>(companion->offset.x), static_cast<int>(companion->offset.y),
                                       static_cast<int>(companion->offset.z), companion->block, state, 0});
            }
        }
    }
    // On a wall: a stone wall behind them to hang on.
    if (state.attachment != BlockFace::Bottom && state.attachment != BlockFace::Top) {
        const block_file::BlockFile* wall = floor;
        for (const block_file::BlockFile& candidate : blocks) {
            if (candidate.name == "stone") wall = &candidate;
        }
        for (int x = -3; x <= 4; ++x) {
            for (int y = 0; y <= 2; ++y) scene.cells.push_back({x, y, -1, wall, {}, 0});
        }
    }

    // A couple of steps away, a little above them, through the game's own
    // 60-degree field of view.
    Camera3D cam{};
    cam.position = {-0.55f, 1.05f, 2.25f};
    cam.target = {0.5f, 0.05f, 0.0f};
    if (companion && companion->offset.y != 0.0f) { // a door: two blocks tall
        cam.position = {-0.9f, 1.6f, 3.1f};
        cam.target = {0.5f, 0.55f, 0.0f};
    }
    cam.up = {0, 1, 0};
    cam.fovy = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const Texture2D& atlas = terrain_atlas();
    BeginTextureMode(block_game_texture);
    ClearBackground(GAME_SKY);
    BeginMode3D(cam);
    BeginShaderMode(entity_cutout_shader()); // the world's shader drops see-through texels too
    draw_scene(scene, atlas, false);
    rlDrawRenderBatchActive();
    rlDisableDepthMask(); // blended faces don't hide what's behind them
    draw_scene(scene, atlas, true);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndShaderMode();
    draw_particles(game_particles, cam, particle_sheet(), atlas);
    rlDrawRenderBatchActive();
    // The left one as if aimed at: the game's dark frame round its hitbox.
    if (show_hitbox) {
        const BlockShapeBoxes hitbox = hitbox_boxes(block, state);
        for (int b = 0; b < hitbox.count; ++b) draw_box_edges(hitbox.boxes[static_cast<size_t>(b)], {-0.5f, -0.5f, -0.5f}, Fade(BLACK, 0.55f));
    }
    EndMode3D();
    EndTextureMode();

    DrawTexturePro(block_game_texture.texture, {0, 0, static_cast<float>(width), -static_cast<float>(height)}, inside, {0, 0},
                   0.0f, WHITE);
}

void ModelEditor::draw_block_inventory_icon(Rectangle bounds)
{
    const Rectangle inside = preview_box(bounds, tr_format("editor.block_in_inventory", {std::to_string(game_ui_scale)}));
    if (selected_block < 0) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    // One inventory slot at the game's own size (16-pixel icons, x2, x the
    // UI scale): Minecraft's grey slot with its sunken edge.
    const float scale = 2.0f * static_cast<float>(game_ui_scale);
    const float slot = 18.0f * scale;
    const Rectangle cell = {inside.x + PAD, inside.y + (inside.height - slot) * 0.5f, slot, slot};
    DrawRectangleRec(cell, Color{139, 139, 139, 255});
    DrawRectangleRec({cell.x, cell.y, cell.width, scale}, Color{55, 55, 55, 255});
    DrawRectangleRec({cell.x, cell.y, scale, cell.height}, Color{55, 55, 55, 255});
    DrawRectangleRec({cell.x, cell.y + cell.height - scale, cell.width, scale}, WHITE);
    DrawRectangleRec({cell.x + cell.width - scale, cell.y, scale, cell.height}, WHITE);

    // The half of a two-cell block that isn't an item never shows up there.
    if (is_pair_kind(kind_of(block)) && !block.item) {
        draw_hint(cell.x + cell.width + PAD, cell.y, inside.width - cell.width - PAD * 2,
                  tr_format("editor.block_not_item", {display_name(block.partner)}), false);
        return;
    }
    const Texture2D& atlas = terrain_atlas();
    auto face = [&](int f) {
        const block_file::Face& source = block.faces[f];
        return CubeIconFace{{source.tile_x / 16.0f, source.tile_y / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f}, source.tint};
    };
    const Rectangle icon = {cell.x + scale, cell.y + scale, 16.0f * scale, 16.0f * scale};
    if (block.item_sprite_x >= 0) {
        // A flat sprite from sprites/items.png, as the game shows a torch.
        DrawTexturePro(items_atlas(), {block.item_sprite_x * 16.0f, block.item_sprite_y * 16.0f, 16.0f, 16.0f}, icon, {0, 0}, 0.0f,
                       WHITE);
    } else if (kind_of(block) == BlockShapeKind::Cross) {
        // Its north tile, flat - as the game's inventory shows a plant.
        const block_file::Face& side = block.faces[2];
        const Rectangle flat = {icon.x + icon.width * 0.16f, icon.y + icon.height * 0.04f, icon.width * 0.68f, icon.height * 0.92f};
        DrawTexturePro(atlas, tile_source(side.tile_x, side.tile_y), flat, {0, 0}, 0.0f, side.tint);
    } else if (kind_of(block) != BlockShapeKind::Cube) {
        draw_shaped_icon(icon, atlas, item_shape_of_kind(kind_of(block)), face(0), face(3), face(4));
    } else {
        draw_cube_icon(icon, atlas, face(0), face(3), face(4), block.side_inset / 16.0f);
    }
    label({cell.x + cell.width + PAD, inside.y, inside.width - cell.width - PAD * 2, inside.height}, display_name(block.name));
}
