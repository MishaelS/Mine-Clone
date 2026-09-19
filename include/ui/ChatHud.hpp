#pragma once

#include "ui/Widgets.hpp"

#include <optional>
#include <string>
#include <vector>

// The in-game chat/command line - a scrollback log plus one text entry
// box, opened with T (empty) or "/" (pre-filled with the slash), submitted
// with Enter or by clicking away, cancelled with Escape. Single-player
// only for now: a plain message just echoes straight back into the log
// under the player's own name (there's no one else here to send it to
// yet), and a "/"-prefixed line is handed to GameEngine::
// execute_chat_command() instead - but that message/command split, and the
// scrollback it all lands in, is exactly what a future multiplayer
// connection would hang real network chat off of rather than this local
// echo.
class ChatHud {
public:
    bool is_open() const { return open; }

    // T - empty box. GameEngine gates this on the inventory screen being
    // closed first (only one modal input surface at a time) and shows the
    // OS cursor while open (EnableCursor(), undone by close()) - the text
    // box widget's own click-to-position-caret and click-away-to-submit
    // both need a real, moving cursor position to mean anything.
    void open_chat();

    // "/" - same as open_chat(), just pre-filled with a leading "/", the
    // same shortcut real Minecraft's own chat gives for jumping straight
    // into a command instead of typing the slash by hand.
    void open_command();

    // Escape - discards whatever was typed without submitting it.
    void close();

    // Draws the visible chat history even when the entry box is closed:
    // recent lines linger and fade out like vanilla Minecraft's HUD, while
    // the full latest scrollback stays opaque whenever chat is open. While
    // open it also owns typed characters/Enter/click-away and returns the
    // submitted line once confirmed.
    std::optional<std::string> update_and_draw();

    // Commands shown as vanilla-like suggestions while the user types a
    // "/" line. Each string may include usage/description after the first
    // token; matching and Tab completion use that first command token.
    void set_command_suggestions(std::vector<std::string> suggestions);

    // Appends one already-formatted line to the scrollback, trimming the
    // oldest past a fixed cap - used for the player's own echoed messages
    // and every command reply/error, "unknown command" included.
    void push_message(const std::string& text);

private:
    struct Message {
        std::string text;
        double created_at = 0.0;
    };

    std::vector<std::string> matching_command_suggestions() const;
    void draw_messages(float x, float bottom_y, float width, float line_height) const;
    void draw_command_suggestions(float x, float input_y, float width, float line_height,
                                  const std::vector<std::string>& suggestions) const;

    bool open = false;
    ui::TextInputState input;
    std::vector<Message> messages;
    std::vector<std::string> command_suggestions;
};
