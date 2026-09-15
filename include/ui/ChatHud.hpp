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

    // Draws the scrollback log (only while open - there's no lingering
    // fade-out once closed the way vanilla's own chat has) and the entry
    // box, and drains this frame's typed characters/Enter/click-away into
    // it - called from GameEngine::draw(), the same immediate-mode spot
    // InventoryHud's own update_grid() is called from. Returns the
    // submitted line once Enter or a click away confirms it (chat already
    // closed by then), std::nullopt every other frame - including the
    // frame a blank box gets confirmed, which just closes with nothing to
    // process.
    std::optional<std::string> update_and_draw();

    // Appends one already-formatted line to the scrollback, trimming the
    // oldest past a fixed cap - used for the player's own echoed messages
    // and every command reply/error, "unknown command" included.
    void push_message(const std::string& text);

private:
    bool open = false;
    ui::TextInputState input;
    std::vector<std::string> messages;
};
