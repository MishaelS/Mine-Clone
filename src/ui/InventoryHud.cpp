#include "ui/InventoryHud.hpp"
#include "ui/Widgets.hpp"

#include "raylib.h"

#include <cmath>

namespace {
    constexpr float SLOT_SIZE = 52.0f;
    constexpr float SLOT_SPACING = 6.0f;
    constexpr float HOTBAR_BOTTOM_MARGIN = 18.0f;

    constexpr int GRID_COLUMNS = 6;
    constexpr float GRID_CELL_SIZE = 76.0f;
    constexpr float GRID_CELL_SPACING = 10.0f;
    constexpr int GRID_LABEL_FONT_SIZE = 14;
    constexpr float GRID_PANEL_PADDING = 24.0f;
    constexpr float CLOSE_BUTTON_WIDTH = 160.0f;
    constexpr float CLOSE_BUTTON_HEIGHT = 44.0f;
    constexpr int TITLE_FONT_SIZE = 30;

    constexpr Color HOTBAR_PANEL_COLOR = {20, 20, 24, 200};
    constexpr Color GRID_PANEL_COLOR = {20, 20, 24, 235};
}

void InventoryHud::draw_hotbar(const Inventory& inventory) const
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    float bar_width = HOTBAR_SIZE * SLOT_SIZE + (HOTBAR_SIZE - 1) * SLOT_SPACING;
    float bar_x = (screen_width - bar_width) / 2.0f;
    float bar_y = screen_height - HOTBAR_BOTTOM_MARGIN - SLOT_SIZE;

    ui::panel({bar_x - 6.0f, bar_y - 6.0f, bar_width + 12.0f, SLOT_SIZE + 12.0f}, HOTBAR_PANEL_COLOR);

    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        Rectangle slot_bounds = {bar_x + i * (SLOT_SIZE + SLOT_SPACING), bar_y, SLOT_SIZE, SLOT_SIZE};
        ui::block_button(slot_bounds, inventory.hotbar[i], i == inventory.selected_slot);
    }
}

void InventoryHud::update_grid(Inventory& inventory)
{
    std::vector<BlockType> blocks = all_placeable_blocks();
    int rows = static_cast<int>(std::ceil(static_cast<float>(blocks.size()) / GRID_COLUMNS));

    float grid_width = GRID_COLUMNS * GRID_CELL_SIZE + (GRID_COLUMNS - 1) * GRID_CELL_SPACING;
    float grid_height = rows * GRID_CELL_SIZE + (rows - 1) * GRID_CELL_SPACING;

    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    float panel_width = grid_width + GRID_PANEL_PADDING * 2.0f;
    float panel_height = grid_height + GRID_PANEL_PADDING * 2.0f + 90.0f; // title + close button
    float panel_x = (screen_width - panel_width) / 2.0f;
    float panel_y = (screen_height - panel_height) / 2.0f;

    ui::panel({panel_x, panel_y, panel_width, panel_height}, GRID_PANEL_COLOR);
    ui::label({panel_x, panel_y + 10.0f, panel_width, 40.0f}, "Инвентарь", TITLE_FONT_SIZE, WHITE);

    float grid_x = panel_x + GRID_PANEL_PADDING;
    float grid_y = panel_y + GRID_PANEL_PADDING + 50.0f;

    for (size_t i = 0; i < blocks.size(); ++i) {
        int col = static_cast<int>(i) % GRID_COLUMNS;
        int row = static_cast<int>(i) / GRID_COLUMNS;
        Rectangle cell_bounds = {
            grid_x + col * (GRID_CELL_SIZE + GRID_CELL_SPACING),
            grid_y + row * (GRID_CELL_SIZE + GRID_CELL_SPACING),
            GRID_CELL_SIZE, GRID_CELL_SIZE,
        };

        if (ui::block_button(cell_bounds, blocks[i], false)) {
            inventory.hotbar[inventory.selected_slot] = blocks[i];
            close();
            return; // `blocks`/layout no longer matter once we've closed
        }

        ui::label({cell_bounds.x - 6.0f, cell_bounds.y + GRID_CELL_SIZE + 2.0f, GRID_CELL_SIZE + 12.0f, 16.0f},
                   get_block_name(blocks[i]), GRID_LABEL_FONT_SIZE, LIGHTGRAY);
    }

    Rectangle close_bounds = {
        panel_x + (panel_width - CLOSE_BUTTON_WIDTH) / 2.0f,
        panel_y + panel_height - GRID_PANEL_PADDING - CLOSE_BUTTON_HEIGHT,
        CLOSE_BUTTON_WIDTH, CLOSE_BUTTON_HEIGHT,
    };
    if (ui::button(close_bounds, "Закрыть")) {
        close();
    }
}
