#include "app/Application.hpp"
#include "core/TextureManager.hpp"
#include "core/WorldSave.hpp"
#include "ui/FontManager.hpp"
#include "ui/Localization.hpp"
#include "ui/Widgets.hpp"

#include "rlgl.h"

Application::Application(int screen_width, int screen_height, const char* title)
    : settings(SettingsIO::load())
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(settings.window_width > 0 ? settings.window_width : screen_width,
               settings.window_height > 0 ? settings.window_height : screen_height, title);
    SetWindowMinSize(960, 540);

    // Esc defaults to closing the window (WindowShouldClose()'s other
    // trigger) - disabled so SettingsScreen can use it to cancel a
    // keybind-rebind-in-progress instead of quitting the whole game out
    // from under it. The only quit paths left are "Закрыть игру"
    // (quit_requested) and the OS window-close control.
    SetExitKey(KEY_NULL);

    // First-launch loading splash: titleIntroLogo.png, up for exactly as
    // long as the synchronous loads just below actually take. There's no
    // background-loading thread here - the loads block the same as they
    // always did: this just puts a frame on screen before that block
    // starts instead of leaving the window whatever the OS painted it as
    // (usually blank/black) for the whole duration.
    {
        const Texture2D& splash = TextureManager::get("sprites/gui/titleIntroLogo.png");
        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle source      = {0.0f, 0.0f, static_cast<float>(splash.width), static_cast<float>(splash.height)};
        Rectangle destination = {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
        DrawTexturePro(splash, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        EndDrawing();
    }

    SetTargetFPS(settings.target_fps);

    audio.initialize();
    ui::load_translations(); // before FontManager::get() below - the font bakes their glyphs
    ui::set_language(settings.language);
    ui::set_sound_callback([this](ui::SoundEvent event) {
        if (event == ui::SoundEvent::Click) audio.play_ui_click();
        else if (event == ui::SoundEvent::Hover) audio.play_ui_hover();
    });
    FontManager::get(); // load the game's text font up front, while the splash is up

    // Needs a GL context for its textures/shaders, so only now.
    game = std::make_unique<GameEngine>(settings, audio);

    // No DisableCursor() here - the app starts on MainMenu, which needs a
    // visible, clickable cursor. enter_state() disables it only when
    // actually transitioning into Playing.
}

Application::~Application()
{
    // First, while the window and GL context still exist: saves whatever
    // world is still open (quitting the app outright - the OS window-close
    // control - has no earlier hook than this) and frees its GPU resources.
    game.reset();

    EnableCursor();
    ui::set_sound_callback({});
    release_pause_snapshot();
    TextureManager::unload_all();
    FontManager::unload();
    audio.shutdown();
    CloseWindow();
}

void Application::run()
{
    while (!WindowShouldClose() && !quit_requested) {
        float delta_time = GetFrameTime();
        if (IsWindowResized()) {
            settings.window_width = GetScreenWidth();
            settings.window_height = GetScreenHeight();
            SettingsIO::save(settings);
        }
        audio.update(delta_time, settings, state == GameState::Playing);
        ui::set_scale_level(settings.ui_scale);
        ui::set_language(settings.language);
        ui::begin_frame();
        if (state == GameState::Playing) {
            update_and_draw_game(delta_time);
        } else {
            update_and_draw_menu();
        }
    }
}

void Application::update_and_draw_game(float delta_time)
{
    game->update_frame(delta_time);
    BeginDrawing();
    game->draw();
    EndDrawing();

    if (game->take_pause_request()) {
        release_pause_snapshot();
        pause_snapshot = game->take_pause_snapshot();
        enter_state(GameState::Paused);
    }
}

void Application::update_and_draw_menu()
{
    BeginDrawing();
    ClearBackground(Color{24, 24, 28, 255}); // fallback - covered by one of the two backgrounds below unless the pause snapshot is missing

    // Pause and its settings share the frozen world; pre-game screens use
    // the dark dirt pattern.
    const bool over_world = state == GameState::Paused ||
        (state == GameState::Settings && settings_return_state == GameState::Paused);
    if (over_world && IsTextureValid(pause_snapshot)) {
        DrawTexturePro(pause_snapshot,
            {0.0f, 0.0f, static_cast<float>(pause_snapshot.width), static_cast<float>(pause_snapshot.height)},
            {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
            {0.0f, 0.0f}, 0.0f, WHITE);
        if (state == GameState::Settings)
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 105});
    } else if (!over_world) {
        ui::menu_background();
    }

    switch (state) {
        case GameState::MainMenu: {
            switch (main_menu_screen.update()) {
                case MainMenuScreen::Action::Singleplayer:
                    enter_state(GameState::WorldList);
                    break;
                case MainMenuScreen::Action::Settings:
                    settings_return_state = GameState::MainMenu;
                    enter_state(GameState::Settings);
                    break;
                case MainMenuScreen::Action::Quit:
                    quit_requested = true;
                    break;
                default: break; // None, or Multiplayer (the button is disabled - never actually returned)
            }
            break;
        }
        case GameState::WorldList: {
            WorldListScreen::Action action = world_list_screen.update();
            if (action.type == WorldListScreen::ActionType::LoadWorld) {
                pending_world_folder = action.folder_name;
            } else if (action.type == WorldListScreen::ActionType::CreateWorld) {
                enter_state(GameState::WorldCreate);
            } else if (action.type == WorldListScreen::ActionType::Back) {
                enter_state(GameState::MainMenu);
            }
            break;
        }
        case GameState::WorldCreate: {
            WorldCreateScreen::Action action = world_create_screen.update();
            if (action.type == WorldCreateScreen::ActionType::Create) {
                WorldSave::create_world(action.world);
                pending_world_folder = action.world.folder_name;
            } else if (action.type == WorldCreateScreen::ActionType::Cancel) {
                enter_state(GameState::WorldList);
            }
            break;
        }
        case GameState::Settings: {
            if (settings_screen.update(settings).type == SettingsScreen::ActionType::Back) {
                enter_state(settings_return_state); // MainMenu, or Paused if opened via the in-game Esc menu
            }
            break;
        }
        case GameState::Paused: {
            switch (pause_menu_screen.update()) {
                case PauseMenuScreen::Action::Resume:
                    enter_state(GameState::Playing);
                    break;
                case PauseMenuScreen::Action::Settings:
                    settings_return_state = GameState::Paused;
                    enter_state(GameState::Settings);
                    break;
                case PauseMenuScreen::Action::MainMenu:
                    return_to_main_menu();
                    break;
                default: break;
            }
            break;
        }
        case GameState::Playing:
            break; // unreachable - run() only calls this method when state != Playing
    }

    EndDrawing();

    // Only now, with this menu frame finished - loading draws frames of
    // its own (the loading screen), which can't nest inside this one.
    if (pending_world_folder) {
        std::string folder = *pending_world_folder;
        pending_world_folder.reset();
        start_singleplayer_world(folder);
    }
}

void Application::enter_state(GameState new_state)
{
    state = new_state;
    if (state == GameState::Settings) settings_screen.enter();
    if (state == GameState::Playing) {
        release_pause_snapshot();
        DisableCursor(); // mouse-look needs the cursor captured
    } else {
        if (state == GameState::MainMenu) release_pause_snapshot();
        EnableCursor(); // every menu screen needs a visible, clickable cursor
        if (state == GameState::WorldList) world_list_screen.enter();
        if (state == GameState::WorldCreate) world_create_screen.enter();
    }
}

void Application::start_singleplayer_world(const std::string& folder_name)
{
    loading_screen.reset();
    last_loading_frame_time = 0.0;
    const char* title_key = "loading.generating";
    auto draw_loading_frame = [this, &title_key](bool generating, WorldLoadStage stage, float progress) {
        // A world with no saved player yet has never been played - it's
        // being generated from scratch, not loaded back.
        title_key = generating ? "loading.generating" : "loading.loading";
        // At most 60 frames a second: EndDrawing() waits out the target
        // frame time, so drawing after every single chunk would slow the
        // load itself down.
        const double now = GetTime();
        if (now - last_loading_frame_time < 1.0 / 60.0 && progress < 1.0f) return;
        last_loading_frame_time = now;
        const char* stage_key = stage == WorldLoadStage::Terrain ? "loading.terrain"
            : stage == WorldLoadStage::Lighting ? "loading.lighting" : "loading.meshes";
        BeginDrawing();
        ClearBackground(BLACK);
        loading_screen.draw(ui::tr(title_key), ui::tr(stage_key), progress);
        EndDrawing();
    };
    if (!game->open_world(folder_name, draw_loading_frame)) return; // shouldn't happen - stay on the current screen

    // The bar fills slower than a fast load finishes - let it visibly
    // reach 100% before switching to the world.
    while (!loading_screen.finished() && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);
        loading_screen.draw(ui::tr(title_key), ui::tr("loading.meshes"), 1.0f);
        EndDrawing();
    }

    enter_state(GameState::Playing);
}

void Application::return_to_main_menu()
{
    game->close_world();
    enter_state(GameState::MainMenu);
}

void Application::release_pause_snapshot()
{
    if (!IsTextureValid(pause_snapshot)) return;
    rlDrawRenderBatchActive(); // it may still be referenced by this frame's pending draw calls
    UnloadTexture(pause_snapshot);
    pause_snapshot = {};
}
