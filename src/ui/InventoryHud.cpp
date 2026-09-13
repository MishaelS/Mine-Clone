#include "ui/InventoryHud.hpp"
#include "ui/Widgets.hpp"
#include "ui/FontManager.hpp"
#include "core/TextureManager.hpp"
#include "player/Item.hpp"

#include "raylib.h"

#include <algorithm>
#include <utility>

namespace {
    const char* HOTBAR_TEXTURE_PATH = "sprites/gui/hotbar.png";
    const char* HOTBAR_SELECTOR_TEXTURE_PATH = "sprites/gui/hotbarSelector.png";

    const char* container_texture_path(InventoryHud::ContainerKind kind)
    {
        switch (kind) {
            case InventoryHud::ContainerKind::Workbench: return "sprites/gui/container/container1.png";
            case InventoryHud::ContainerKind::Furnace:   return "sprites/gui/container/container2.png";
            case InventoryHud::ContainerKind::Chest:     return "sprites/gui/container/container3.png";
            case InventoryHud::ContainerKind::Inventory: default: return "sprites/gui/container/container0.png";
        }
    }

    // The furnace's fire/arrow icons - see the comment on ContainerKind::
    // Furnace's handling in update_grid(). No smelting simulation exists
    // to drive these, so they're always drawn at their full, un-clipped
    // size: the flame at full height (fuel not about to run out), the
    // arrow fully across (an in-progress smelt) - "100%", same static
    // sub-images real Minecraft itself draws and would only ever clip
    // shorter as fuel burns down or smelting progresses.
    const char* FUEL_PROGRESS_TEXTURE_PATH = "sprites/gui/container/fuelProgress.png";
    const char* SMELTING_PROGRESS_TEXTURE_PATH = "sprites/gui/container/smeltingProgress.png";
    // Positions in container2.png's own texture-pixel space, measured
    // directly off the art (the panel already has the arrow and a smoke-
    // squiggle placeholder baked into its background right where these
    // two icons belong).
    constexpr Vector2 FUEL_PROGRESS_ORIGIN = {57.0f, 36.0f};
    constexpr Vector2 SMELTING_PROGRESS_ORIGIN = {79.0f, 34.0f};

    constexpr float HOTBAR_SCALE = 3.0f;
    constexpr float INVENTORY_SCALE = 2.0f;

    constexpr float ITEM_SIZE_PX = 16.0f;
    constexpr float ITEM_OFFSET_PX = -0.5f;
    constexpr float CONTAINER_SLOT_SIZE_PX = 18.0f;
    constexpr float ICON_MARGIN_PX = 1.0f;

    // Slot grid origins within the texture, in texture pixels - see the
    // Minecraft Wiki's own documented layout for this exact asset (verified
    // against the actual file: 9 columns starting at x=8, each row/column
    // stride exactly CONTAINER_SLOT_SIZE_PX with no gap between slots). Only the main
    // storage grid and hotbar row are ever drawn on top of; the crafting
    // grid/output slot and armor column near the top of the texture are
    // left as pure background decoration - this project has no crafting or
    // armor system yet; those cells are deliberately left as background.
    constexpr Vector2 MAIN_GRID_ORIGIN = {8.0f, 84.0f};
    constexpr int MAIN_GRID_COLUMNS = 9;
    constexpr Vector2 INVENTORY_HOTBAR_ORIGIN = {8.0f, 142.0f};

    // hotbar.png uses the vanilla HUD layout: nine 20px cells inside a
    // 182x22 texture. Item artwork starts 3px from the texture's top-left.
    constexpr float HOTBAR_SLOT_STRIDE_PX = 20.0f;
    constexpr Vector2 HOTBAR_ICON_ORIGIN = {3.0f, 3.0f};
    constexpr Vector2 HOTBAR_SELECTOR_ORIGIN = {-1.0f, -1.0f};

    constexpr float HOTBAR_BOTTOM_MARGIN = 18.0f;

    constexpr Color SELECTION_HIGHLIGHT_COLOR = {255, 255, 255, 235};
    constexpr int STACK_COUNT_FONT_SIZE = 13;

    constexpr float DURABILITY_BAR_HEIGHT_PX = 1.0f;
    constexpr float DURABILITY_BAR_MARGIN_PX = 1.0f;
    constexpr Color DURABILITY_BAR_BACKGROUND = {0, 0, 0, 180};

    void draw_item_stack(Vector2 icon_origin, const ItemStack& stack, float scale)
    {
        if (stack.empty()) return;

        float icon_size = ITEM_SIZE_PX * scale;
        float offset = ITEM_OFFSET_PX * scale;
        Rectangle bounds = {icon_origin.x + offset, icon_origin.y + offset, icon_size, icon_size};

        if (stack.is_tool()) {
            ui::item_icon(bounds, stack.tool);

            // Tools never stack (always count 1), so there's no count
            // badge to draw - a thin durability bar under the icon
            // instead, same as real Minecraft's own tool slots. Hidden at
            // full durability, same as vanilla hides it on a fresh tool.
            const ItemProperties& properties = get_item_properties(stack.tool);
            if (properties.max_durability > 0 && stack.durability < properties.max_durability) {
                float fraction = std::clamp(
                    static_cast<float>(stack.durability) / static_cast<float>(properties.max_durability), 0.0f, 1.0f);
                float margin = DURABILITY_BAR_MARGIN_PX * scale;
                float bar_height = DURABILITY_BAR_HEIGHT_PX * scale;
                float bar_x = bounds.x + margin;
                float bar_y = bounds.y + bounds.height - margin - bar_height;
                float bar_width = bounds.width - margin * 2.0f;
                Color fill_color = {
                    static_cast<unsigned char>(255.0f * (1.0f - fraction)),
                    static_cast<unsigned char>(255.0f * fraction),
                    0, 255,
                };
                DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                               static_cast<int>(bar_width), static_cast<int>(bar_height), DURABILITY_BAR_BACKGROUND);
                DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                               static_cast<int>(bar_width * fraction), static_cast<int>(bar_height), fill_color);
            }
            return;
        }

        ui::block_icon(bounds, stack.block);

        if (stack.count > 1) {
            std::string count = std::to_string(stack.count);
            const Font& font = FontManager::get();
            Vector2 size = MeasureTextEx(font, count.c_str(), STACK_COUNT_FONT_SIZE, 1.0f);
            Vector2 pos = {bounds.x + bounds.width - size.x, bounds.y + bounds.height - size.y};
            DrawTextEx(font, count.c_str(), {pos.x + 1.0f, pos.y + 1.0f}, STACK_COUNT_FONT_SIZE, 1.0f, BLACK);
            DrawTextEx(font, count.c_str(), pos, STACK_COUNT_FONT_SIZE, 1.0f, WHITE);
        }
    }

    void draw_slot_highlight(Vector2 slot_origin, float scale)
    {
        float size = CONTAINER_SLOT_SIZE_PX * scale;
        DrawRectangleLinesEx({slot_origin.x, slot_origin.y, size, size}, scale * 0.5f, SELECTION_HIGHLIGHT_COLOR);
    }

}

void InventoryHud::toggle(Inventory& inventory)
{
    if (open) close(inventory);
    else {
        kind = ContainerKind::Inventory;
        open = true;
    }
}

void InventoryHud::open_container(ContainerKind new_kind)
{
    kind = new_kind;
    open = true;
}

void InventoryHud::close(Inventory& inventory)
{
    if (!carried_stack.empty()) {
        if (drag_source && drag_source->empty()) {
            *drag_source = carried_stack;
        } else {
            inventory.put_back(carried_stack);
        }
        carried_stack.clear();
    }
    drag_source = nullptr;
    open = false;
}

void InventoryHud::draw_hotbar(const Inventory& inventory) const
{
    const Texture2D& texture = TextureManager::get(HOTBAR_TEXTURE_PATH);
    const Texture2D& selector = TextureManager::get(HOTBAR_SELECTOR_TEXTURE_PATH);

    float hotbar_w = static_cast<float>(texture.width) * HOTBAR_SCALE;
    float hotbar_h = static_cast<float>(texture.height) * HOTBAR_SCALE;
    float hotbar_x = (GetScreenWidth() - hotbar_w) / 2.0f;
    float hotbar_y = GetScreenHeight() - HOTBAR_BOTTOM_MARGIN - hotbar_h;

    Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, source, {hotbar_x, hotbar_y, hotbar_w, hotbar_h}, {0.0f, 0.0f}, 0.0f, WHITE);

    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        Vector2 icon_origin = {
            hotbar_x + (HOTBAR_ICON_ORIGIN.x + i * HOTBAR_SLOT_STRIDE_PX) * HOTBAR_SCALE,
            hotbar_y + HOTBAR_ICON_ORIGIN.y * HOTBAR_SCALE,
        };
        draw_item_stack(icon_origin, inventory.hotbar[i], HOTBAR_SCALE);
    }

    if (inventory.selected_slot >= 0 && inventory.selected_slot < HOTBAR_SIZE) {
        float selector_size = static_cast<float>(selector.width) * HOTBAR_SCALE;
        float selector_x = hotbar_x +
            (HOTBAR_SELECTOR_ORIGIN.x + inventory.selected_slot * HOTBAR_SLOT_STRIDE_PX) * HOTBAR_SCALE;
        float selector_y = hotbar_y + HOTBAR_SELECTOR_ORIGIN.y * HOTBAR_SCALE;
        Rectangle selector_source = {
            0.0f, 0.0f, static_cast<float>(selector.width), static_cast<float>(selector.height)
        };
        DrawTexturePro(selector, selector_source,
            {selector_x, selector_y, selector_size, selector_size},
            {0.0f, 0.0f}, 0.0f, WHITE);
    }
}

std::optional<ItemStack> InventoryHud::update_grid(Inventory& inventory)
{
    tooltip.clear();

    const Texture2D& texture = TextureManager::get(container_texture_path(kind));

    float panel_w = static_cast<float>(texture.width) * INVENTORY_SCALE;
    float panel_h = static_cast<float>(texture.height) * INVENTORY_SCALE;
    float panel_x = (GetScreenWidth() - panel_w) / 2.0f;
    float panel_y = (GetScreenHeight() - panel_h) / 2.0f;

    Rectangle full_source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, full_source, {panel_x, panel_y, panel_w, panel_h}, {0.0f, 0.0f}, 0.0f, WHITE);

    if (kind == ContainerKind::Furnace) {
        const Texture2D& fuel = TextureManager::get(FUEL_PROGRESS_TEXTURE_PATH);
        const Texture2D& smelting = TextureManager::get(SMELTING_PROGRESS_TEXTURE_PATH);
        auto draw_icon = [&](const Texture2D& icon, Vector2 origin) {
            Rectangle icon_source = {0.0f, 0.0f, static_cast<float>(icon.width), static_cast<float>(icon.height)};
            Rectangle icon_destination = {
                panel_x + origin.x * INVENTORY_SCALE, panel_y + origin.y * INVENTORY_SCALE,
                static_cast<float>(icon.width) * INVENTORY_SCALE, static_cast<float>(icon.height) * INVENTORY_SCALE,
            };
            DrawTexturePro(icon, icon_source, icon_destination, {0.0f, 0.0f}, 0.0f, WHITE);
        };
        draw_icon(fuel, FUEL_PROGRESS_ORIGIN);
        draw_icon(smelting, SMELTING_PROGRESS_ORIGIN);
    }

    Vector2 mouse = GetMousePosition();
    bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    ItemStack* hovered_slot = nullptr;

    auto process_slot = [&](ItemStack& stack, Vector2 slot_origin) {
        float size = CONTAINER_SLOT_SIZE_PX * INVENTORY_SCALE;
        float inset = ICON_MARGIN_PX * INVENTORY_SCALE;
        draw_item_stack({slot_origin.x + inset, slot_origin.y + inset}, stack, INVENTORY_SCALE);

        if (CheckCollisionPointRec(mouse, {slot_origin.x, slot_origin.y, size, size})) {
            hovered_slot = &stack;
            draw_slot_highlight(slot_origin, INVENTORY_SCALE);
            if (!stack.empty()) {
                tooltip.show(stack.is_tool() ? get_item_properties(stack.tool).display_name : get_block_name(stack.block), mouse);
            }
            if (pressed && carried_stack.empty() && !stack.empty()) {
                carried_stack = stack;
                stack.clear();
                drag_source = &stack;
            }
        }
    };

    for (int i = 0; i < INVENTORY_STORAGE_SIZE; ++i) {
        int col = i % MAIN_GRID_COLUMNS;
        int row = i / MAIN_GRID_COLUMNS;
        Vector2 slot_origin = {
            panel_x + (MAIN_GRID_ORIGIN.x + col * CONTAINER_SLOT_SIZE_PX) * INVENTORY_SCALE,
            panel_y + (MAIN_GRID_ORIGIN.y + row * CONTAINER_SLOT_SIZE_PX) * INVENTORY_SCALE,
        };
        process_slot(inventory.storage[i], slot_origin);
    }

    // This is the same data as the always-visible hotbar, not a copy.
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        Vector2 slot_origin = {
            panel_x + (INVENTORY_HOTBAR_ORIGIN.x + i * CONTAINER_SLOT_SIZE_PX) * INVENTORY_SCALE,
            panel_y + INVENTORY_HOTBAR_ORIGIN.y * INVENTORY_SCALE,
        };
        process_slot(inventory.hotbar[i], slot_origin);
    }

    if (released && !carried_stack.empty()) {
        if (!hovered_slot || hovered_slot == drag_source) {
            *drag_source = carried_stack;
        } else if (hovered_slot->empty()) {
            *hovered_slot = carried_stack;
        } else if (!hovered_slot->is_tool() && !carried_stack.is_tool() && hovered_slot->block == carried_stack.block) {
            int moved = std::min(carried_stack.count, MAX_ITEM_STACK - hovered_slot->count);
            hovered_slot->count += moved;
            carried_stack.count -= moved;
            if (carried_stack.count > 0) *drag_source = carried_stack;
        } else {
            *drag_source = *hovered_slot;
            *hovered_slot = carried_stack;
        }
        carried_stack.clear();
        drag_source = nullptr;
    }

    if (!carried_stack.empty()) {
        float size = ITEM_SIZE_PX * INVENTORY_SCALE;
        draw_item_stack({mouse.x - size / 2.0f, mouse.y - size / 2.0f}, carried_stack, INVENTORY_SCALE);
    }

    // Draw as the final inventory layer so later block slots cannot cover it.
    tooltip.draw();

    // Q drops one item out of whatever's hovered - only while nothing's
    // actively being dragged (carried_stack empty), same as real
    // Minecraft's own inventory screen gates it. GameEngine turns the
    // returned stack into an actual DroppedItem thrown out in front of the
    // player; this class has no notion of world position to spawn one
    // itself.
    std::optional<ItemStack> dropped;
    if (hovered_slot && carried_stack.empty() && !hovered_slot->empty() && IsKeyPressed(KEY_Q)) {
        dropped = take_one_item(*hovered_slot);
    }

    // No separate close button - same as real Minecraft's own inventory
    // screen, E or Escape closes it (GameEngine::update() already handles
    // both independently of this grid).
    return dropped;
}

std::optional<InventoryHud::ContainerKind> container_kind_for_block(BlockType type)
{
    switch (type) {
        case BlockType::Workbench: return InventoryHud::ContainerKind::Workbench;
        case BlockType::Furnace:
        case BlockType::LitFurnace: return InventoryHud::ContainerKind::Furnace;
        case BlockType::Chest: return InventoryHud::ContainerKind::Chest;
        default: return std::nullopt;
    }
}
