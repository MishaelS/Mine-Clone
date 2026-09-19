#pragma once

#include "core/BlockShape.hpp"

#include "raylib.h"
#include "core/Block.hpp"
#include "player/Item.hpp"

#include <cstddef>
#include <functional>
#include <string>

// Widget set for the menu screens - every screen (MainMenuScreen,
// WorldListScreen, WorldCreateScreen, SettingsScreen) builds its layout out
// of these. Callers never see it, but internally button()/text_input()/
// slider_int() are backed by raygui (external/raygui) rather than
// hand-rolled hit-testing: it owns click-edge/disabled-gating/keyboard
// focus/drag-outside-bounds capture, while button() still draws its own
// Minecraft nine-patch texture on top (raygui has no notion of that art
// style) and text_input()/slider_int() get a restyled-to-match-the-theme
// version of raygui's own look. See src/ui/Widgets.cpp. No widget IDs or
// persistent hover state beyond that here - each call recomputes hover
// purely from the current mouse position, and a caller that needs focus
// (text_input) tracks which field is focused itself and passes it in.
namespace ui {
    // One scale shared by every menu widget and in-game HUD. Levels mirror
    // Minecraft's GUI scale option: 1 standard, 2 medium, 3 large, 4 huge.
    void set_scale_level(int level);
    int scale_level();
    float scale_factor();
    float scaled(float value);
    int scaled_font(int value);
    int text_size();
    constexpr float BUTTON_HEIGHT = 40.0f;
    constexpr float BUTTON_GAP    = 8.0f;
    constexpr float MENU_WIDTH    = 640.0f;
    enum class TextAlign { Center, Left };
    void begin_frame();

    enum class SoundEvent { Click, Hover };
    using SoundCallback = std::function<void(SoundEvent)>;
    void set_sound_callback(SoundCallback callback);

    // Deferred overlay kept in the widget module because it is reusable UI
    // chrome, not a standalone screen. Draw after the hovered content so it
    // always remains on top.
    class Tooltip {
    public:
        void clear();
        void show(const std::string& text, Vector2 anchor);
        void draw() const;

    private:
        std::string text;
        Vector2 anchor = {0.0f, 0.0f};
        bool visible = false;
    };

    void panel(Rectangle bounds, Color color);

    // Dark tiled dirt for out-of-game menus. In-game menus use a blurred
    // snapshot of the world instead.
    void menu_background();
    // Capture the current completed draw batch; caller owns the texture.
    Texture2D capture_blurred_background();

    // Live alternative to capture_blurred_background(), for backgrounds
    // that should keep moving (the world behind an open inventory):
    // everything drawn between these two calls goes into a half-resolution
    // off-screen target, which end_...() then draws to the screen through a
    // small GPU blur (assets/shaders/blur.fs) - cheap enough every frame.
    // Call inside BeginDrawing(), never nested.
    void begin_blurred_background();
    void end_blurred_background();

    // Minecraft-style inverted crosshair, centered in the current window.
    void crosshair();

    // World-space selection chrome for the currently targeted voxel.
    void block_outline(const BlockShapeBoxes& shape);

    // Alpha-blended crack overlay on the block currently being broken -
    // terrain.png's own "block breaking" strip (row 15, 10 stages), picked
    // by `progress` (0..1, GameEngine's own breaking_progress) the same
    // way real Minecraft steps through its crack stages as a hold-to-break
    // approaches completion.
    void block_breaking_overlay(const BlockShapeBoxes& shape, float progress);

    // Centered text within `bounds` - doesn't draw a background of its own.
    // One font size for all chrome. Long labels are elided, never shrunk.
    // Text fields scroll horizontally at exactly the same font size.
    void label(Rectangle bounds, const std::string& text, Color color = WHITE,
               TextAlign align = TextAlign::Center);

    // Minecraft-textured button with centered text. `selected` uses the
    // highlighted texture for one of a pair (e.g. Creative/Survival,
    // Point/Bilinear) drawn as two adjacent button() calls rather than a
    // separate segmented-control widget.
    // `enabled = false` grays it out and never returns true, however the
    // mouse moves - used for the still-unimplemented "Сетевая игра".
    // Returns true once, the frame the button is released (raygui's own
    // click-edge convention - a drag-off-then-release cancels the click,
    // unlike a plain press check).
    bool button(Rectangle bounds, const std::string& text, bool selected = false, bool enabled = true);

    struct TextInputState {
        std::string text;             // UTF-8
        size_t max_codepoints = 32;   // counted in codepoints, not bytes - a Cyrillic name is 2 bytes/letter
    };

    // While `focused` (raygui's "edit mode"), edits state.text in place:
    // UTF-8 typing, backspace/delete (incl. held-key repeat and word-skip
    // with Ctrl), arrow-key/click-to-position caret, and Ctrl+V paste, all
    // handled by raygui's GuiTextBox. Draws the field's box/border and
    // current text/caret regardless of focus. Returns true if the box
    // itself was clicked this frame - the caller updates its own idea of
    // which field is focused from that; this function doesn't track focus
    // across calls. state.max_codepoints is still enforced afterwards
    // (raygui only knows about its scratch buffer's byte size, not the
    // caller's codepoint budget - a Cyrillic name is 2 bytes/letter).
    bool text_input(Rectangle bounds, TextInputState& state, bool focused);

    // Full button-height slider with its caption centered on the track,
    // restyled to the game's dark theme rather than Minecraft's own pixel
    // art (no bespoke slider texture exists for raygui to skin with).
    // Backed by raygui's GuiSlider, which already captures the pointer
    // until release even outside its bounds.
    bool slider_int(Rectangle bounds, const std::string& label_text, int& value, int min_value, int max_value);

    // Draws `type` as a three-face isometric block item using its top and
    // side atlas textures. The visible sides receive different brightness
    // levels to reproduce Minecraft's inventory-item depth.
    void block_icon(Rectangle bounds, BlockType type);

    // block_icon() plus button()'s own hover/selected/bordered chrome -
    // used by both InventoryHud's hotbar slots and its picker grid so they
    // share one look. Returns true on the frame it's clicked.
    bool block_button(Rectangle bounds, BlockType type, bool selected = false);

    // A tool's flat 2D icon, sampled straight from the item atlas - unlike
    // block_icon()'s isometric cube render, items are plain flat sprites in
    // Minecraft's own inventory too.
    void item_icon(Rectangle bounds, ItemType type);
}
