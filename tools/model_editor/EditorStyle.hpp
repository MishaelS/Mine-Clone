#pragma once

#include "raylib.h"

// Sizes and colors every part of the editor shares - the entity tab
// (ModelEditor.cpp), the block tab (BlockTab.cpp) and the structure tab
// (StructureTab.cpp) alike.
namespace editor_style {
    constexpr float TABS_HEIGHT    = 32.0f; // the Entities / Blocks / Structures tab strip, top left
    constexpr float TOP_BAR_HEIGHT = 40.0f; // the current tab's own bar under it
    constexpr float HEADER_HEIGHT  = TABS_HEIGHT + TOP_BAR_HEIGHT;
    constexpr float PANEL_HEADER   = 28.0f;
    constexpr float ROW            = 26.0f; // one control row
    constexpr float GAP            = 6.0f;
    constexpr float PAD            = 10.0f;

    constexpr Color VIEWPORT_BACKGROUND = {  8,  48,  52, 255};
    constexpr Color SELECTION           = {255, 160,  20, 255};
}
