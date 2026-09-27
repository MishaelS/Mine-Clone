#pragma once

#include "core/Block.hpp"

#include "raylib.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// A block described in a file of its own, assets/blocks/<name>.json - a full
// cube or one of the single-cell shapes (slab, stairs, trapdoor, cake,
// torch). What the model editor's "Blocks" tab edits and the game registers
// at startup (see content::register_block_files()). Fluids, plants, doors and
// beds stay in src/content/Blocks.cpp for now.
//
// Only needs the JSON reader, raylib and the pure shape geometry
// (core/BlockShapeKinds.cpp) - the editor uses it without the game's block
// registry.
namespace block_file {

    // A cube's six faces, in BlockFace order - also the keys under "faces".
    constexpr const char* FACE_IDS[6] = {"top", "bottom", "north", "south", "east", "west"};
    // BlockSoundGroup, in its own order.
    constexpr const char* SOUND_IDS[] = {"none", "grass", "dirt", "gravel", "stone", "wood",
                                        "sand", "snow", "glass", "cloth", "foliage", "metal"};
    constexpr int SOUND_COUNT = static_cast<int>(sizeof(SOUND_IDS) / sizeof(SOUND_IDS[0]));
    // ToolKind, in its own order.
    constexpr const char* TOOL_IDS[] = {"none", "sword", "pickaxe", "shovel", "axe", "hoe"};
    constexpr int TOOL_COUNT = static_cast<int>(sizeof(TOOL_IDS) / sizeof(TOOL_IDS[0]));

    // BlockShapeKind, in its own order - the shapes a block file can take
    // (doors and beds stay in code for now).
    constexpr const char* SHAPE_IDS[] = {"cube", "slab", "stairs", "trapdoor", "cake", "torch", "door", "bed"};
    constexpr int SHAPE_COUNT = static_cast<int>(sizeof(SHAPE_IDS) / sizeof(SHAPE_IDS[0]));
    constexpr int FILE_SHAPE_COUNT = 6; // the first SHAPE_IDS the editor offers

    // ElementNormal, in its own order.
    constexpr const char* NORMAL_IDS[] = {"out", "in", "both"};
    constexpr int NORMAL_COUNT = 3;

    // Tiles per row/column of sprites/terrain.png (16px each).
    constexpr int ATLAS_TILES = 16;

    struct Face {
        int tile_x = 0; // column/row in sprites/terrain.png
        int tile_y = 0;
        Color tint = WHITE; // multiplied into the tile - alpha is opacity for translucent blocks
    };

    // Its hitbox and model placement in one state (BlockStateModel), in
    // texture pixels (16 = one block), laid out for the north wall.
    struct StateModel {
        bool has_hitbox = false;
        Vector3 hitbox_from{0, 0, 0};
        Vector3 hitbox_to{16, 16, 16};
        Vector3 offset{0, 0, 0};
        Vector3 pivot{8, 0, 8};
        float angle = 0.0f; // degrees, top leaning out of the wall
    };

    // One face of a model part (BlockElementFace), in texture pixels.
    struct ElementFace {
        bool enabled = true;
        bool auto_uv = true;                // the part of the tile the face's own size covers, as the game crops
        std::array<float, 4> uv{0, 0, 16, 16}; // else this: u1, v1, u2, v2 within the side's tile
        int normal = 0;                     // index into NORMAL_IDS
    };

    // One part of the block's own model (BlockElement), in texture pixels.
    struct Element {
        std::string name;
        Vector3 from{0, 0, 0};
        Vector3 to{16, 16, 16};
        bool shade = true;
        std::array<ElementFace, 6> faces{};
    };

    struct BlockFile {
        int id = 0;       // BlockType value: stable - chunks are saved by it
        std::string name; // stable id: saves, translations ("block.<name>"), commands
        int sound = 4;    // index into SOUND_IDS (stone)
        float hardness = 1.5f; // seconds to break by hand
        int tool = 2;          // index into TOOL_IDS - the tool that breaks it faster
        float density = 2.7f;  // relative to water: dropped items sink or float
        int luminance = 0;     // 0-15 light it gives off

        bool solid            = true;   // collides and hides neighbors' faces
        bool selectable       = true;   // can be aimed at and broken
        bool replaceable      = false;  // placing a block may replace it
        bool transparent      = false;  // lets light and neighbors' faces through
        bool translucent      = false;  // alpha-blended pass (ice)
        bool cutout           = false;  // alpha-tested texture (leaves, glass)
        bool keep_same_faces  = false;  // draws the face between two of itself (leaves)
        bool damages_on_touch = false;
        bool directional      = false; // its south face is a front that turns to face whoever placed it

        int shape = 0;            // index into SHAPE_IDS - its shape and behavior (BlockShapeKind)
        std::string double_block; // a slab's two halves become this block (by name)
        bool has_cut = false;     // a cake's cross-section tile, shown once bitten
        int cut_x = 0, cut_y = 0;
        int item_sprite_x = -1;   // a flat sprites/items.png icon ({column, row}) instead of a 3D one; -1 none
        int item_sprite_y = -1;
        int side_inset = 0; // side faces drawn this many texture pixels in (cactus)

        std::array<Face, 6> faces{};
        // Per state of its shape (shape_state_count()): a torch's "floor"
        // and "wall", anything else's one "default".
        std::array<StateModel, MAX_BLOCK_STATES> states{};
        // Its own model from parts - a cube or a torch may have one
        // (elements_allowed()); none: its shape's own look.
        std::vector<Element> elements;
    };

    // Whether a block of `shape` can be drawn from parts: a cube or a torch
    // (the other shapes' boxes change with their state).
    bool elements_allowed(int shape);
    // The parts a `shape` starts with - a torch's vanilla model (its two
    // crossed pairs of planes and the flame's cap); none for anything else.
    std::vector<Element> default_elements(int shape);
    // A face's UV in pixels: its own, or (auto) what its size covers.
    std::array<float, 4> face_uv(const Element& element, int face);
    // In the game's units.
    std::vector<BlockElement> to_elements(const std::vector<Element>& elements);

    // The key a state is saved under (a torch: "floor", "wall").
    const char* state_id(int shape, int state);
    // What a state of `shape` starts as - a torch's vanilla-like hitboxes
    // and its lean out of a wall; nothing for anything else.
    StateModel default_state(int shape, int state);
    // In the game's units (cell 0..1).
    BlockStateModel to_state_model(const StateModel& state);

    // assets/blocks/
    std::string directory();

    std::optional<BlockFile> load(const std::string& path);
    bool save(const BlockFile& block, const std::string& path);
    // Every *.json in directory(), by id. Unreadable files are skipped with a
    // warning.
    std::vector<BlockFile> load_all();

} // namespace block_file
