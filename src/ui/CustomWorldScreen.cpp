#include "ui/CustomWorldScreen.hpp"
#include "ui/Localization.hpp"
#include "world/Chunk.hpp" // MIN_WORLD_Y

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
    constexpr size_t MAX_LAYERS_CODEPOINTS = 300;

    // Lower-cases ASCII and Russian letters in UTF-8 text, for matching a
    // typed block name against its translation regardless of case.
    std::string lower_utf8(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            unsigned char next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
            if (c >= 'A' && c <= 'Z') {
                out += static_cast<char>(c + ('a' - 'A'));
            } else if (c == 0xD0 && next >= 0x90 && next <= 0x9F) { // А-П -> а-п
                out += static_cast<char>(0xD0);
                out += static_cast<char>(next + 0x20);
                ++i;
            } else if (c == 0xD0 && next >= 0xA0 && next <= 0xAF) { // Р-Я -> р-я
                out += static_cast<char>(0xD1);
                out += static_cast<char>(next - 0x20);
                ++i;
            } else if (c == 0xD0 && next == 0x81) { // Ё -> ё
                out += static_cast<char>(0xD1);
                out += static_cast<char>(0x91);
                ++i;
            } else {
                out += static_cast<char>(c);
            }
        }
        return out;
    }

    // One line of the scrolling list: a group heading or a feature.
    struct Row {
        bool heading = false;
        CustomFeatureGroup group = CustomFeatureGroup::Structures;
        const CustomFeatureInfo* feature = nullptr;
    };

    std::vector<Row> build_rows() {
        std::vector<Row> rows;
        for (CustomFeatureGroup group : {CustomFeatureGroup::Structures, CustomFeatureGroup::Biomes}) {
            rows.push_back({true, group, nullptr});
            for (const CustomFeatureInfo& feature : CUSTOM_FEATURES) {
                if (feature.group == group) rows.push_back({false, group, &feature});
            }
        }
        return rows;
    }

    const char* group_key(CustomFeatureGroup group) {
        return group == CustomFeatureGroup::Biomes ? "custom.group.biomes" : "custom.group.structures";
    }
}

void CustomWorldScreen::enter(const CustomWorld& current)
{
    editing = current;
    layers_field = {};
    layers_field.max_codepoints = MAX_LAYERS_CODEPOINTS;
    layers_field.text = editing.layers;
    ui::move_text_caret_to_end(layers_field);
    first_row = 0;

    translated_names.clear();
    for (BlockType type : all_block_types()) {
        translated_names.emplace(lower_utf8(ui::block_display_name(type)), type);
    }
    parsed_text.clear();
    refresh_parse();
}

void CustomWorldScreen::refresh_parse()
{
    if (layers_field.text == parsed_text && !parsed_text.empty()) return;
    parsed_text = layers_field.text;
    parsed = parse_custom_layers(parsed_text, [this](const std::string& name) -> std::optional<BlockType> {
        auto found = translated_names.find(lower_utf8(name));
        if (found == translated_names.end()) return std::nullopt;
        return found->second;
    });
}

CustomWorldScreen::Action CustomWorldScreen::update()
{
    const float width = ui::scaled(ui::MENU_WIDTH);
    const float x = (GetScreenWidth() - width) * 0.5f;
    const float height = ui::scaled(ui::BUTTON_HEIGHT);
    const float gap = ui::scaled(ui::BUTTON_GAP);
    const float half = (width - gap) * 0.5f;
    const float line = ui::scaled(22);

    ui::label({0, ui::scaled(28), static_cast<float>(GetScreenWidth()), height}, ui::tr("custom.title"));

    // Layer recipe + live check.
    float y = ui::scaled(84);
    ui::label({x, y, width, ui::scaled(24)}, ui::tr("custom.layers"), LIGHTGRAY, ui::TextAlign::Left);
    y += ui::scaled(26);
    ui::text_input({x, y, width, height}, layers_field, true);
    refresh_parse();
    y += height + ui::scaled(4);
    if (parsed.ok()) {
        int top = static_cast<int>(parsed.column.size()) - 1;
        while (top > 0 && parsed.column[top] == BlockType::Air) --top;
        ui::label({x, y, width, line},
                  ui::tr_format("custom.layers_ok", {std::to_string(parsed.column.size()), std::to_string(top + MIN_WORLD_Y)}),
                  Color{140, 220, 140, 255}, ui::TextAlign::Left);
    } else {
        ui::label({x, y, width, line}, ui::tr_format(parsed.error_key, {parsed.error_arg}), Color{255, 110, 110, 255},
                  ui::TextAlign::Left);
    }
    y += line;
    ui::label({x, y, width, line}, ui::tr("custom.layers_hint"), GRAY, ui::TextAlign::Left);
    y += line + ui::scaled(12);

    // Structures/biomes list - as many rows as fit above the bottom buttons,
    // the rest reached with the mouse wheel.
    const float bottom = GetScreenHeight() - ui::scaled(68);
    const float row_step = height + gap;
    const std::vector<Row> rows = build_rows();
    const int visible = std::max(1, static_cast<int>((bottom - gap - y + gap) / row_step));
    const int max_first = std::max(0, static_cast<int>(rows.size()) - visible);
    first_row = std::clamp(first_row - static_cast<int>(GetMouseWheelMove()), 0, max_first);

    for (int i = 0; i < visible && first_row + i < static_cast<int>(rows.size()); ++i) {
        const Row& row = rows[first_row + i];
        const float row_y = y + i * row_step;
        if (row.heading) {
            ui::label({x, row_y + height - ui::scaled(28), width, ui::scaled(24)}, ui::tr(group_key(row.group)),
                      LIGHTGRAY, ui::TextAlign::Left);
            continue;
        }
        CustomFeatureSetting& setting = editing[row.feature->feature];
        const std::string name = ui::tr(std::string("custom.feature.") + row.feature->id);
        if (ui::button({x, row_y, half, height}, name + ": " + ui::tr(setting.enabled ? "common.on" : "common.off"),
                       setting.enabled)) {
            setting.enabled = !setting.enabled;
        }
        ui::slider_int({x + half + gap, row_y, half, height}, ui::tr("custom.chance"), setting.chance,
                       CUSTOM_CHANCE_MIN, CUSTOM_CHANCE_MAX, "%", setting.enabled);
    }
    if (max_first > 0) {
        // Scroll position: a thin bar beside the list.
        const float track_h = visible * row_step - gap;
        const float thumb_h = track_h * visible / static_cast<float>(rows.size());
        const float thumb_y = y + (track_h - thumb_h) * first_row / static_cast<float>(max_first);
        DrawRectangleRec({x + width + ui::scaled(6), y, ui::scaled(4), track_h}, Color{255, 255, 255, 40});
        DrawRectangleRec({x + width + ui::scaled(6), thumb_y, ui::scaled(4), thumb_h}, Color{255, 255, 255, 160});
    }

    if (ui::button({x, bottom, half, height}, ui::tr("common.done"), false, parsed.ok()) && parsed.ok()) {
        editing.layers = parsed.canonical;
        editing.column = parsed.column;
        return Action::Done;
    }
    if (ui::button({x + half + gap, bottom, half, height}, ui::tr("common.cancel")) || IsKeyPressed(KEY_ESCAPE)) {
        return Action::Cancel;
    }
    return Action::None;
}
