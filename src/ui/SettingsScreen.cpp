#include "ui/SettingsScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "core/Block.hpp"
#include "world/Chunk.hpp"

#include <algorithm>

SettingsScreen::Action SettingsScreen::update(Settings& settings)
{
    const bool was_rebinding = rebinding_action.has_value();
    if (rebinding_action) {
        if (IsKeyPressed(KEY_ESCAPE)) rebinding_action.reset();
        else if (auto binding = poll_any_binding_pressed()) {
            settings.keybindings[static_cast<size_t>(*rebinding_action)] = *binding;
            SettingsIO::save(settings);
            rebinding_action.reset();
        }
    }

    const float width = ui::scaled(ui::MENU_WIDTH);
    const float height = ui::scaled(ui::BUTTON_HEIGHT);
    const float gap = ui::scaled(ui::BUTTON_GAP);
    const float half = (width - gap) * 0.5f;
    const float x = (GetScreenWidth() - width) * 0.5f;
    const float top = ui::scaled(108.0f);
    const auto cell = [&](int column, int row) {
        return Rectangle{x + column * (half + gap), top + row * (height + gap), half, height};
    };
    const char* title = section == Section::Controls ? "settings.controls"
        : section == Section::Graphics ? "settings.graphics"
        : section == Section::Sound ? "settings.sound"
        : section == Section::Language ? "settings.language" : "settings.title";
    ui::label({0, ui::scaled(28), static_cast<float>(GetScreenWidth()), height}, ui::tr(title));

    bool changed = false;
    const auto scale_button = [&](Rectangle bounds) {
        const std::string key = "settings.scale." + std::to_string(settings.ui_scale);
        if (ui::button(bounds, ui::tr("settings.ui_scale") + ": " + ui::tr(key))) {
            settings.ui_scale = settings.ui_scale % 4 + 1;
            // Apply on the next frame, so geometry and font never use
            // different scales within the frame handling this click.
            changed = true;
        }
    };

    if (section == Section::Overview) {
        changed |= ui::slider_int(cell(0, 0), ui::tr("settings.master"), settings.master_volume, 0, 100);
        scale_button(cell(1, 0));
        if (ui::button(cell(0, 2), ui::tr("settings.graphics") + "...")) enter(Section::Graphics);
        if (ui::button(cell(1, 2), ui::tr("settings.sound") + "...")) enter(Section::Sound);
        if (ui::button(cell(0, 3), ui::tr("settings.controls") + "...")) enter(Section::Controls);
        if (ui::button(cell(1, 3), ui::tr("settings.language") + "...")) enter(Section::Language);
    } else if (section == Section::Graphics) {
        changed |= ui::slider_int(cell(0, 0), ui::tr("settings.render"), settings.render_distance_chunks, 4, 16);
        settings.fog_distance_blocks = std::min(settings.fog_distance_blocks, settings.render_distance_chunks * CHUNK_SIZE);
        changed |= ui::slider_int(cell(1, 0), ui::tr("settings.fog"), settings.fog_distance_blocks, 32,
                                 settings.render_distance_chunks * CHUNK_SIZE);
        if (ui::slider_int(cell(0, 1), ui::tr("settings.fps"), settings.target_fps, 30, 240)) {
            SetTargetFPS(settings.target_fps);
            changed = true;
        }
        scale_button(cell(1, 1));
        std::string size = ui::tr("settings.window") + ": " + std::to_string(GetScreenWidth()) + "x" + std::to_string(GetScreenHeight());
        if (ui::button(cell(0, 2), size)) {
            constexpr int widths[] = {1280, 1600, 1920};
            constexpr int heights[] = {720, 900, 1080};
            int next = 0;
            for (int i = 0; i < 3; ++i)
                if (GetScreenWidth() == widths[i] && GetScreenHeight() == heights[i]) next = (i + 1) % 3;
            SetWindowSize(widths[next], heights[next]);
            settings.window_width = widths[next];
            settings.window_height = heights[next];
            changed = true;
        }
        if (ui::button(cell(1, 2), ui::tr("settings.filter") + ": " +
                       ui::tr(settings.texture_filter == TextureFilterMode::Point ? "settings.point" : "settings.smooth"))) {
            settings.texture_filter = settings.texture_filter == TextureFilterMode::Point
                ? TextureFilterMode::Bilinear : TextureFilterMode::Point;
            SetTextureFilter(get_block_atlas_texture(), settings.texture_filter == TextureFilterMode::Bilinear
                ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
            changed = true;
        }
    } else if (section == Section::Sound) {
        changed |= ui::slider_int(cell(0, 0), ui::tr("settings.master"), settings.master_volume, 0, 100);
        changed |= ui::slider_int(cell(1, 0), ui::tr("settings.music"), settings.music_volume, 0, 100);
        changed |= ui::slider_int(cell(0, 1), ui::tr("settings.effects"), settings.effects_volume, 0, 100);
        changed |= ui::slider_int(cell(1, 1), ui::tr("settings.ambient"), settings.ambient_volume, 0, 100);
    } else if (section == Section::Language) {
        if (ui::button(cell(0, 0), ui::tr("language.ru"), settings.language == "ru")) {
            settings.language = "ru";
            changed = true;
        }
        if (ui::button(cell(1, 0), ui::tr("language.en"), settings.language == "en")) {
            settings.language = "en";
            changed = true;
        }
    } else {
        for (size_t i = 0; i < settings.keybindings.size(); ++i) {
            const GameAction action = static_cast<GameAction>(i);
            const bool active = rebinding_action && *rebinding_action == action;
            const bool english = settings.language == "en";
            const std::string caption = std::string(game_action_display_name(action, english)) + ": " +
                (active ? "..." : binding_display_name(settings.keybindings[i], english));
            if (ui::button(cell(static_cast<int>(i % 2), static_cast<int>(i / 2)), caption, active,
                           !rebinding_action || active) && !was_rebinding && !rebinding_action) {
                rebinding_action = action;
            }
        }
    }

    if (changed) SettingsIO::save(settings);
    Rectangle done = {(GetScreenWidth() - ui::scaled(400)) * 0.5f,
                      GetScreenHeight() - ui::scaled(68), ui::scaled(400), height};
    if (ui::button(done, ui::tr("common.done"), false, !rebinding_action) ||
        (!was_rebinding && IsKeyPressed(KEY_ESCAPE))) {
        if (section == Section::Overview) return {ActionType::Back};
        enter();
    }
    return {};
}
