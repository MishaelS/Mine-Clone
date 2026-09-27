#pragma once

#include "raylib.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// A full-cube block described in a file of its own, assets/blocks/<name>.json
// - what the model editor's "Blocks" tab edits and the game registers at
// startup (see content::register_block_files()). Blocks with anything
// beyond a plain textured cube (fluids, plants, torches, stairs, doors...)
// stay in src/content/Blocks.cpp.
//
// Only needs the JSON reader and raylib's Color - the editor uses it
// without the game's block registry.
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

    // Tiles per row/column of sprites/terrain.png (16px each).
    constexpr int ATLAS_TILES = 16;

    struct Face {
        int tile_x = 0; // column/row in sprites/terrain.png
        int tile_y = 0;
        Color tint = WHITE; // multiplied into the tile - alpha is opacity for translucent blocks
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
        int side_inset = 0; // side faces drawn this many texture pixels in (cactus)

        std::array<Face, 6> faces{};
    };

    // assets/blocks/
    std::string directory();

    std::optional<BlockFile> load(const std::string& path);
    bool save(const BlockFile& block, const std::string& path);
    // Every *.json in directory(), by id. Unreadable files are skipped with a
    // warning.
    std::vector<BlockFile> load_all();

} // namespace block_file
