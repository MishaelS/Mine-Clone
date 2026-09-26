#pragma once

#include "ui/Widgets.hpp"
#include "worldgen/CustomWorld.hpp"

#include <string>
#include <unordered_map>

// "Настроить..." page of the world creation screen for WorldType::Custom:
// one text field with the layer recipe (checked live as you type), then a
// row per CUSTOM_FEATURES entry - an on/off button and a chance slider -
// grouped under Structures/Biomes and scrolled with the mouse wheel when
// they don't all fit. New features show up here without touching this
// screen (see CustomFeature).
class CustomWorldScreen {
public:
    enum class Action { None, Done, Cancel };

    // Starts editing a copy of `current` - call each time the page opens.
    void enter(const CustomWorld& current);

    Action update();

    // The edited settings, layers in canonical form - valid after Done.
    const CustomWorld& result() const { return editing; }

private:
    void refresh_parse();

    CustomWorld editing;
    ui::TextInputState layers_field;
    std::string parsed_text; // layers_field.text as of the last refresh_parse()
    LayerParseResult parsed;
    // Block names as shown in the current language, lower-cased, so
    // "3 земля" works as well as "3 dirt".
    std::unordered_map<std::string, BlockType> translated_names;
    int first_row = 0; // feature rows scrolled off the top
};
