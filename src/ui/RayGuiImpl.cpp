// raygui is a single-header library (like stb_*.h): its declarations are
// included as plain C++ from any translation unit, but the actual function
// bodies only get compiled where RAYGUI_IMPLEMENTATION is defined first -
// exactly once across the whole link, here. Widgets.cpp includes raygui.h
// too (for GuiButton/GuiTextBox/GuiSlider et al.) without that macro.
#if defined(__GNUC__) || defined(__clang__)
// Third-party-only noise (unused params/dead helper in controls this
// project never calls, e.g. GuiColorPicker) under -Wall -Wextra -Wpedantic -
// same reasoning as raymath.h's suppression in CMakeLists.txt, scoped to
// just this translation unit instead of loosening the project's own flags.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
