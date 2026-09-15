#include "ui/ChatHud.hpp"

#include "raylib.h"

#include <algorithm>

namespace {
    constexpr size_t MAX_MESSAGES = 50;         // full scrollback cap - oldest lines fall off the front
    constexpr size_t MAX_VISIBLE_MESSAGES = 10; // how many of the most recent are actually drawn at once
    constexpr size_t MAX_CHAT_CODEPOINTS = 256;

    constexpr float CHAT_WIDTH = 480.0f;
    constexpr float LINE_HEIGHT = 16.0f;
    constexpr float INPUT_HEIGHT = 22.0f;
    constexpr float BOTTOM_MARGIN = 4.0f;
    constexpr float LEFT_MARGIN = 4.0f;
    constexpr Color LOG_BACKGROUND = {0, 0, 0, 140};
}

void ChatHud::open_chat()
{
    open = true;
    input.text.clear();
    input.max_codepoints = MAX_CHAT_CODEPOINTS;
    // Flush this same keystroke's own typed character out of raylib's
    // queue - GetCharPressed() (drained inside ui::text_input()) would
    // otherwise hand the fresh box the very 'T' that opened it as if it
    // had been typed in.
    while (GetCharPressed() != 0) {}
    EnableCursor();
}

void ChatHud::open_command()
{
    open_chat();
    input.text = "/";
}

void ChatHud::close()
{
    open = false;
    input.text.clear();
    DisableCursor();
}

std::optional<std::string> ChatHud::update_and_draw()
{
    if (!open) return std::nullopt;

    float width = ui::scaled(CHAT_WIDTH);
    float line_height = ui::scaled(LINE_HEIGHT);
    float input_height = ui::scaled(INPUT_HEIGHT);
    float x = ui::scaled(LEFT_MARGIN);
    float input_y = static_cast<float>(GetScreenHeight()) - ui::scaled(BOTTOM_MARGIN) - input_height;

    size_t visible = std::min(messages.size(), MAX_VISIBLE_MESSAGES);
    if (visible > 0) {
        float log_height = static_cast<float>(visible) * line_height;
        float log_y = input_y - log_height;
        ui::panel({x, log_y, width, log_height}, LOG_BACKGROUND);
        for (size_t i = 0; i < visible; ++i) {
            const std::string& line = messages[messages.size() - visible + i];
            ui::label({x, log_y + static_cast<float>(i) * line_height, width, line_height},
                      line, WHITE, ui::TextAlign::Left);
        }
    }

    // See RESULT_PRESSED in raygui's own GuiTextBox: for a non-multiline
    // box this fires on Enter *or* a click outside `bounds` - both read as
    // "done typing, submit" here, there's nothing else on screen a click
    // while chat is open would mean instead.
    bool submitted = ui::text_input({x, input_y, width, input_height}, input, true);
    if (!submitted) return std::nullopt;

    std::string text = input.text;
    close();
    if (text.empty()) return std::nullopt;
    return text;
}

void ChatHud::push_message(const std::string& text)
{
    messages.push_back(text);
    if (messages.size() > MAX_MESSAGES) {
        messages.erase(messages.begin(), messages.begin() + static_cast<long>(messages.size() - MAX_MESSAGES));
    }
}
