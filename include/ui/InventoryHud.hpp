#pragma once

#include "core/Inventory.hpp"

// The in-game HUD half of the Creative-style inventory: a 9-slot hotbar
// always drawn during play, plus a simple grid picker (every block
// blocks.json defines) toggled by GameEngine's own E-key handling. Owned
// by GameEngine, not tied to GameState - it's an overlay within Playing,
// the same way the debug overlay is.
class InventoryHud {
public:
    bool is_open() const { return open; }
    void toggle() { open = !open; }
    void close() { open = false; }

    // Always called during Playing - draws the 9 slots at the bottom of
    // the screen with `inventory.selected_slot` highlighted. No input
    // handling here: number-key slot selection is simple enough that
    // GameEngine::update() just sets inventory.selected_slot directly.
    void draw_hotbar(const Inventory& inventory) const;

    // Only meaningful while is_open() - draws the centered block grid and
    // handles clicks in the same call (the same immediate-mode pattern
    // every menu screen already uses). Clicking a block assigns it to
    // inventory.hotbar[inventory.selected_slot] and closes the picker.
    void update_grid(Inventory& inventory);

private:
    bool open = false;
};
