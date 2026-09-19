#include "ui/ChatHud.hpp"

#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace {
    constexpr size_t MAX_MESSAGES         = 50;  // full scrollback cap - oldest lines fall off the front
    constexpr size_t MAX_VISIBLE_MESSAGES = 10;  // how many of the most recent are actually drawn at once
    constexpr size_t MAX_VISIBLE_SUGGESTIONS = 6;
    constexpr size_t MAX_CHAT_CODEPOINTS  = 256;

    constexpr float CHAT_WIDTH    = 480.0f;
    constexpr float LINE_HEIGHT   = 16.0f;
    constexpr float INPUT_HEIGHT  = 22.0f;
    constexpr float BOTTOM_MARGIN = 4.0f;
    constexpr float LEFT_MARGIN   = 4.0f;
    constexpr double CHAT_LINGER_SECONDS = 10.0;
    constexpr double CHAT_FADE_SECONDS   = 2.0;
    constexpr unsigned char ROW_ALPHA = 150;
    constexpr unsigned char INPUT_ROW_ALPHA = 185;
    constexpr unsigned char TEXT_ALPHA = 255;

    std::string ascii_lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string first_token(const std::string& text) {
        size_t end = text.find_first_of(" \t");
        return end == std::string::npos ? text : text.substr(0, end);
    }

    bool starts_with(const std::string& text, const std::string& prefix) {
        return prefix.size() <= text.size() && std::equal(prefix.begin(), prefix.end(), text.begin());
    }

    unsigned char scaled_alpha(unsigned char alpha, float fade) {
        return static_cast<unsigned char>(std::round(static_cast<float>(alpha) * std::clamp(fade, 0.0f, 1.0f)));
    }
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
    float x = ui::scaled(LEFT_MARGIN);
    float width = std::min(ui::scaled(CHAT_WIDTH), static_cast<float>(GetScreenWidth()) - x * 2.0f);
    float line_height = ui::scaled(LINE_HEIGHT);
    float input_height = ui::scaled(INPUT_HEIGHT);
    float input_y = static_cast<float>(GetScreenHeight()) - ui::scaled(BOTTOM_MARGIN) - input_height;

    std::vector<std::string> suggestions = open ? matching_command_suggestions() : std::vector<std::string>{};
    if (open && IsKeyPressed(KEY_TAB) && !suggestions.empty()) {
        input.text = first_token(suggestions.front()) + " ";
        suggestions = matching_command_suggestions();
    }

    float suggestion_gap = suggestions.empty() ? 0.0f : ui::scaled(2.0f);
    float suggestion_height = static_cast<float>(suggestions.size()) * line_height;
    float log_bottom = open ? input_y - suggestion_height - suggestion_gap : input_y + input_height;
    draw_messages(x, log_bottom, width, line_height);

    if (!open) return std::nullopt;

    if (!suggestions.empty()) {
        draw_command_suggestions(x, input_y, width, line_height, suggestions);
    }

    DrawRectangleRec({x, input_y, width, input_height}, Color{0, 0, 0, INPUT_ROW_ALPHA});

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

void ChatHud::set_command_suggestions(std::vector<std::string> suggestions)
{
    command_suggestions = std::move(suggestions);
}

std::vector<std::string> ChatHud::matching_command_suggestions() const
{
    if (input.text.empty() || input.text[0] != '/') return {};
    std::string typed = input.text.substr(1);
    if (typed.find_first_of(" \t") != std::string::npos) return {};

    std::string prefix = ascii_lower(typed);
    std::vector<std::string> result;
    for (const std::string& suggestion : command_suggestions) {
        std::string token = first_token(suggestion);
        if (!token.empty() && token[0] == '/') token.erase(token.begin());
        if (!starts_with(ascii_lower(token), prefix)) continue;
        result.push_back(suggestion);
        if (result.size() >= MAX_VISIBLE_SUGGESTIONS) break;
    }
    return result;
}

void ChatHud::draw_messages(float x, float bottom_y, float width, float line_height) const
{
    std::vector<const Message*> visible;
    double now = GetTime();
    for (auto it = messages.rbegin(); it != messages.rend() && visible.size() < MAX_VISIBLE_MESSAGES; ++it) {
        if (!open && now - it->created_at > CHAT_LINGER_SECONDS) continue;
        visible.push_back(&*it);
    }
    if (visible.empty()) return;
    std::reverse(visible.begin(), visible.end());

    float y = bottom_y - static_cast<float>(visible.size()) * line_height;
    for (size_t i = 0; i < visible.size(); ++i) {
        const Message& message = *visible[i];
        float fade = 1.0f;
        if (!open) {
            double age = now - message.created_at;
            double fade_start = CHAT_LINGER_SECONDS - CHAT_FADE_SECONDS;
            if (age > fade_start) {
                fade = static_cast<float>((CHAT_LINGER_SECONDS - age) / CHAT_FADE_SECONDS);
            }
        }
        Rectangle row{x, y + static_cast<float>(i) * line_height, width, line_height};
        DrawRectangleRec(row, Color{0, 0, 0, scaled_alpha(ROW_ALPHA, fade)});
        ui::label(row, message.text, Color{255, 255, 255, scaled_alpha(TEXT_ALPHA, fade)}, ui::TextAlign::Left);
    }
}

void ChatHud::draw_command_suggestions(float x, float input_y, float width, float line_height,
                                       const std::vector<std::string>& suggestions) const
{
    float y = input_y - static_cast<float>(suggestions.size()) * line_height - ui::scaled(2.0f);
    for (size_t i = 0; i < suggestions.size(); ++i) {
        Rectangle row{x, y + static_cast<float>(i) * line_height, width, line_height};
        DrawRectangleRec(row, Color{0, 0, 0, INPUT_ROW_ALPHA});
        Color color = i == 0 ? Color{255, 255, 160, 255} : Color{210, 210, 210, 255};
        ui::label(row, suggestions[i], color, ui::TextAlign::Left);
    }
}

void ChatHud::push_message(const std::string& text)
{
    messages.push_back(Message{text, GetTime()});
    if (messages.size() > MAX_MESSAGES) {
        messages.erase(messages.begin(), messages.begin() + static_cast<long>(messages.size() - MAX_MESSAGES));
    }
}
