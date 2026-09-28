#include "EditorWidgets.hpp"
#include "EditorStyle.hpp"
#include "EditorText.hpp"

#include "raygui.h"

namespace {
    Color gui_color(int control, int property) {
        return GetColor(static_cast<unsigned int>(GuiGetStyle(control, property)));
    }
}

namespace editor_ui {

    float draw_hint(float x, float y, float width, const std::string& text, bool error)
    {
        const Color color = error ? Color{230, 90, 80, 255} : Fade(gui_color(DEFAULT, TEXT_COLOR_NORMAL), 0.7f);
        std::string line, word;
        auto flush = [&] {
            DrawTextEx(editor_text::font(), line.c_str(), {x, y + 2}, 14, 1, color);
            y += 18;
            line.clear();
        };
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != ' ') {
                word += text[i];
                continue;
            }
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && MeasureTextEx(editor_text::font(), candidate.c_str(), 14, 1).x > width) {
                flush();
                line = word;
            } else {
                line = candidate;
            }
            word.clear();
        }
        if (!line.empty()) flush();
        return y + 2;
    }

    std::string block_display_name(const std::string& name)
    {
        const std::string key = "block." + name;
        const std::string& text = editor_text::tr(key);
        return text == key ? name : text;
    }

    bool fold_header(float x, float& y, float width, const std::string& title, std::set<std::string>& folded)
    {
        using namespace editor_style;
        const Rectangle bar = {x, y, width, ROW};
        const bool open = folded.count(title) == 0;
        const bool hovered = !GuiIsLocked() && CheckCollisionPointRec(GetMousePosition(), bar);
        DrawRectangleRec(bar, hovered ? Fade(WHITE, 0.10f) : Fade(WHITE, 0.05f));
        DrawRectangleRec({bar.x, bar.y, 3, bar.height}, open ? SELECTION : Fade(SELECTION, 0.45f));
        // The arrow: pointing down while open, right while folded.
        const float cx = bar.x + 16, cy = bar.y + bar.height * 0.5f;
        const Color arrow = gui_color(DEFAULT, TEXT_COLOR_NORMAL);
        if (open) DrawTriangle({cx - 5, cy - 3}, {cx, cy + 3}, {cx + 5, cy - 3}, arrow);
        else DrawTriangle({cx - 3, cy - 5}, {cx - 3, cy + 5}, {cx + 3, cy}, arrow);
        DrawTextEx(editor_text::font(), title.c_str(), {bar.x + 30, bar.y + 5}, 16, 1, arrow);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (open) folded.insert(title);
            else folded.erase(title);
        }
        y += ROW + GAP;
        return folded.count(title) == 0;
    }

} // namespace editor_ui
