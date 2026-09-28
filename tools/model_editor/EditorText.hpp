#pragma once

#include "raylib.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// The model editor's text: every word it shows comes from the game's own
// assets/translations/<language>.json ("editor.*" keys), in the language
// picked in the game's settings.json - English as the fallback, the key
// itself as the last resort. Kept separate from the game's ui::Localization,
// which pulls in the whole block/item content registry the editor doesn't
// need.
namespace editor_text {
    void load();
    const std::string& tr(std::string_view key);
    std::string tr_format(std::string_view key, std::initializer_list<std::string> args);

    // Every language the game has a translations file for ("en", "ru"), sorted.
    const std::vector<std::string>& languages();
    // `key` as assets/translations/<language>.json has it - "" if it doesn't.
    std::string translation(const std::string& language, const std::string& key);
    // Puts `key` = `value` into that file - replacing its line, or added
    // after the last key with the same prefix ("block.") - and into what
    // tr() shows. False if the file couldn't be written.
    bool set_translation(const std::string& language, const std::string& key, const std::string& value);

    // The game's own font with every glyph the loaded translations use
    // (Cyrillic included). Valid after load(), until unload().
    const Font& font();
    void unload();
}
