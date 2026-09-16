#include "ui/Widgets.hpp"
#include "ui/FontManager.hpp"
#include "core/TextureManager.hpp"
#include "rendering/BlockMesh.hpp"

#include "rlgl.h"
// Declarations only here - RAYGUI_IMPLEMENTATION is compiled once, in
// RayGuiImpl.cpp. Backs button()/text_input()/slider_int() below.
#include "raygui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
    // Own name, not raygui's TEXT_SPACING style property (GuiDefaultProperty)
    // - both are in scope here since this file includes raygui.h.
    constexpr float LABEL_SPACING = 1.0f;
    constexpr int BASE_TEXT_SIZE = 16;
    constexpr float BORDER_THICKNESS = 2.0f;

    const char* BUTTON_TEXTURE_PATH       = "sprites/gui/widgets/widget1.png";
    const char* BUTTON_HOVER_TEXTURE_PATH = "sprites/gui/widgets/widget2.png";
    // Same 200x20 strip layout as widget1/2.png above, so they share one
    // nine-patch border size - see draw_nine_patch_texture().
    const char* TEXTFIELD_TEXTURE_PATH = "sprites/gui/widgets/widget0.png";
    constexpr float NINE_PATCH_BORDER = 3.0f;

    const char* OPTIONS_BACKGROUND_TEXTURE_PATH = "sprites/gui/optionsBackground.png";
    constexpr float OPTIONS_BACKGROUND_TILE_SCALE = 4.0f; // on-screen size of each 16px source tile

    // block_button()'s own flat chrome - unrelated to the widget0-2
    // textures above, kept as-is (no texture asset covers this slot look).
    constexpr Color BUTTON_FILL          = {60, 60, 68, 255};
    constexpr Color BUTTON_FILL_HOVER    = {82, 82, 92, 255};
    constexpr Color BUTTON_FILL_SELECTED = {88, 138, 88, 255};
    constexpr Color BUTTON_BORDER        = {20, 20, 24, 255};
    constexpr Color TEXT_DISABLED        = {140, 140, 140, 255};

    constexpr float TOOLTIP_PADDING       = 6.0f;
    constexpr float TOOLTIP_CURSOR_OFFSET = 14.0f;
    constexpr float TOOLTIP_SCREEN_MARGIN = 4.0f;
    constexpr Color TOOLTIP_BACKGROUND = {20, 20, 24, 235};
    constexpr float CROSSHAIR_ARM_LENGTH = 10.0f;
    constexpr float CROSSHAIR_THICKNESS  = 2.0f;
    constexpr unsigned char CROSSHAIR_INTENSITY = 235;
    constexpr float TARGET_OUTLINE_SIZE = 1.002f;
    constexpr Color TARGET_OUTLINE_COLOR = {0, 0, 0, 200};

    Color shade(Color color, float brightness) {
        return {
            static_cast<unsigned char>(color.r * brightness),
            static_cast<unsigned char>(color.g * brightness),
            static_cast<unsigned char>(color.b * brightness),
            color.a,
        };
    }

    void draw_atlas_quad(Rectangle uv, const Vector2 (&vertices)[4], Color tint) {
        uv = get_sample_safe_block_uv(uv);
        float u[] = {uv.x, uv.x + uv.width, uv.x + uv.width, uv.x};
        float v[] = {uv.y, uv.y, uv.y + uv.height, uv.y + uv.height};
        // Input order is texture-space TL, TR, BR, BL. raylib's 2D quads
        // use TL, BL, BR, TR so they remain front-facing with the default
        // back-face culling state left enabled after the world pass.
        constexpr int DRAW_ORDER[] = {0, 3, 2, 1};

        rlColor4ub(tint.r, tint.g, tint.b, tint.a);
        rlNormal3f(0.0f, 0.0f, 1.0f);
        for (int i : DRAW_ORDER) {
            rlTexCoord2f(u[i], v[i]);
            rlVertex2f(vertices[i].x, vertices[i].y);
        }
    }

    // The source textures (buttons, text field) are only 200x20, while the
    // menu uses several different widths for each. Draw them as a
    // nine-patch so their pixel-art corners keep the correct proportions
    // and only the center stretches.
    void draw_nine_patch_texture(Rectangle bounds, const char* texture_path, Color tint = WHITE) {
        const Texture2D& texture = TextureManager::get(texture_path);
        float source_border = NINE_PATCH_BORDER;
        float scale = bounds.height / static_cast<float>(texture.height);
        float destination_border = std::min(source_border * scale, bounds.width / 2.0f);

        float source_x[] = {0.0f, source_border, static_cast<float>(texture.width) - source_border};
        float source_y[] = {0.0f, source_border, static_cast<float>(texture.height) - source_border};
        float source_w[] = {
            source_border,
            static_cast<float>(texture.width) - source_border * 2.0f,
            source_border,
        };
        float source_h[] = {
            source_border,
            static_cast<float>(texture.height) - source_border * 2.0f,
            source_border,
        };
        float destination_x[] = {
            bounds.x,
            bounds.x + destination_border,
            bounds.x + bounds.width - destination_border,
        };
        float destination_y[] = {
            bounds.y,
            bounds.y + destination_border,
            bounds.y + bounds.height - destination_border,
        };
        float destination_w[] = {
            destination_border,
            bounds.width - destination_border * 2.0f,
            destination_border,
        };
        float destination_h[] = {
            destination_border,
            bounds.height - destination_border * 2.0f,
            destination_border,
        };

        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                DrawTexturePro(
                    texture,
                    {source_x[column], source_y[row], source_w[column], source_h[row]},
                    {destination_x[column], destination_y[row], destination_w[column], destination_h[row]},
                    {0.0f, 0.0f}, 0.0f, tint);
            }
        }
    }

    // Walks back from the end of a UTF-8 string to the start of its last
    // codepoint (a lead byte's top bits are never 10xxxxxx - only
    // continuation bytes are), then erases from there - removes one whole
    // multi-byte character (e.g. Cyrillic, 2 bytes/letter) in one step
    // instead of leaving a dangling continuation byte behind.
    void utf8_pop_back(std::string& text) {
        if (text.empty()) return;
        size_t pos = text.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
        text.erase(pos);
    }

    size_t utf8_length(const std::string& text) {
        size_t count = 0;
        for (unsigned char c : text) {
            if ((c & 0xC0) != 0x80) ++count; // count lead bytes only, not continuation bytes
        }
        return count;
    }

    int current_scale_level = 1;
    ui::SoundCallback sound_callback;
    std::string hovered_button_id;
    bool hovered_last_frame = false;

    // One-time raygui setup, lazily run from begin_frame()'s first call
    // (needs FontManager's font already loaded, which GameEngine::init()
    // guarantees happens before the first frame).
    void init_raygui_style() {
        static bool ready = false;
        if (ready) return;
        ready = true;

        GuiLoadStyleDefault();
        GuiSetFont(FontManager::get());

        // button() below draws its own Minecraft nine-patch texture and
        // shadowed label() text every call - raygui contributes nothing
        // visual to it, only click-edge/disabled-state via GuiButton(), so
        // every part of its style that could draw something is switched
        // off here, once, rather than pushed/popped per call.
        GuiSetStyle(BUTTON, BORDER_WIDTH, 0);
        for (int state = 0; state < 4; ++state) {
            GuiSetStyle(BUTTON, BASE_COLOR_NORMAL + state * 3, ColorToInt(BLANK));
            GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL + state * 3, ColorToInt(BLANK));
        }

        // Text fields and sliders never had bespoke pixel-art assets (the
        // old hand-rolled text_input() drew a flat rect too) - restyled to
        // the game's dark theme instead of Minecraft's own look, and
        // otherwise left to raygui's own drawing.
        GuiSetStyle(TEXTBOX, BASE_COLOR_NORMAL   , ColorToInt(BLACK));
        GuiSetStyle(TEXTBOX, BASE_COLOR_FOCUSED  , ColorToInt(BLACK));
        GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED  , ColorToInt(BLACK));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_NORMAL , ColorToInt(Color{160, 160, 160, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, ColorToInt(WHITE));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_PRESSED, ColorToInt(WHITE));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL   , ColorToInt(WHITE));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED  , ColorToInt(WHITE));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED  , ColorToInt(WHITE));
        GuiSetStyle(TEXTBOX, TEXT_ALIGNMENT      , TEXT_ALIGN_LEFT);

        GuiSetStyle(SLIDER, BORDER_COLOR_NORMAL, ColorToInt(Color{160, 160, 160, 255}));
        GuiSetStyle(SLIDER, BASE_COLOR_NORMAL  , ColorToInt(Color{20, 20, 24, 220}));
        GuiSetStyle(SLIDER, TEXT_COLOR_NORMAL  , ColorToInt(Color{225, 225, 230, 255}));
        GuiSetStyle(SLIDER, TEXT_COLOR_FOCUSED , ColorToInt(Color{255, 255, 160, 255}));
        GuiSetStyle(SLIDER, TEXT_COLOR_PRESSED , ColorToInt(Color{255, 255, 160, 255}));
    }

    float level_factor(int level) {
        // Level 1 is "Стандарт" - the UI's original, unscaled pixel size -
        // per spec ("1 - стандартный размер в оригинальном размере"), so it
        // must map to exactly 1.0, not shrink everything by default. Every
        // level is a whole multiplier (matching real Minecraft's own GUI
        // Scale: 1/2/3/4, not fractional in-between steps) - anything else
        // makes pixel art (hearts, hotbar, nine-patch buttons - every
        // pixel-snapped draw across the whole UI multiplies by this same
        // factor) round its edges unevenly per-axis and look crooked, the
        // same bug HOTBAR_SCALE had at 2.5x in InventoryHud.cpp.
        constexpr float FACTORS[4] = {0.5f, 1.0f, 1.5f, 2.0f};
        return FACTORS[std::clamp(level, 1, 4) - 1];
    }

    std::string elide(const Font& font, std::string text, float width) {
        const float size = static_cast<float>(ui::text_size());
        if (MeasureTextEx(font, text.c_str(), size, ui::scaled(LABEL_SPACING)).x <= width) return text;
        while (!text.empty() &&
               MeasureTextEx(font, (text + "...").c_str(), size, ui::scaled(LABEL_SPACING)).x > width) {
            utf8_pop_back(text);
        }
        return text.empty() ? "" : text + "...";
    }
}

namespace ui {
    void set_scale_level(int level) {
        current_scale_level = std::clamp(level, 1, 4);
    }

    int scale_level() { return current_scale_level; }
    float scale_factor() {
        // Every level is exactly its own whole multiplier (1/2/3/4), always -
        // no window-size ceiling here anymore. That ceiling used to silently
        // cap Large/Huge down to whatever a generic reference canvas allowed,
        // which on an ordinary widescreen window made them indistinguishable
        // from Medium. The one screen dense enough to actually overlap itself
        // at x3/x4 (SettingsScreen's Section::Controls keybinding grid) now
        // scrolls instead - see SettingsScreen.cpp - so nothing here needs to
        // shrink the whole UI to protect it.
        return level_factor(current_scale_level);
    }
    float scaled(float value) { return value * scale_factor(); }
    int scaled_font(int value) { return std::max(1, static_cast<int>(std::lround(value * scale_factor()))); }
    int text_size() { return scaled_font(BASE_TEXT_SIZE); }
    void begin_frame() {
        if (!hovered_last_frame) hovered_button_id.clear();
        hovered_last_frame = false;

        // raygui's own text-drawing controls (text_input(), slider_int()) need
        // their font size/spacing kept in step with the current UI scale level,
        // which can change frame-to-frame (Settings' UI-scale button, or the
        // window being resized) - style properties are plain globals, so this
        // is cheap to just re-set every frame rather than track a "did it
        // change" flag.
        init_raygui_style();
        GuiSetStyle(DEFAULT, TEXT_SIZE, text_size());
        GuiSetStyle(DEFAULT, TEXT_SPACING, std::max(1, static_cast<int>(std::lround(scaled(LABEL_SPACING)))));
    }
    void set_sound_callback(SoundCallback callback) { sound_callback = std::move(callback); }

    void Tooltip::clear() {
        visible = false;
        text.clear();
    }

    void Tooltip::show(const std::string& new_text, Vector2 new_anchor) {
        text = new_text;
        anchor = new_anchor;
        visible = !text.empty();
    }

    void Tooltip::draw() const {
        if (!visible) return;

        const Font& font = FontManager::get();
        const float font_size = static_cast<float>(ui::text_size());
        const float padding = scaled(TOOLTIP_PADDING);
        Vector2 text_size = MeasureTextEx(font, text.c_str(), font_size, scaled(LABEL_SPACING));
        Rectangle bounds = {
            anchor.x + scaled(TOOLTIP_CURSOR_OFFSET),
            anchor.y + scaled(TOOLTIP_CURSOR_OFFSET),
            text_size.x + padding * 2.0f,
            text_size.y + padding * 2.0f,
        };
        bounds.x = std::clamp(bounds.x, TOOLTIP_SCREEN_MARGIN,
            std::max(TOOLTIP_SCREEN_MARGIN, GetScreenWidth() - bounds.width - TOOLTIP_SCREEN_MARGIN));
        bounds.y = std::clamp(bounds.y, TOOLTIP_SCREEN_MARGIN,
            std::max(TOOLTIP_SCREEN_MARGIN, GetScreenHeight() - bounds.height - TOOLTIP_SCREEN_MARGIN));

        panel(bounds, TOOLTIP_BACKGROUND);
        label(bounds, text);
    }

    void panel(Rectangle bounds, Color color) {
        DrawRectangleRec(bounds, color);
    }

    void menu_background() {
        // TextureManager defaults every texture to clamp-to-edge (right for the
        // block atlas/every other sprite so far) - this is the one texture
        // that needs to actually repeat, so it's switched to wrap mode here
        // rather than changing that shared default. Per-texture GL state keyed
        // by the id, so this never touches any other texture's sampling.
        const Texture2D& texture = TextureManager::get(OPTIONS_BACKGROUND_TEXTURE_PATH);
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);

        float screen_w = static_cast<float>(GetScreenWidth());
        float screen_h = static_cast<float>(GetScreenHeight());
        // A source rectangle bigger than the 16x16 texture samples past its
        // edge, which wrap mode above turns into repeats instead of clamping -
        // one draw call tiles the whole screen.
        Rectangle source = {0.0f, 0.0f, screen_w / scaled(OPTIONS_BACKGROUND_TILE_SCALE), screen_h / scaled(OPTIONS_BACKGROUND_TILE_SCALE)};
        Rectangle destination = {0.0f, 0.0f, screen_w, screen_h};
        DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, Color{65, 65, 65, 255});
    }

    Texture2D capture_blurred_background() {
        // Read the back buffer before EndDrawing swaps it. Reading after the
        // swap is undefined on some window systems and can capture an old menu.
        rlDrawRenderBatchActive();
        Image frame = LoadImageFromScreen();
        if (!IsImageValid(frame)) return {};
        ImageResize(&frame, std::max(1, frame.width / 2), std::max(1, frame.height / 2));
        ImageBlurGaussian(&frame, 1);
        Texture2D result = LoadTextureFromImage(frame);
        UnloadImage(frame);
        if (IsTextureValid(result)) SetTextureFilter(result, TEXTURE_FILTER_BILINEAR);
        return result;
    }

    void crosshair() {
        Color color = {CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, 255};
        float center_x = GetScreenWidth() / 2.0f;
        float center_y = GetScreenHeight() / 2.0f;

        rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        const float arm = scaled(CROSSHAIR_ARM_LENGTH);
        const float thickness = std::max(1.0f, scaled(CROSSHAIR_THICKNESS));
        DrawRectangle(static_cast<int>(center_x - arm), static_cast<int>(center_y - thickness / 2.0f),
                    static_cast<int>(arm * 2.0f), static_cast<int>(thickness), color);
        DrawRectangle(static_cast<int>(center_x - thickness / 2.0f), static_cast<int>(center_y - arm),
                    static_cast<int>(thickness), static_cast<int>(arm * 2.0f), color);
        EndBlendMode();
    }

    void block_outline(int block_x, int block_y, int block_z) {
        Vector3 center = {block_x + 0.5f, block_y + 0.5f, block_z + 0.5f};
        DrawCubeWires(center, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE,
                    TARGET_OUTLINE_SIZE, TARGET_OUTLINE_COLOR);
    }

    void block_breaking_overlay(int block_x, int block_y, int block_z, float progress) {
        constexpr int STAGE_COUNT = 10; // terrain.png row 15, columns 0-9
        constexpr int STAGE_ROW = 15;
        // Half-transparent: alpha-blended over the block already drawn beneath
        // it, so the crack pattern reads as tinted by the block's own color
        // instead of a flat gray/white overlay stamped on top of it.
        constexpr unsigned char OVERLAY_ALPHA = 128;
        int stage = std::clamp(static_cast<int>(progress * STAGE_COUNT), 0, STAGE_COUNT - 1);
        Rectangle uv = get_sample_safe_block_uv(block_atlas_tile_uv(stage, STAGE_ROW));

        Vector3 center = {block_x + 0.5f, block_y + 0.5f, block_z + 0.5f};
        rlPushMatrix();
        rlTranslatef(center.x, center.y, center.z);
        rlScalef(TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE);
        draw_textured_cube(get_block_atlas_texture(), uv, {255, 255, 255, OVERLAY_ALPHA});
        rlPopMatrix();
    }

    void label(Rectangle bounds, const std::string& text, Color color, TextAlign align) {
        const Font& font = FontManager::get();
        const float size = static_cast<float>(text_size());
        const float padding = scaled(6.0f);
        const std::string visible = elide(font, text, std::max(0.0f, bounds.width - padding * 2.0f));
        Vector2 measured = MeasureTextEx(font, visible.c_str(), size, scaled(LABEL_SPACING));
        Vector2 pos = {
            std::round(align == TextAlign::Left ? bounds.x + padding : bounds.x + (bounds.width - measured.x) * 0.5f),
            std::round(bounds.y + (bounds.height - measured.y) * 0.5f),
        };
        // The string already fits horizontally, so no nested scissor state is
        // needed here (world lists may have an outer clipping rectangle).
        const float shadow = std::max(1.0f, scaled(2.0f));
        DrawTextEx(font, visible.c_str(), {pos.x + shadow, pos.y + shadow}, size,
                scaled(LABEL_SPACING), Color{0, 0, 0, color.a});
        DrawTextEx(font, visible.c_str(), pos, size, scaled(LABEL_SPACING), color);
    }

    bool button(Rectangle bounds, const std::string& text, bool selected, bool enabled) {
        Vector2 mouse = GetMousePosition();
        bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);
        char identity[256];
        std::snprintf(identity, sizeof(identity), "%s@%.0f,%.0f", text.c_str(), bounds.x, bounds.y);
        if (hovered) hovered_last_frame = true;
        if (hovered && hovered_button_id != identity) {
            hovered_button_id = identity;
            if (sound_callback) sound_callback(SoundEvent::Hover);
        }

        const char* texture_path = !enabled ? TEXTFIELD_TEXTURE_PATH
            : selected || hovered ? BUTTON_HOVER_TEXTURE_PATH : BUTTON_TEXTURE_PATH;
        draw_nine_patch_texture(bounds, texture_path, hovered ? Color{185, 195, 255, 255} : WHITE);
        label(bounds, text, !enabled ? TEXT_DISABLED : hovered ? Color{255, 255, 160, 255} : WHITE);

        // GuiButton() draws nothing visible (init_raygui_style() above made its
        // whole style transparent) - it's called purely for raygui's own
        // click-edge and disabled-state gating instead of hand-rolling both
        // here. Its own convention is "pressed" on mouse release inside
        // bounds, not on press.
        GuiSetState(enabled ? STATE_NORMAL : STATE_DISABLED);
        bool clicked = GuiButton(bounds, "") != 0;
        GuiSetState(STATE_NORMAL);
        if (clicked && sound_callback) sound_callback(SoundEvent::Click);
        return clicked;
    }

    bool text_input(Rectangle bounds, TextInputState& state, bool focused) {
        GuiSetStyle(TEXTBOX, BORDER_WIDTH, std::max(1, static_cast<int>(scaled(2.0f))));
        GuiSetStyle(TEXTBOX, TEXT_PADDING, static_cast<int>(scaled(8.0f)));

        // GuiTextBox edits a raw byte buffer in place, sized for the worst
        // case - every one of the caller's codepoints spending the full 4
        // UTF-8 bytes - plus the terminator.
        std::vector<char> buffer(state.max_codepoints * 4 + 1, '\0');
        std::snprintf(buffer.data(), buffer.size(), "%s", state.text.c_str());

        // `focused` is exactly raygui's own "edit mode": while true, GuiTextBox
        // owns UTF-8 typing, backspace/delete, arrow/click-to-position caret
        // and Ctrl+V paste; while false, a click just reports "pressed" (below)
        // so the caller can hand this field focus, matching the old contract.
        int result = GuiTextBox(bounds, buffer.data(), static_cast<int>(buffer.size()), focused);

        state.text.assign(buffer.data());
        // GuiTextBox only enforces the byte-buffer size above, not the
        // caller's codepoint budget (a Cyrillic name is 2 bytes/letter) - trim
        // back the same way the old hand-rolled version capped it.
        while (utf8_length(state.text) > state.max_codepoints) utf8_pop_back(state.text);

        return result == 1; // RESULT_PRESSED
    }

    bool slider_int(Rectangle bounds, const std::string& label_text, int& value, int min_value, int max_value) {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && sound_callback) sound_callback(SoundEvent::Click);

        GuiSetStyle(SLIDER, BORDER_WIDTH, std::max(1, static_cast<int>(scaled(2.0f))));
        GuiSetStyle(SLIDER, SLIDER_WIDTH, static_cast<int>(scaled(14.0f)));

        // GuiSlider() owns hover/press styling, and - unlike the old hand-rolled
        // version's own `active_slider_id` bookkeeping - already keeps
        // capturing the drag once started even if the pointer leaves `bounds`.
        float float_value = static_cast<float>(value);
        GuiSlider(bounds, nullptr, nullptr, &float_value, static_cast<float>(min_value), static_cast<float>(max_value));
        const int next = min_value < max_value ? static_cast<int>(std::lround(float_value)) : value;
        const bool changed = next != value;
        value = next;

        // No bespoke slider texture for raygui to skin with (init_raygui_style()
        // above just restyles its flat track/knob to the game's dark theme), so
        // the centered "Label: value" caption stays a custom label() draw on
        // top, same as the old nine-patch version.
        label(bounds, label_text + ": " + std::to_string(value),
            hovered ? Color{255, 255, 160, 255} : WHITE);
        return changed;
    }

    void block_icon(Rectangle bounds, BlockType type) {
        if (type == BlockType::Air) return;

        // A torch's in-world (terrain.png) cross sprite is mostly transparent
        // flame/pole at icon scale - same as real Minecraft, these three get a
        // dedicated flat items.png icon instead (drawn the same way
        // item_icon() draws a real ItemType) rather than the Cross-shape
        // render below. Tile coordinates given directly, not read off the
        // atlas image.
        constexpr int ICON_TILE_PIXELS = 16;
        Rectangle items_png_source{};
        bool use_items_png_icon = true;
        switch (type) {
            case BlockType::Torch:            items_png_source = {13 * ICON_TILE_PIXELS, 3 * ICON_TILE_PIXELS, ICON_TILE_PIXELS, ICON_TILE_PIXELS}; break;
            case BlockType::RedstoneTorch:     items_png_source = { 6 * ICON_TILE_PIXELS, 6 * ICON_TILE_PIXELS, ICON_TILE_PIXELS, ICON_TILE_PIXELS}; break;
            case BlockType::LitRedstoneTorch:  items_png_source = { 6 * ICON_TILE_PIXELS, 7 * ICON_TILE_PIXELS, ICON_TILE_PIXELS, ICON_TILE_PIXELS}; break;
            // Same reasoning as the torches above - a sapling's terrain.png
            // cross sprite is a thin sprig on a mostly-transparent tile, barely
            // readable at icon scale. Same tile ItemType::Sapling's own loose-
            // item icon already uses (see Item.cpp's define_material call).
            case BlockType::OakSapling:        items_png_source = {14 * ICON_TILE_PIXELS, 2 * ICON_TILE_PIXELS, ICON_TILE_PIXELS, ICON_TILE_PIXELS}; break;
            default: use_items_png_icon = false; break;
        }
        if (use_items_png_icon) {
            DrawTexturePro(get_item_atlas_texture(), items_png_source, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
            return;
        }

        const Texture2D& atlas = get_block_atlas_texture();
        const BlockProperties& properties = get_block_properties(type);

        if (properties.render_shape == BlockRenderShape::Cross) {
            float inset_x = bounds.width * 0.16f;
            float inset_y = bounds.height * 0.04f;
            Vector2 icon[4] = {
                {bounds.x + inset_x, bounds.y + inset_y},
                {bounds.x + bounds.width - inset_x, bounds.y + inset_y},
                {bounds.x + bounds.width - inset_x, bounds.y + bounds.height - inset_y},
                {bounds.x + inset_x, bounds.y + bounds.height - inset_y},
            };
            int face = static_cast<int>(BlockFace::North);
            rlSetTexture(atlas.id);
            rlBegin(RL_QUADS);
            draw_atlas_quad(properties.texture_uvs[face], icon, properties.texture_tints[face]);
            rlEnd();
            rlSetTexture(0);
            return;
        }

        // Orthographic isometric cube fitted inside the requested icon bounds.
        // The proportions mirror Minecraft's inventory block-item rendering:
        // a shallow diamond top and two taller visible side faces.
        float center_x   = bounds.x + bounds.width  * 0.5f;
        float top_y      = bounds.y + bounds.height * 0.04f;
        float shoulder_y = bounds.y + bounds.height * 0.25f;
        float middle_y   = bounds.y + bounds.height * 0.45f;
        float lower_y    = bounds.y + bounds.height * 0.75f;
        float bottom_y   = bounds.y + bounds.height * 0.96f;
        float left_x     = bounds.x + bounds.width  * 0.07f;
        float right_x    = bounds.x + bounds.width  * 0.93f;

        Vector2 top_face[4] = {
            {center_x,      top_y},
            { right_x, shoulder_y},
            {center_x,   middle_y},
            {  left_x, shoulder_y},
        };
        Vector2 left_face[4] = {
            {  left_x, shoulder_y},
            {center_x,  middle_y},
            {center_x,  bottom_y},
            {  left_x,   lower_y},
        };
        Vector2 right_face[4] = {
            {center_x,   middle_y},
            { right_x, shoulder_y},
            { right_x,    lower_y},
            {center_x,   bottom_y},
        };

        constexpr float LEFT_BRIGHTNESS  = 0.72f;
        constexpr float RIGHT_BRIGHTNESS = 0.86f;

        int top   = static_cast<int>(BlockFace::Top);
        int left  = static_cast<int>(BlockFace::South);
        int right = static_cast<int>(BlockFace::East);

        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
            // Sides first, then the top, so the upper face owns their shared
            // seam even for translucent block textures.
            draw_atlas_quad(properties.texture_uvs[left], left_face,
                            shade(properties.texture_tints[left], LEFT_BRIGHTNESS));
            draw_atlas_quad(properties.texture_uvs[right], right_face,
                            shade(properties.texture_tints[right], RIGHT_BRIGHTNESS));
            draw_atlas_quad(properties.texture_uvs[top], top_face,
                            properties.texture_tints[top]);
        rlEnd();
        rlSetTexture(0);
    }

    bool block_button(Rectangle bounds, BlockType type, bool selected) {
        Vector2 mouse = GetMousePosition();
        bool hovered = CheckCollisionPointRec(mouse, bounds);

        Color fill = selected ? BUTTON_FILL_SELECTED : hovered ? BUTTON_FILL_HOVER : BUTTON_FILL;
        DrawRectangleRec(bounds, fill);
        DrawRectangleLinesEx(bounds, BORDER_THICKNESS, BUTTON_BORDER);

        constexpr float ICON_MARGIN = 4.0f;
        Rectangle icon_bounds = {bounds.x + ICON_MARGIN, bounds.y + ICON_MARGIN,
                                bounds.width - ICON_MARGIN * 2.0f, bounds.height - ICON_MARGIN * 2.0f};
        block_icon(icon_bounds, type);

        return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }

    void item_icon(Rectangle bounds, ItemType type) {
        const ItemProperties& properties = get_item_properties(type);
        const Texture2D& atlas = get_item_atlas_texture();
        DrawTexturePro(atlas, properties.atlas_source, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }

}
