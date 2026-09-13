#include "ui/Widgets.hpp"
#include "ui/FontManager.hpp"
#include "core/TextureManager.hpp"
#include "rendering/BlockMesh.hpp"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
    constexpr float TEXT_SPACING = 1.0f;
    constexpr int BUTTON_FONT_SIZE = 20;
    constexpr int FIELD_FONT_SIZE = 20;
    constexpr int SLIDER_FONT_SIZE = 18;
    constexpr float BORDER_THICKNESS = 2.0f;

    const char* BUTTON_TEXTURE_PATH = "sprites/gui/widgets/widget1.png";
    const char* BUTTON_HOVER_TEXTURE_PATH = "sprites/gui/widgets/widget2.png";
    // Same 200x20 strip layout as the two button textures above, so they
    // share one nine-patch border size - see draw_nine_patch_texture().
    const char* TEXTFIELD_TEXTURE_PATH = "sprites/gui/widgets/widget0.png";
    constexpr float NINE_PATCH_BORDER = 3.0f;

    // The slider's draggable handle, 7x20 - small enough to draw as one
    // straight (non-nine-patch) stretch into its handle rect.
    const char* SLIDER_HANDLE_TEXTURE_PATH = "sprites/gui/widgets/widget3.png";
    const char* SLIDER_HANDLE_ACTIVE_TEXTURE_PATH = "sprites/gui/widgets/widget4.png";

    const char* TITLE_BLUR_TEXTURE_PATH = "sprites/gui/titleBlur.png";
    const char* OPTIONS_BACKGROUND_TEXTURE_PATH = "sprites/gui/optionsBackground.png";
    constexpr float OPTIONS_BACKGROUND_TILE_SCALE = 2.0f; // on-screen size of each 16px source tile

    // block_button()'s own flat chrome - unrelated to the widget0-4
    // textures above, kept as-is (no texture asset covers this slot look).
    constexpr Color BUTTON_FILL = {60, 60, 68, 255};
    constexpr Color BUTTON_FILL_HOVER = {82, 82, 92, 255};
    constexpr Color BUTTON_FILL_SELECTED = {88, 138, 88, 255};
    constexpr Color BUTTON_BORDER = {20, 20, 24, 255};
    constexpr Color TEXT_DISABLED = {140, 140, 140, 255};

    constexpr Color FIELD_BORDER_FOCUSED = {120, 170, 255, 255};

    constexpr Color TRACK_FILL = {40, 40, 46, 255};
    constexpr Color TRACK_BORDER = {80, 80, 88, 255};

    constexpr int TOOLTIP_FONT_SIZE = 16;
    constexpr float TOOLTIP_PADDING = 6.0f;
    constexpr float TOOLTIP_CURSOR_OFFSET = 14.0f;
    constexpr float TOOLTIP_SCREEN_MARGIN = 4.0f;
    constexpr Color TOOLTIP_BACKGROUND = {20, 20, 24, 235};
    constexpr float CROSSHAIR_ARM_LENGTH = 10.0f;
    constexpr float CROSSHAIR_THICKNESS = 2.0f;
    constexpr unsigned char CROSSHAIR_INTENSITY = 235;
    constexpr float TARGET_OUTLINE_SIZE = 1.002f;
    constexpr Color TARGET_OUTLINE_COLOR = {0, 0, 0, 200};

    Color shade(Color color, float brightness)
    {
        return {
            static_cast<unsigned char>(color.r * brightness),
            static_cast<unsigned char>(color.g * brightness),
            static_cast<unsigned char>(color.b * brightness),
            color.a,
        };
    }

    void draw_atlas_quad(Rectangle uv, const Vector2 (&vertices)[4], Color tint)
    {
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
    void draw_nine_patch_texture(Rectangle bounds, const char* texture_path)
    {
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
                    {0.0f, 0.0f}, 0.0f, WHITE);
            }
        }
    }

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

void Tooltip::clear()
{
    visible = false;
    text.clear();
}

void Tooltip::show(const std::string& new_text, Vector2 new_anchor)
{
    text = new_text;
    anchor = new_anchor;
    visible = !text.empty();
}

void Tooltip::draw() const
{
    if (!visible) return;

    const Font& font = FontManager::get();
    Vector2 text_size = MeasureTextEx(font, text.c_str(), static_cast<float>(TOOLTIP_FONT_SIZE), TEXT_SPACING);
    Rectangle bounds = {
        anchor.x + TOOLTIP_CURSOR_OFFSET,
        anchor.y + TOOLTIP_CURSOR_OFFSET,
        text_size.x + TOOLTIP_PADDING * 2.0f,
        text_size.y + TOOLTIP_PADDING * 2.0f,
    };
    bounds.x = std::clamp(bounds.x, TOOLTIP_SCREEN_MARGIN,
        std::max(TOOLTIP_SCREEN_MARGIN, GetScreenWidth() - bounds.width - TOOLTIP_SCREEN_MARGIN));
    bounds.y = std::clamp(bounds.y, TOOLTIP_SCREEN_MARGIN,
        std::max(TOOLTIP_SCREEN_MARGIN, GetScreenHeight() - bounds.height - TOOLTIP_SCREEN_MARGIN));

    panel(bounds, TOOLTIP_BACKGROUND);
    label(bounds, text, TOOLTIP_FONT_SIZE, WHITE);
}

void panel(Rectangle bounds, Color color)
{
    DrawRectangleRec(bounds, color);
}

void title_background()
{
    const Texture2D& texture = TextureManager::get(TITLE_BLUR_TEXTURE_PATH);
    Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    Rectangle destination = {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
}

void menu_background()
{
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
    Rectangle source = {0.0f, 0.0f, screen_w / OPTIONS_BACKGROUND_TILE_SCALE, screen_h / OPTIONS_BACKGROUND_TILE_SCALE};
    Rectangle destination = {0.0f, 0.0f, screen_w, screen_h};
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
}

void crosshair()
{
    Color color = {CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, 255};
    float center_x = GetScreenWidth() / 2.0f;
    float center_y = GetScreenHeight() / 2.0f;

    rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    DrawRectangle(static_cast<int>(center_x - CROSSHAIR_ARM_LENGTH),
                  static_cast<int>(center_y - CROSSHAIR_THICKNESS / 2.0f),
                  static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f),
                  static_cast<int>(CROSSHAIR_THICKNESS), color);
    DrawRectangle(static_cast<int>(center_x - CROSSHAIR_THICKNESS / 2.0f),
                  static_cast<int>(center_y - CROSSHAIR_ARM_LENGTH),
                  static_cast<int>(CROSSHAIR_THICKNESS),
                  static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f), color);
    EndBlendMode();
}

void block_outline(int block_x, int block_y, int block_z)
{
    Vector3 center = {block_x + 0.5f, block_y + 0.5f, block_z + 0.5f};
    DrawCubeWires(center, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE,
                  TARGET_OUTLINE_SIZE, TARGET_OUTLINE_COLOR);
}

void block_breaking_overlay(int block_x, int block_y, int block_z, float progress)
{
    constexpr int STAGE_COUNT = 10; // terrain.png row 15, columns 0-9
    constexpr int STAGE_ROW = 15;
    int stage = std::clamp(static_cast<int>(progress * STAGE_COUNT), 0, STAGE_COUNT - 1);
    Rectangle uv = get_sample_safe_block_uv(block_atlas_tile_uv(stage, STAGE_ROW));

    Vector3 center = {block_x + 0.5f, block_y + 0.5f, block_z + 0.5f};
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlScalef(TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE);
    draw_textured_cube(get_block_atlas_texture(), uv, WHITE);
    rlPopMatrix();
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

    // No dedicated disabled-button texture in this asset set (widget0 is
    // the text field, not a third button state) - a disabled button keeps
    // the plain unhovered look and relies on TEXT_DISABLED's dimmer label
    // to read as inert.
    const char* texture_path = (enabled && (selected || hovered)) ? BUTTON_HOVER_TEXTURE_PATH : BUTTON_TEXTURE_PATH;
    draw_nine_patch_texture(bounds, texture_path);
    label(bounds, text, BUTTON_FONT_SIZE, enabled ? WHITE : TEXT_DISABLED);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool text_input(Rectangle bounds, TextInputState& state, bool focused)
{
    draw_nine_patch_texture(bounds, TEXTFIELD_TEXTURE_PATH);
    // The texture's own border reads as "unfocused" - focus gets an extra
    // highlighted outline on top rather than a second texture asset.
    if (focused) {
        DrawRectangleLinesEx(bounds, BORDER_THICKNESS, FIELD_BORDER_FOCUSED);
    }

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

    Vector2 mouse = GetMousePosition();
    // "Held" covers the whole bounds, not just the handle rect, matching
    // the drag-anywhere-in-bounds behavior below.
    bool handle_active = CheckCollisionPointRec(mouse, handle) ||
                          (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, bounds));
    const Texture2D& handle_texture = TextureManager::get(
        handle_active ? SLIDER_HANDLE_ACTIVE_TEXTURE_PATH : SLIDER_HANDLE_TEXTURE_PATH);
    Rectangle handle_source = {0.0f, 0.0f, static_cast<float>(handle_texture.width), static_cast<float>(handle_texture.height)};
    DrawTexturePro(handle_texture, handle_source, handle, {0.0f, 0.0f}, 0.0f, WHITE);

    bool changed = false;
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
    if (type == BlockType::Air) return;

    const Texture2D& atlas = get_block_atlas_texture();
    const BlockProperties& properties = get_block_properties(type);

    // Orthographic isometric cube fitted inside the requested icon bounds.
    // The proportions mirror Minecraft's inventory block-item rendering:
    // a shallow diamond top and two taller visible side faces.
    float center_x = bounds.x + bounds.width * 0.5f;
    float top_y = bounds.y + bounds.height * 0.04f;
    float shoulder_y = bounds.y + bounds.height * 0.25f;
    float middle_y = bounds.y + bounds.height * 0.45f;
    float lower_y = bounds.y + bounds.height * 0.75f;
    float bottom_y = bounds.y + bounds.height * 0.96f;
    float left_x = bounds.x + bounds.width * 0.07f;
    float right_x = bounds.x + bounds.width * 0.93f;

    Vector2 top_face[4] = {
        {center_x, top_y},
        {right_x, shoulder_y},
        {center_x, middle_y},
        {left_x, shoulder_y},
    };
    Vector2 left_face[4] = {
        {left_x, shoulder_y},
        {center_x, middle_y},
        {center_x, bottom_y},
        {left_x, lower_y},
    };
    Vector2 right_face[4] = {
        {center_x, middle_y},
        {right_x, shoulder_y},
        {right_x, lower_y},
        {center_x, bottom_y},
    };

    constexpr float LEFT_BRIGHTNESS = 0.72f;
    constexpr float RIGHT_BRIGHTNESS = 0.86f;

    int top = static_cast<int>(BlockFace::Top);
    int left = static_cast<int>(BlockFace::South);
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

void item_icon(Rectangle bounds, ItemType type)
{
    const ItemProperties& properties = get_item_properties(type);
    const Texture2D& atlas = get_item_atlas_texture();
    DrawTexturePro(atlas, properties.atlas_source, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
}

}
