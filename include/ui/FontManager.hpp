#pragma once

#include "raylib.h"

// The single font every on-screen UI element draws with (the debug
// overlay today; any future HUD/UI text later), so the whole game reads with
// one consistent typeface instead of raylib's built-in default font.
namespace FontManager {
    // Loads (on first call) and returns assets/fonts/Minecraft Rus/minecraft.ttf,
    // with glyphs for basic Latin and Cyrillic (the font is Latin+Cyrillic,
    // for Russian text). Needs a GL context (call after InitWindow).
    const Font& get();

    // Frees the font. Call once before CloseWindow.
    void unload();
}
