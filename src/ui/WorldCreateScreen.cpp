#include "ui/WorldCreateScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"

void WorldCreateScreen::enter()
{
    name_field = {};
    seed_field = {};
    seed_field.max_codepoints = 24;
    selected_mode = GameMode::Creative;
    selected_type = WorldType::Normal;
    custom_world = {};
    custom_screen_open = false;
    allow_commands = false;
    focused_field = 0;
    error_message.clear();
}

WorldCreateScreen::Action WorldCreateScreen::update()
{
    // "Настроить..." page for a custom world - replaces this screen until
    // it's closed with Done (keeps the edits) or Cancel.
    if (custom_screen_open) {
        CustomWorldScreen::Action action = custom_screen.update();
        if (action == CustomWorldScreen::Action::Done) custom_world = custom_screen.result();
        if (action != CustomWorldScreen::Action::None) custom_screen_open = false;
        return {};
    }

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
    if (ui::button({x, ui::scaled(320), half, height}, ui::tr("create.mode") + ": " +
                   ui::tr(selected_mode == GameMode::Creative ? "create.creative" : "create.survival"))) {
        selected_mode = selected_mode == GameMode::Creative ? GameMode::Survival : GameMode::Creative;
    }
    if (ui::button({x + half + gap, ui::scaled(320), half, height}, ui::tr("create.commands") + ": " +
                   ui::tr(allow_commands ? "common.on" : "common.off"), allow_commands)) {
        allow_commands = !allow_commands;
    }

    // World type: each click cycles to the next generator preset, with a
    // one-line description of the current one underneath.
    const float type_y = ui::scaled(320) + height + gap;
    const std::string type_id = world_type_id(selected_type);
    const bool custom = selected_type == WorldType::Custom;
    if (ui::button({x, type_y, custom ? half : width, height},
                   ui::tr_format("create.world_type", {ui::tr("world_type." + type_id)}))) {
        selected_type = next_world_type(selected_type);
    }
    if (custom && ui::button({x + half + gap, type_y, half, height}, ui::tr("create.customize"))) {
        custom_screen.enter(custom_world);
        custom_screen_open = true;
    }
    ui::label({x, type_y + height + ui::scaled(4), width, ui::scaled(22)},
              ui::tr("world_type." + type_id + ".description"), LIGHTGRAY);

    if (!error_message.empty())
        ui::label({x, type_y + height + ui::scaled(30), width, ui::scaled(24)}, ui::tr("create.required"), RED);

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
            info.allow_commands = allow_commands;
            info.world_type = selected_type;
            info.custom = custom_world;
            return {ActionType::Create, info};
        }
    }
    if (ui::button({x + half + gap, bottom, half, height}, ui::tr("common.cancel")) || IsKeyPressed(KEY_ESCAPE))
        return {ActionType::Cancel, {}};
    return {};
}
