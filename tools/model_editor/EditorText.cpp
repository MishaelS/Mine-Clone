#include "EditorText.hpp"
#include "core/Json.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

    std::unordered_map<std::string, std::string> strings;  // selected language
    std::unordered_map<std::string, std::string> fallback; // English
    std::unordered_map<std::string, std::string> missing;  // keys shown as themselves
    Font loaded_font{};

    std::string read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    void load_language(const std::string& code, std::unordered_map<std::string, std::string>& into) {
        std::string text = read_file(std::string(ASSETS_PATH) + "translations/" + code + ".json");
        if (text.empty()) return;
        try {
            Json root = Json::parse(text);
            for (const auto& [key, value] : root["strings"].as_object()) {
                if (value.get_type() == Json::Type::String) into[key] = value.as_string();
            }
        } catch (const std::exception&) {
            TraceLog(LOG_WARNING, "model editor: translations/%s.json could not be read", code.c_str());
        }
    }
}

namespace editor_text {

    void load() {
        std::string language = "ru";
        std::string settings = read_file(std::string(SAVE_DATA_PATH) + "settings.json");
        if (!settings.empty()) {
            try {
                language = Json::parse(settings)["language"].as_string(language);
            } catch (const std::exception&) {
            }
        }
        load_language("en", fallback);
        load_language(language, strings);

        // Every character the translations use, plus plain ASCII for
        // numbers and typed-in names.
        std::set<int> codepoints;
        for (int c = 32; c < 127; ++c) codepoints.insert(c);
        for (const auto* table : {&strings, &fallback}) {
            for (const auto& [key, value] : *table) {
                int count = 0;
                int* points = LoadCodepoints(value.c_str(), &count);
                codepoints.insert(points, points + count);
                UnloadCodepoints(points);
            }
        }
        for (int c = 0x410; c <= 0x44F; ++c) codepoints.insert(c); // Cyrillic, for typed names
        std::vector<int> list(codepoints.begin(), codepoints.end());
        loaded_font = LoadFontEx(ASSETS_PATH "fonts/Minecraft Rus/minecraft.ttf", 32, list.data(),
                                 static_cast<int>(list.size()));
        SetTextureFilter(loaded_font.texture, TEXTURE_FILTER_BILINEAR);
    }

    const std::string& tr(std::string_view key) {
        std::string key_string(key);
        if (auto found = strings.find(key_string); found != strings.end()) return found->second;
        if (auto found = fallback.find(key_string); found != fallback.end()) return found->second;
        return missing.try_emplace(key_string, key_string).first->second;
    }

    std::string tr_format(std::string_view key, std::initializer_list<std::string> args) {
        std::string text = tr(key);
        size_t index = 0;
        for (const std::string& arg : args) {
            const std::string slot = "{" + std::to_string(index++) + "}";
            for (size_t at = text.find(slot); at != std::string::npos; at = text.find(slot, at + arg.size())) {
                text.replace(at, slot.size(), arg);
            }
        }
        return text;
    }

    const Font& font() { return loaded_font; }

    void unload() {
        if (loaded_font.texture.id != 0) UnloadFont(loaded_font);
        loaded_font = {};
    }
}
