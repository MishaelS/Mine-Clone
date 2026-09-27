#pragma once

#include "raylib.h"

#include <initializer_list>
#include <string>
#include <string_view>

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

    // The game's own font with every glyph the loaded translations use
    // (Cyrillic included). Valid after load(), until unload().
    const Font& font();
    void unload();
}
