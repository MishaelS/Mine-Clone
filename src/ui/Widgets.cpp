#include "ui/Widgets.hpp"
#include "core/FontManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
    constexpr float TEXT_SPACING = 1.0f;
    constexpr int BUTTON_FONT_SIZE = 20;
    constexpr int FIELD_FONT_SIZE = 20;
    constexpr int SLIDER_FONT_SIZE = 18;
    constexpr float BORDER_THICKNESS = 2.0f;

    constexpr Color BUTTON_FILL = {60, 60, 68, 255};
    constexpr Color BUTTON_FILL_HOVER = {82, 82, 92, 255};
    constexpr Color BUTTON_FILL_SELECTED = {88, 138, 88, 255};
    constexpr Color BUTTON_FILL_DISABLED = {48, 48, 52, 255};
    constexpr Color BUTTON_BORDER = {20, 20, 24, 255};
    constexpr Color BUTTON_BORDER_DISABLED = {68, 68, 72, 255};
    constexpr Color TEXT_DISABLED = {140, 140, 140, 255};

    constexpr Color FIELD_FILL = {30, 30, 34, 255};
    constexpr Color FIELD_BORDER = {80, 80, 88, 255};
    constexpr Color FIELD_BORDER_FOCUSED = {120, 170, 255, 255};

    constexpr Color TRACK_FILL = {40, 40, 46, 255};
    constexpr Color TRACK_BORDER = {80, 80, 88, 255};
    constexpr Color HANDLE_FILL = {210, 210, 220, 255};

    // Walks back from the end of a UTF-8 string to the start of its last
    // codepoint (a lead byte's top bits are never 10xxxxxx - only
    // continuation bytes are), then erases from there - removes one whole
    // multi-byte character (e.g. Cyrillic, 2 bytes/letter) in one step
    // instead of leaving a dangling continuation byte behind.
    void utf8_pop_back(std::string& text)
    {
        if (text.empty()) return;
        size_t pos = text.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
        text.erase(pos);
    }

    size_t utf8_length(const std::string& text)
    {
        size_t count = 0;
        for (unsigned char c : text) {
            if ((c & 0xC0) != 0x80) ++count; // count lead bytes only, not continuation bytes
        }
        return count;
    }
}

namespace ui {

void panel(Rectangle bounds, Color color)
{
    DrawRectangleRec(bounds, color);
}

void label(Rectangle bounds, const std::string& text, int font_size, Color color)
{
    const Font& font = FontManager::get();
    Vector2 size = MeasureTextEx(font, text.c_str(), static_cast<float>(font_size), TEXT_SPACING);
    Vector2 pos = {
        bounds.x + (bounds.width - size.x) / 2.0f,
        bounds.y + (bounds.height - size.y) / 2.0f,
    };
    DrawTextEx(font, text.c_str(), pos, static_cast<float>(font_size), TEXT_SPACING, color);
}

bool button(Rectangle bounds, const std::string& text, bool selected, bool enabled)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);

    Color fill = !enabled ? BUTTON_FILL_DISABLED
               : selected ? BUTTON_FILL_SELECTED
               : hovered  ? BUTTON_FILL_HOVER
                          : BUTTON_FILL;
    Color border = enabled ? BUTTON_BORDER : BUTTON_BORDER_DISABLED;

    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, BORDER_THICKNESS, border);
    label(bounds, text, BUTTON_FONT_SIZE, enabled ? WHITE : TEXT_DISABLED);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool text_input(Rectangle bounds, TextInputState& state, bool focused)
{
    DrawRectangleRec(bounds, FIELD_FILL);
    DrawRectangleLinesEx(bounds, BORDER_THICKNESS, focused ? FIELD_BORDER_FOCUSED : FIELD_BORDER);

    if (focused) {
        int codepoint = GetCharPressed();
        while (codepoint != 0) {
            if (utf8_length(state.text) < state.max_codepoints) {
                int byte_count = 0;
                const char* encoded = CodepointToUTF8(codepoint, &byte_count);
                state.text.append(encoded, byte_count);
            }
            codepoint = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            utf8_pop_back(state.text);
        }
    }

    const Font& font = FontManager::get();
    Vector2 text_pos = {bounds.x + 8.0f, bounds.y + (bounds.height - FIELD_FONT_SIZE) / 2.0f};
    DrawTextEx(font, state.text.c_str(), text_pos, static_cast<float>(FIELD_FONT_SIZE), TEXT_SPACING, WHITE);

    if (focused && std::fmod(static_cast<float>(GetTime()), 1.0f) < 0.5f) {
        float text_width = MeasureTextEx(font, state.text.c_str(), static_cast<float>(FIELD_FONT_SIZE), TEXT_SPACING).x;
        DrawRectangle(static_cast<int>(text_pos.x + text_width + 2.0f), static_cast<int>(bounds.y + 4.0f),
                      2, static_cast<int>(bounds.height - 8.0f), WHITE);
    }

    return CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool slider_int(Rectangle bounds, const std::string& label_text, int& value, int min_value, int max_value)
{
    float track_x = bounds.x + bounds.width * 0.45f;
    float track_width = bounds.width * 0.55f;
    Rectangle track = {track_x, bounds.y + bounds.height * 0.4f, track_width, bounds.height * 0.2f};

    char full_label[160];
    std::snprintf(full_label, sizeof(full_label), "%s: %d", label_text.c_str(), value);
    Rectangle label_bounds = {bounds.x, bounds.y, track_x - bounds.x - 10.0f, bounds.height};
    const Font& font = FontManager::get();
    Vector2 label_size = MeasureTextEx(font, full_label, static_cast<float>(SLIDER_FONT_SIZE), TEXT_SPACING);
    DrawTextEx(font, full_label,
               {label_bounds.x, label_bounds.y + (label_bounds.height - label_size.y) / 2.0f},
               static_cast<float>(SLIDER_FONT_SIZE), TEXT_SPACING, WHITE);

    DrawRectangleRec(track, TRACK_FILL);
    DrawRectangleLinesEx(track, 1.5f, TRACK_BORDER);

    float t = (max_value > min_value) ? static_cast<float>(value - min_value) / static_cast<float>(max_value - min_value) : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    Rectangle handle = {track.x + t * track.width - 5.0f, track.y - 4.0f, 10.0f, track.height + 8.0f};
    DrawRectangleRec(handle, HANDLE_FILL);

    bool changed = false;
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, bounds)) {
        float new_t = std::clamp((mouse.x - track.x) / track.width, 0.0f, 1.0f);
        int new_value = min_value + static_cast<int>(std::lround(new_t * static_cast<float>(max_value - min_value)));
        if (new_value != value) {
            value = new_value;
            changed = true;
        }
    }

    return changed;
}

void block_icon(Rectangle bounds, BlockType type)
{
    const Texture2D& atlas = get_block_atlas_texture();
    const BlockProperties& properties = get_block_properties(type);
    Rectangle uv = properties.texture_uvs[static_cast<int>(BlockFace::Top)]; // normalized (0..1) - see Block.cpp

    Rectangle source = {
        uv.x * static_cast<float>(atlas.width),
        uv.y * static_cast<float>(atlas.height),
        uv.width * static_cast<float>(atlas.width),
        uv.height * static_cast<float>(atlas.height),
    };
    DrawTexturePro(atlas, source, bounds, {0.0f, 0.0f}, 0.0f, properties.texture_tints[static_cast<int>(BlockFace::Top)]);
}

bool block_button(Rectangle bounds, BlockType type, bool selected)
{
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

}
