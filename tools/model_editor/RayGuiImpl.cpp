// The model editor's own copy of raygui's implementation (the game compiles
// its own in src/ui/RayGuiImpl.cpp - the two are separate executables),
// plus raygui's bundled dark theme.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "dark/style_dark.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void editor_load_dark_style()
{
    GuiLoadStyleDark();
}
