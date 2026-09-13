#include "core/Settings.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {
    constexpr const char* SETTINGS_PATH = SAVE_DATA_PATH "settings.json";

    const char* filter_json_value(TextureFilterMode mode) {
        return mode == TextureFilterMode::Bilinear ? "bilinear" : "point";
    }

    TextureFilterMode filter_from_json_value(const std::string& value) {
        return value == "bilinear" ? TextureFilterMode::Bilinear : TextureFilterMode::Point;
    }

    const char* binding_kind_json_value(BindingKind kind) {
        return kind == BindingKind::MouseButton ? "mouse" : "key";
    }

    BindingKind binding_kind_from_json_value(const std::string& value) {
        return value == "mouse" ? BindingKind::MouseButton : BindingKind::Key;
    }

    std::string read_whole_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
}

namespace SettingsIO {

Settings load() {
    Settings settings; // defaults (including default_keybindings()) if anything below fails

    std::string text = read_whole_file(SETTINGS_PATH);
    if (text.empty()) return settings;

    try {
        Json root = Json::parse(text);
        settings.render_distance_chunks = static_cast<int>(root["render_distance_chunks"].as_number(settings.render_distance_chunks));
        settings.fog_distance_blocks    = static_cast<int>(root["fog_distance_blocks"].as_number(settings.fog_distance_blocks));
        settings.texture_filter         = filter_from_json_value(root["texture_filter"].as_string(filter_json_value(settings.texture_filter)));
        settings.target_fps             = static_cast<int>(root["target_fps"].as_number(settings.target_fps));
        settings.window_width           = std::clamp(static_cast<int>(root["window_width"].as_number(settings.window_width)), 960, 3840);
        settings.window_height          = std::clamp(static_cast<int>(root["window_height"].as_number(settings.window_height)), 540, 2160);
        settings.ui_scale               = std::clamp(static_cast<int>(root["ui_scale"].as_number(settings.ui_scale)), 1, 4);
        settings.language               = root["language"].as_string(settings.language) == "en" ? "en" : "ru";
        settings.master_volume          = static_cast<int>(root["master_volume"].as_number(settings.master_volume));
        settings.effects_volume         = static_cast<int>(root["effects_volume"].as_number(settings.effects_volume));
        settings.ambient_volume         = static_cast<int>(root["ambient_volume"].as_number(settings.ambient_volume));
        settings.music_volume           = static_cast<int>(root["music_volume"].as_number(settings.music_volume));

        const Json& keybindings_json = root["keybindings"];
        for (size_t i = 0; i < settings.keybindings.size(); ++i) {
            GameAction action = static_cast<GameAction>(i);
            const Json& entry = keybindings_json[game_action_json_key(action)];
            Binding& binding = settings.keybindings[i];
            binding.kind = binding_kind_from_json_value(entry["kind"].as_string(binding_kind_json_value(binding.kind)));
            binding.code = static_cast<int>(entry["code"].as_number(binding.code));
        }
    } catch (const std::exception&) {
        return Settings{}; // corrupted file - fall back to full defaults rather than a half-applied mix
    }

    return settings;
}

bool save(const Settings& settings)
{
    std::ofstream out(SETTINGS_PATH, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"render_distance_chunks\": " << settings.render_distance_chunks << ",\n";
    out << "  \"fog_distance_blocks\": " << settings.fog_distance_blocks << ",\n";
    out << "  \"texture_filter\": \"" << filter_json_value(settings.texture_filter) << "\",\n";
    out << "  \"target_fps\": " << settings.target_fps << ",\n";
    out << "  \"window_width\": " << settings.window_width << ",\n";
    out << "  \"window_height\": " << settings.window_height << ",\n";
    out << "  \"ui_scale\": " << settings.ui_scale << ",\n";
    out << "  \"language\": \"" << settings.language << "\",\n";
    out << "  \"master_volume\": " << settings.master_volume << ",\n";
    out << "  \"effects_volume\": " << settings.effects_volume << ",\n";
    out << "  \"ambient_volume\": " << settings.ambient_volume << ",\n";
    out << "  \"music_volume\": " << settings.music_volume << ",\n";
    out << "  \"keybindings\": {\n";
    for (size_t i = 0; i < settings.keybindings.size(); ++i) {
        const Binding& binding = settings.keybindings[i];
        out << "    \"" << game_action_json_key(static_cast<GameAction>(i)) << "\": "
            << "{ \"kind\": \"" << binding_kind_json_value(binding.kind) << "\", \"code\": " << binding.code << " }"
            << (i + 1 < settings.keybindings.size() ? ",\n" : "\n");
    }
    out << "  }\n";
    out << "}\n";

    return static_cast<bool>(out);
}

}
