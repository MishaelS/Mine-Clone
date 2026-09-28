#pragma once

#include "raylib.h"

#include <set>
#include <string>

// Small drawing helpers every tab of the editor shares.
namespace editor_ui {

    // A few lines of small grey (or red, when something's wrong) text,
    // wrapped to `width`; returns the y under them.
    float draw_hint(float x, float y, float width, const std::string& text, bool error);

    // The game's own name for a block ("block.<name>" in the translations),
    // or its id.
    std::string block_display_name(const std::string& name);

    // A section's title bar that folds it: a click flips `title` in and
    // out of `folded`. Draws at y and moves y under it; true while the
    // section is open (its contents should be drawn). Clicks only count
    // while the GUI isn't locked (the mouse is over the panel).
    bool fold_header(float x, float& y, float width, const std::string& title, std::set<std::string>& folded);

} // namespace editor_ui
