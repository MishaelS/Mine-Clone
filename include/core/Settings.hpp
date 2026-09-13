#pragma once

#include "core/Keybindings.hpp"

#include <array>
#include <cstdint>
#include <string>

// Point = today's hardcoded TEXTURE_FILTER_POINT (crisp/blocky); Bilinear
// smooths the block atlas's texels together, a common "de-blockified" look.
enum class TextureFilterMode : uint8_t { Point, Bilinear };

// Every user-configurable setting, persisted as one flat settings.json at
// the repo root (see SettingsIO below) - GameEngine owns exactly one of
// these for its whole lifetime, edited in place by SettingsScreen.
struct Settings {
    std::array<Binding, static_cast<size_t>(GameAction::Count)> keybindings = default_keybindings();
    int render_distance_chunks = 8;    // today's hardcoded LOADED_RADIUS
    int fog_distance_blocks = 102;     // today's derived default (8 chunks * 16 blocks * 0.8)
    TextureFilterMode texture_filter = TextureFilterMode::Point;
    int target_fps = 60;
    int window_width = 1280;
    int window_height = 720;
    int ui_scale = 1;                 // 1=standard, 2=medium, 3=large, 4=huge
    std::string language = "ru";      // menu localization: "ru" or "en"
    int master_volume = 100;
    int effects_volume = 100;
    int ambient_volume = 70;
    int music_volume = 60;
};

namespace SettingsIO {
    // Reads SAVE_DATA_PATH "settings.json"; returns Settings{} defaults if
    // the file is missing or fails to parse (Json::parse throws on
    // malformed input - a corrupted settings file must never crash the
    // menu, just fall back).
    Settings load();

    // Overwrites SAVE_DATA_PATH "settings.json" with `settings`. Returns
    // false if the file couldn't be written.
    bool save(const Settings& settings);
}
