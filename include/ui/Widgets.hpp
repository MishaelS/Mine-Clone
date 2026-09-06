#pragma once

#include "raylib.h"
#include "core/Block.hpp"

#include <cstddef>
#include <string>

// Minimal hand-rolled immediate-mode widget set for the menu screens - no
// GUI library is linked in this project (raygui.h exists only, unused,
// inside external/raylib/examples/), so every screen (MainMenuScreen,
// WorldListScreen, WorldCreateScreen, SettingsScreen) builds its layout out
// of these. No widget IDs or persistent hover/focus state here - each call
// recomputes hover purely from the current mouse position, and a caller
// that needs focus (text_input) tracks which field is focused itself and
// passes it in.
namespace ui {
    void panel(Rectangle bounds, Color color);

    // Centered text within `bounds` - doesn't draw a background of its own.
    void label(Rectangle bounds, const std::string& text, int font_size = 22, Color color = WHITE);

    // Filled rect + centered text. `selected` highlights one of a pair
    // (e.g. Creative/Survival, Point/Bilinear) drawn as two adjacent
    // button() calls rather than a separate segmented-control widget.
    // `enabled = false` grays it out and never returns true, however the
    // mouse moves - used for the still-unimplemented "Сетевая игра".
    // Returns true on the exact frame it's clicked.
    bool button(Rectangle bounds, const std::string& text, bool selected = false, bool enabled = true);

    struct TextInputState {
        std::string text;             // UTF-8
        size_t max_codepoints = 32;   // counted in codepoints, not bytes - a Cyrillic name is 2 bytes/letter
    };

    // While `focused`, appends this frame's typed characters (GetCharPressed(),
    // UTF-8-encoded) to state.text, and Backspace (incl. held-key repeat)
    // removes exactly one trailing UTF-8 codepoint (never leaves a
    // dangling continuation byte from a multi-byte Cyrillic character).
    // Draws the field's box/border and current text/cursor regardless of
    // focus. Returns true if the box itself was clicked this frame - the
    // caller updates its own idea of which field is focused from that;
    // this function doesn't track focus across calls.
    bool text_input(Rectangle bounds, TextInputState& state, bool focused);

    // A labeled horizontal drag slider over `bounds` (the whole row - label
    // and track are both laid out inside it). Integer-snapped. Tracks
    // while the mouse button is held anywhere within `bounds`, not just on
    // the handle itself, so a slider is easy to grab. Returns true only on
    // a frame `value` actually changed.
    bool slider_int(Rectangle bounds, const std::string& label_text, int& value, int min_value, int max_value);

    // Draws `type`'s Top-face texture (BlockProperties::texture_uvs[0],
    // tinted by texture_tints[0]) stretched to fill `bounds` - a flat 2D
    // icon, not the 3-face isometric render Minecraft's own inventory
    // uses, in keeping with this whole toolkit staying simple.
    void block_icon(Rectangle bounds, BlockType type);

    // block_icon() plus button()'s own hover/selected/bordered chrome -
    // used by both InventoryHud's hotbar slots and its picker grid so they
    // share one look. Returns true on the frame it's clicked.
    bool block_button(Rectangle bounds, BlockType type, bool selected = false);
}
