#include "EditorText.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

    std::string selected_language = "ru";
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
        selected_language = language;
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

    namespace {
        std::string translations_path(const std::string& language) {
            return std::string(ASSETS_PATH) + "translations/" + language + ".json";
        }

        // Every language's strings as its file has them - read once, kept
        // in step by set_translation().
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& file_strings() {
            static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tables;
            return tables;
        }

        std::string json_string(const std::string& text) {
            std::string out = "\"";
            for (char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                if (c == '\n') { out += "\\n"; continue; }
                out += c;
            }
            return out + "\"";
        }
    }

    const std::vector<std::string>& languages() {
        static std::vector<std::string> found;
        if (!found.empty()) return found;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(std::string(ASSETS_PATH) + "translations", error)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") found.push_back(entry.path().stem().string());
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    std::string translation(const std::string& language, const std::string& key) {
        auto& tables = file_strings();
        auto table = tables.find(language);
        if (table == tables.end()) {
            table = tables.emplace(language, std::unordered_map<std::string, std::string>{}).first;
            load_language(language, table->second);
        }
        const auto found = table->second.find(key);
        return found == table->second.end() ? std::string() : found->second;
    }

    bool set_translation(const std::string& language, const std::string& key, const std::string& value) {
        const std::string path = translations_path(language);
        std::vector<std::string> lines;
        {
            std::istringstream text(read_file(path));
            for (std::string line; std::getline(text, line);) lines.push_back(line);
        }
        if (lines.empty()) return false;
        // One "key": "value" a line - the files' own layout.
        const std::string quoted = "\"" + key + "\":";
        const std::string prefix = "\"" + key.substr(0, key.find('.') + 1);
        auto key_line = [&](const std::string& line) {
            const size_t start = line.find_first_not_of(' ');
            return start == std::string::npos ? std::string() : line.substr(start);
        };
        int existing = -1, last_prefixed = -1, strings_open = -1, last_entry = -1;
        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            const std::string trimmed = key_line(lines[static_cast<size_t>(i)]);
            if (trimmed.rfind(quoted, 0) == 0) existing = i;
            if (trimmed.rfind(prefix, 0) == 0) last_prefixed = i;
            if (trimmed.rfind("\"strings\":", 0) == 0) strings_open = i;
            if (strings_open >= 0 && i > strings_open && trimmed.rfind("\"", 0) == 0) last_entry = i;
        }
        const std::string entry = "    " + json_string(key) + ": " + json_string(value);
        if (existing >= 0) {
            std::string& line = lines[static_cast<size_t>(existing)];
            const bool comma = !line.empty() && line.back() == ',';
            line = entry + (comma ? "," : "");
        } else {
            const int after = last_prefixed >= 0 ? last_prefixed : strings_open;
            if (after < 0) return false;
            std::string& previous = lines[static_cast<size_t>(after)];
            // After the last entry: that one gets the comma, this one none.
            const bool previous_is_last = after == last_entry;
            if (after == strings_open) {
                lines.insert(lines.begin() + after + 1, entry + (last_entry >= 0 ? "," : ""));
            } else {
                if (previous_is_last && previous.back() != ',') previous += ",";
                lines.insert(lines.begin() + after + 1, entry + (previous_is_last ? "" : ","));
            }
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        for (const std::string& line : lines) out << line << "\n";
        if (!out) return false;

        translation(language, key); // makes sure the table is loaded
        file_strings()[language][key] = value;
        if (language == selected_language) strings[key] = value;
        if (language == "en") fallback[key] = value;
        return true;
    }

    const Font& font() { return loaded_font; }

    void unload() {
        if (loaded_font.texture.id != 0) UnloadFont(loaded_font);
        loaded_font = {};
    }
}
