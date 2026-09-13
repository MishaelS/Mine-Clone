#include "ui/WorldCreateScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"

void WorldCreateScreen::enter()
{
    name_field = {};
    seed_field = {};
    seed_field.max_codepoints = 24;
    selected_mode = GameMode::Creative;
    focused_field = 0;
    error_message.clear();
}

WorldCreateScreen::Action WorldCreateScreen::update()
{
    const float width = ui::scaled(ui::MENU_WIDTH);
    const float x = (GetScreenWidth() - width) * 0.5f;
    const float height = ui::scaled(ui::BUTTON_HEIGHT);
    const float gap = ui::scaled(ui::BUTTON_GAP);
    const float half = (width - gap) * 0.5f;
    ui::label({0, ui::scaled(28), static_cast<float>(GetScreenWidth()), height}, ui::tr("create.title"));

    if (IsKeyPressed(KEY_TAB)) focused_field = (focused_field + 1) % 2;
    ui::label({x, ui::scaled(100), width, ui::scaled(24)}, ui::tr("create.name"), LIGHTGRAY, ui::TextAlign::Left);
    if (ui::text_input({x, ui::scaled(128), width, height}, name_field, focused_field == 0)) focused_field = 0;
    ui::label({x, ui::scaled(196), width, ui::scaled(24)}, ui::tr("create.seed"), LIGHTGRAY, ui::TextAlign::Left);
    if (ui::text_input({x, ui::scaled(224), width, height}, seed_field, focused_field == 1)) focused_field = 1;
    ui::label({x, ui::scaled(292), width, ui::scaled(24)}, ui::tr("create.mode"), LIGHTGRAY, ui::TextAlign::Left);
    if (ui::button({x, ui::scaled(320), half, height}, ui::tr("create.creative"), selected_mode == GameMode::Creative))
        selected_mode = GameMode::Creative;
    if (ui::button({x + half + gap, ui::scaled(320), half, height}, ui::tr("create.survival"), selected_mode == GameMode::Survival))
        selected_mode = GameMode::Survival;

    if (!error_message.empty())
        ui::label({x, ui::scaled(376), width, height}, ui::tr("create.required"), RED);

    const float bottom = GetScreenHeight() - ui::scaled(68);
    if (ui::button({x, bottom, half, height}, ui::tr("worlds.create"))) {
        if (name_field.text.find_first_not_of(" \t\r\n") == std::string::npos) {
            error_message = "create.required";
        } else {
            WorldInfo info;
            info.display_name = name_field.text;
            info.folder_name = WorldSave::next_available_folder_name(name_field.text);
            info.seed = WorldSave::derive_seed(name_field.text, seed_field.text);
            info.game_mode = selected_mode;
            return {ActionType::Create, info};
        }
    }
    if (ui::button({x + half + gap, bottom, half, height}, ui::tr("common.cancel")) || IsKeyPressed(KEY_ESCAPE))
        return {ActionType::Cancel, {}};
    return {};
}
