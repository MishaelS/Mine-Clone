#pragma once

#include "core/Keybindings.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class BlockType : uint8_t;
enum class ItemType : uint8_t;

namespace ui {

// Localization. Screens deal only in stable keys ("main.play",
// "block.oak_planks", ...); the text itself lives in
// assets/translations/<code>.json - one file per language, its file name
// (without .json) is the language code saved in settings.json:
//
//   { "language_name": "Deutsch", "strings": { "main.quit": "...", ... } }
//
// Dropping a new file into that folder is all it takes to add a language:
// it appears in Settings > Language on the next launch. Any key a file
// leaves out falls back to English, so a partial translation still works.

struct LanguageInfo {
    std::string code; // file name without .json, e.g. "ru"
    std::string name; // the file's own "language_name", shown in Settings
};

// Loads every assets/translations/*.json. Call once at startup, before the
// font is loaded (see translation_codepoints()) and before set_language().
// A malformed file is skipped with a warning rather than stopping the game.
void load_translations();

// Every loaded language, sorted by code.
const std::vector<LanguageInfo>& available_languages();

// Every character used anywhere in the loaded translations, so the font can
// bake glyphs for a newly added language's own alphabet too (FontManager).
std::vector<int> translation_codepoints();

// Switches the active language; an unknown code falls back to Russian (the
// game's default), or the first loaded language if even that is missing.
void set_language(const std::string& language_code);
const std::string& language();

// The active language's text for `key` -> English -> the key itself.
const std::string& tr(std::string_view key);

// Display text only; persistent block/item IDs remain language-independent.
// An untranslated block/item falls back to a readable form of its ID.
std::string block_display_name(BlockType type);
std::string item_display_name(ItemType type);

// Settings > Controls captions.
std::string game_action_display_name(GameAction action);
std::string binding_display_name(const Binding& binding);

}
