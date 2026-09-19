#include "ui/Localization.hpp"
#include "core/Block.hpp"
#include "core/Json.hpp"
#include "player/Item.hpp"

#include "raylib.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace {
    struct Language {
        std::string name;
        std::unordered_map<std::string, std::string> strings;
    };

    constexpr const char* TRANSLATIONS_DIRECTORY = ASSETS_PATH "translations";
    constexpr const char* DEFAULT_LANGUAGE = "ru";
    constexpr const char* FALLBACK_LANGUAGE = "en";

    std::map<std::string, Language> languages; // keyed (and so sorted) by code
    std::vector<ui::LanguageInfo> language_list;
    std::string current_code;
    const Language* current = nullptr;
    const Language* fallback = nullptr;

    // tr() hands out references, so a key with no text in any language is
    // remembered here to return itself by reference.
    std::unordered_map<std::string, std::string> untranslated;

    const std::string* find(const Language* language, const std::string& key)
    {
        if (language == nullptr) return nullptr;
        auto found = language->strings.find(key);
        return found != language->strings.end() ? &found->second : nullptr;
    }

    const std::string* lookup(const std::string& key)
    {
        if (const std::string* text = find(current, key)) return text;
        return find(fallback, key);
    }

    // A block/item with no translation anywhere still gets a readable name
    // ("oak_planks" -> "Oak Planks"); its persistent ID is never modified.
    std::string content_name(const std::string& key, const std::string& id)
    {
        if (const std::string* text = lookup(key)) return *text;
        std::string readable = id;
        bool word_start = true;
        for (char& c : readable) {
            if (c == '_') { c = ' '; word_start = true; }
            else { if (word_start && c >= 'a' && c <= 'z') c -= 'a' - 'A'; word_start = false; }
        }
        return readable;
    }
}

namespace ui {
    void load_translations()
    {
        languages.clear();
        language_list.clear();
        untranslated.clear();
        current = fallback = nullptr;
        current_code.clear();

        namespace fs = std::filesystem;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(TRANSLATIONS_DIRECTORY, error)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            const std::string code = entry.path().stem().string();
            char* text = LoadFileText(entry.path().string().c_str());
            if (text == nullptr) continue;
            try {
                Json root = Json::parse(text);
                Language language;
                language.name = root["language_name"].as_string(code);
                for (const auto& [key, value] : root["strings"].as_object()) {
                    if (value.get_type() == Json::Type::String) language.strings[key] = value.as_string();
                }
                languages[code] = std::move(language);
            } catch (const std::exception& e) {
                // A translation file is user-editable content - one broken
                // file shouldn't stop the game from starting.
                TraceLog(LOG_WARNING, "translations/%s.json skipped: %s", code.c_str(), e.what());
            }
            UnloadFileText(text);
        }
        if (languages.empty()) {
            throw std::runtime_error("No translation files found in " ASSETS_PATH "translations/");
        }

        for (const auto& [code, language] : languages) language_list.push_back({code, language.name});
        auto found = languages.find(FALLBACK_LANGUAGE);
        fallback = found != languages.end() ? &found->second : nullptr;
    }

    const std::vector<LanguageInfo>& available_languages() { return language_list; }

    std::vector<int> translation_codepoints()
    {
        std::set<int> unique;
        for (const auto& [code, language] : languages) {
            std::string all_text = language.name;
            for (const auto& [key, text] : language.strings) all_text += text;
            int count = 0;
            int* codepoints = LoadCodepoints(all_text.c_str(), &count);
            unique.insert(codepoints, codepoints + count);
            UnloadCodepoints(codepoints);
        }
        return {unique.begin(), unique.end()};
    }

    void set_language(const std::string& language_code)
    {
        if (language_code == current_code && current != nullptr) return; // called every frame
        auto found = languages.find(language_code);
        if (found == languages.end()) found = languages.find(DEFAULT_LANGUAGE);
        if (found == languages.end()) found = languages.begin();
        current_code = found->first;
        current = &found->second;
    }

    const std::string& language() { return current_code; }

    const std::string& tr(std::string_view key)
    {
        const std::string key_string(key);
        if (const std::string* text = lookup(key_string)) return *text;
        return untranslated.try_emplace(key_string, key_string).first->second;
    }

    std::string block_display_name(BlockType type)
    {
        const std::string& id = get_block_name(type);
        return content_name("block." + id, id);
    }

    std::string item_display_name(ItemType type)
    {
        const std::string id = type == ItemType::None ? "none" : get_item_name(type);
        return content_name("item." + id, id);
    }

    std::string game_action_display_name(GameAction action)
    {
        return tr(std::string("action.") + game_action_json_key(action));
    }

    std::string binding_display_name(const Binding& binding)
    {
        if (binding.kind == BindingKind::MouseButton) {
            switch (binding.code) {
                case MOUSE_BUTTON_LEFT  : return tr("mouse.left");
                case MOUSE_BUTTON_RIGHT : return tr("mouse.right");
                case MOUSE_BUTTON_MIDDLE: return tr("mouse.middle");
                default                 : return tr("mouse.other");
            }
        }
        switch (binding.code) {
            case KEY_SPACE       : return tr("key.space");
            case KEY_UP          : return tr("key.up");
            case KEY_DOWN        : return tr("key.down");
            case KEY_LEFT        : return tr("key.left");
            case KEY_RIGHT       : return tr("key.right");
            case KEY_LEFT_SHIFT  : return "Shift";
            case KEY_LEFT_CONTROL: return "Ctrl";
            case KEY_LEFT_ALT    : return "Alt";
            case KEY_TAB         : return "Tab";
            default: break;
        }
        // Printable ASCII keys - raylib's KeyboardKey values for these match
        // their own ASCII codepoint.
        if (binding.code >= 32 && binding.code < 127) return std::string(1, static_cast<char>(binding.code));

        std::string text = tr("key.other");
        size_t slot = text.find("{0}");
        if (slot != std::string::npos) text.replace(slot, 3, std::to_string(binding.code));
        return text;
    }
}
