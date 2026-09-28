#pragma once

#include "content/BlockFile.hpp"

#include "raylib.h"

#include <vector>

// Blocks drawn the way the game meshes them - the Blocks tab's own drawing
// (BlockTab.cpp), shared with the Structures tab.
namespace block_draw {
    // One block, its cell centered on (x, y, z).
    struct Cell {
        int x, y, z;
        const block_file::BlockFile* block;
    };

    // Every cell as the game shows them standing together: faces between
    // two of them culled, corners shaded (AO), see-through texels dropped,
    // blended blocks last. Inside BeginMode3D().
    void draw_cells(const std::vector<Cell>& cells, const Texture2D& atlas);

    // A small flat icon of it: its item sprite, a plant's tile, else its top.
    void draw_icon(Rectangle bounds, const block_file::BlockFile& block, const Texture2D& terrain, const Texture2D& items);
}
