#pragma once

#include "core/Block.hpp"
#include "player/Inventory.hpp"
#include "ui/Widgets.hpp"

#include <optional>

// The in-game inventory UI: a 9-slot hotbar always drawn during play and a
// 27-slot storage panel toggled by GameEngine's E-key handling, or opened
// by right-clicking a Workbench/Furnace/Chest block (see
// container_kind_for_block() below). Owned by GameEngine, not tied to
// GameState - it's an overlay within Playing, the same way the debug
// overlay is.
class InventoryHud {
public:
    // Which container GUI panel is currently showing -
    // assets/sprites/gui/container/container<N>.png. All four share the
    // exact same player storage+hotbar grid position (verified against
    // each texture's own art - see MAIN_GRID_ORIGIN/INVENTORY_HOTBAR_ORIGIN
    // in the .cpp), so only the background texture (and, for Furnace, the
    // two progress icons) actually varies by kind.
    enum class ContainerKind { Inventory, Workbench, Furnace, Chest };

    bool is_open() const { return open; }

    // E-key toggle - always the plain survival inventory.
    void toggle(Inventory& inventory);
    void close(Inventory& inventory);

    // Right-click-on-block interaction (GameEngine::update()): opens the
    // given container. Only meant to be called while nothing is open yet -
    // GameEngine gates that with its own is_open() check first, same as it
    // already does before the E-key toggle.
    void open_container(ContainerKind kind);

    // Always called during Playing - draws the 9 slots at the bottom of
    // the screen with `inventory.selected_slot` highlighted. No input
    // handling here: number-key slot selection is simple enough that
    // GameEngine::update() just sets inventory.selected_slot directly.
    void draw_hotbar(const Inventory& inventory) const;

    // Draws storage + the mirrored hotbar row and handles drag-and-drop
    // movement between every slot, plus Q to drop one item out of whatever
    // slot the mouse is currently hovering (only while nothing's being
    // dragged - GameEngine spawns the actual DroppedItem for whatever this
    // returns, since only it knows the player's position/aim). For
    // Workbench/Furnace/Chest, the container's own special slots (crafting
    // grid, furnace input/fuel/output, chest storage) are drawn as pure
    // background decoration, not interactive slots - this project has no
    // crafting, smelting, or per-chest storage system yet (same reasoning
    // the plain survival inventory's own crafting/armor cells were already
    // left inert for).
    std::optional<ItemStack> update_grid(Inventory& inventory);

private:
    bool open = false;
    ContainerKind kind = ContainerKind::Inventory;
    ItemStack carried_stack;
    ItemStack* drag_source = nullptr;
    ui::Tooltip tooltip;
};

// Workbench opens ContainerKind::Workbench, Furnace/LitFurnace both open
// ContainerKind::Furnace (they differ only in whether the block is lit),
// Chest opens ContainerKind::Chest - std::nullopt for every other block
// (including Air, i.e. nothing targeted), meaning "not a container, handle
// the click as a normal block placement instead".
std::optional<InventoryHud::ContainerKind> container_kind_for_block(BlockType type);
