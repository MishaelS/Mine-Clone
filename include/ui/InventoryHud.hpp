#pragma once

#include "core/Block.hpp"
#include "player/Inventory.hpp"
#include "ui/Widgets.hpp"

#include <array>
#include <optional>
#include <cstdint>

enum class GameMode : uint8_t;
class World;

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
    // already does before the E-key toggle. `x`/`y`/`z` are the targeted
    // block's own world position - unused for Workbench/Furnace/Inventory,
    // but for Chest it's what update_grid() looks up World::chest_inventory()
    // with, so a right-clicked chest actually shows *that* chest's own
    // contents rather than some other one.
    void open_container(ContainerKind kind, int x = 0, int y = 0, int z = 0);

    // Always called during Playing - draws the 9 slots at the bottom of
    // the screen with `inventory.selected_slot` highlighted. No input
    // handling here: number-key slot selection is simple enough that
    // GameEngine::update() just sets inventory.selected_slot directly.
    void draw_hotbar(const Inventory& inventory) const;

    // The row of 10 hearts above the hotbar (Survival only - GameEngine
    // never calls this in Creative, matching real Minecraft hiding its own
    // health/hunger bars there). `health`/`max_health` are PlayerHealth's
    // own half-heart units - see assets/sprites/gui/hearts/heart{0,1,2}.png
    // for the full/half/empty frames this steps through per heart.
    void draw_hearts(int health, int max_health) const;

    // Creative shows its paged unlimited block catalog. Survival draws
    // storage + mirrored hotbar and implements vanilla-style left/right
    // click, half stacks, Shift quick-move, double-click gather, number-key
    // swaps and Q/Shift+Q drops. GameEngine spawns the returned DroppedItem.
    // Inventory/Workbench also get a real crafting grid (see Recipe.hpp).
    // Chest gets its own 27-slot storage, read from `world` via
    // World::chest_inventory() at whatever position open_container() was
    // last called with - `world` may be null only in states this is never
    // actually called from (chest slots just render empty then). Furnace's
    // own input/fuel/output slots are still pure background decoration -
    // this project has no smelting simulation yet.
    std::optional<ItemStack> update_grid(Inventory& inventory, GameMode game_mode, World* world);

private:
    bool open = false;
    ContainerKind kind = ContainerKind::Inventory;
    // Only meaningful while kind == Chest - the world position
    // open_container() was last called with, i.e. which chest's own
    // World::chest_inventory() update_grid() should read/mutate.
    int chest_x = 0, chest_y = 0, chest_z = 0;
    ItemStack carried_stack;

    // Real crafting grids - Inventory's own small 2x2 (always available) and
    // the Workbench's 3x3 (see Recipe.hpp). Furnace/Chest have no crafting
    // grid of their own, so these two just sit unused (and stay empty -
    // update_grid() never lets carried_stack land in the wrong container's
    // grid) whenever `kind` is neither.
    std::array<ItemStack, 4> inventory_craft_grid{};
    std::array<ItemStack, 9> workbench_craft_grid{};
    int creative_page = 0;
    double last_click_time = -1.0;
    ItemStack* last_clicked_slot = nullptr;
    ui::Tooltip tooltip;
};

// Workbench opens ContainerKind::Workbench, Furnace/LitFurnace both open
// ContainerKind::Furnace (they differ only in whether the block is lit),
// Chest opens ContainerKind::Chest - std::nullopt for every other block
// (including Air, i.e. nothing targeted), meaning "not a container, handle
// the click as a normal block placement instead".
std::optional<InventoryHud::ContainerKind> container_kind_for_block(BlockType type);
