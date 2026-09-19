#include "ui/InventoryHud.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "ui/FontManager.hpp"
#include "core/TextureManager.hpp"
#include "player/Item.hpp"
#include "player/Recipe.hpp"
#include "player/Smelting.hpp"
#include "core/WorldSave.hpp"
#include "rendering/PlayerRenderer.hpp"
#include "world/World.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>
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
            case InventoryHud::ContainerKind::LargeChest: return "sprites/gui/container/container4.png";
            case InventoryHud::ContainerKind::Inventory:
            default:
                return "sprites/gui/container/container0.png";
        }
    }

    // The furnace's fire/arrow icons - see the comment on ContainerKind::
    // Furnace's handling in update_grid(). No smelting simulation exists
    // to drive these, so they're always drawn at their full, un-clipped
    // size: the flame at full height (fuel not about to run out), the
    // arrow fully across (an in-progress smelt) - "100%", same static
    // sub-images real Minecraft itself draws and would only ever clip
    // shorter as fuel burns down or smelting progresses.
    const char* FUEL_PROGRESS_TEXTURE_PATH     = "sprites/gui/container/fuelProgress.png";
    const char* SMELTING_PROGRESS_TEXTURE_PATH = "sprites/gui/container/smeltingProgress.png";
    // Positions in container2.png's own texture-pixel space, measured
    // directly off the art (the panel already has the arrow and a smoke-
    // squiggle placeholder baked into its background right where these
    // two icons belong).
    constexpr Vector2 FUEL_PROGRESS_ORIGIN     = {57.0f, 36.0f};
    constexpr Vector2 SMELTING_PROGRESS_ORIGIN = {79.0f, 34.0f};
    // The furnace's three slots, measured the same flood-fill way off
    // container2.png: input above the flame, fuel below it, and the big
    // 24x24 output slot right of the arrow.
    constexpr Vector2 FURNACE_INPUT_ORIGIN  = {56.0f, 17.0f};
    constexpr Vector2 FURNACE_FUEL_ORIGIN   = {56.0f, 53.0f};
    constexpr Vector2 FURNACE_OUTPUT_ORIGIN = {112.0f, 31.0f};
    constexpr float   FURNACE_OUTPUT_SIZE_PX = 24.0f;

    constexpr float HOTBAR_SCALE    = 2.0f;
    constexpr float INVENTORY_SCALE = 2.0f;

    constexpr float ITEM_SIZE_PX           = 16.0f;
    constexpr float CONTAINER_SLOT_STRIDE_PX = 18.0f;

    // These origins are the gray INTERIOR of each cell, not its bevel.
    // Interiors are 16x16 with an 18px pitch; confusing size with pitch
    // makes icons and hover overlays spill onto the next cell's border.
    constexpr Vector2 MAIN_GRID_ORIGIN = {8.0f, 84.0f};
    constexpr int MAIN_GRID_COLUMNS = 9;
    constexpr Vector2 INVENTORY_HOTBAR_ORIGIN = {8.0f, 142.0f};

    // Crafting grid/output slot positions - measured directly off each
    // container texture's own baked-in art (flood-filled the slot-grey
    // (139,139,139) regions), same CONTAINER_SLOT_STRIDE_PX stride as every
    // other slot. The output slot itself isn't part of either grid array -
    // it's not a real backing ItemStack, just whatever match_recipe()
    // currently reports for that grid, drawn/handled separately below.
    constexpr Vector2 INVENTORY_CRAFT_ORIGIN         = {98.0f, 18.0f};
    constexpr Vector2 INVENTORY_CRAFT_OUTPUT_ORIGIN  = {154.0f, 28.0f};
    constexpr float   INVENTORY_CRAFT_OUTPUT_SIZE_PX = 16.0f;
    constexpr Vector2 WORKBENCH_CRAFT_ORIGIN         = {30.0f, 17.0f};
    constexpr Vector2 WORKBENCH_CRAFT_OUTPUT_ORIGIN  = {120.0f, 31.0f};
    constexpr float   WORKBENCH_CRAFT_OUTPUT_SIZE_PX = 24.0f;

    // Chest's own 27-slot grid (9x3, same column count/stride as
    // MAIN_GRID_ORIGIN below) - measured the same flood-fill way as every
    // other slot grid in this file.
    constexpr Vector2 CHEST_GRID_ORIGIN = {8.0f, 18.0f};

    // container4.png (large/double chest, 176x222px) is container3.png's
    // own 176x166px art with 3 extra storage rows inserted above the same
    // bottom player-storage+hotbar section, which is why it's exactly 56px
    // (3 rows x CONTAINER_SLOT_STRIDE_PX) taller - its own storage grid
    // starts at the same CHEST_GRID_ORIGIN but spans 6 rows (54 slots)
    // instead of 3, and the player-storage/hotbar section shifts down by
    // that same 56px from MAIN_GRID_ORIGIN/INVENTORY_HOTBAR_ORIGIN below.
    constexpr Vector2 LARGE_CHEST_MAIN_GRID_ORIGIN = {8.0f, 140.0f};
    constexpr Vector2 LARGE_CHEST_HOTBAR_ORIGIN = {8.0f, 198.0f};

    // The player-model preview box - container0.png's own art leaves this
    // rectangle solid black (flood-filled to find these exact bounds),
    // Inventory-only (real Minecraft has no equivalent in the Workbench/
    // Furnace/Chest screens either).
    constexpr Rectangle INVENTORY_PREVIEW_BOX = {26.0f, 8.0f, 49.0f, 70.0f};

    // Fixed resolution for the render-to-texture preview - a little denser
    // than the box's own native pixel size so the 3D model reads smoothly
    // rather than blocky, while staying cheap (a few hundred pixels).
    constexpr int PREVIEW_RENDER_WIDTH = 140;
    constexpr int PREVIEW_RENDER_HEIGHT = 200;

    constexpr float PREVIEW_MOUSE_SENSITIVITY = 0.4f; // screen pixels of mouse offset -> degrees of rotation
    constexpr float PREVIEW_MAX_YAW = 70.0f;
    constexpr float PREVIEW_MAX_PITCH = 25.0f;

    // Renders the player model into a small off-screen texture, rotated to
    // follow the mouse (same idea as real Minecraft's own inventory
    // character - the model turns toward wherever the cursor is, not just
    // while hovering the little preview box itself), then blits that
    // texture into `destination`. The render texture is created once and
    // reused every call - LoadRenderTexture()/UnloadRenderTexture() every
    // frame would be wasteful for something drawn every frame the
    // inventory screen is open.
    void draw_player_preview(Rectangle destination, Vector2 mouse)
    {
        static RenderTexture2D target = LoadRenderTexture(PREVIEW_RENDER_WIDTH, PREVIEW_RENDER_HEIGHT);

        Vector2 box_center = {destination.x + destination.width / 2.0f, destination.y + destination.height / 2.0f};
        float yaw = std::clamp((mouse.x - box_center.x) * PREVIEW_MOUSE_SENSITIVITY, -PREVIEW_MAX_YAW, PREVIEW_MAX_YAW);
        float pitch = std::clamp((mouse.y - box_center.y) * PREVIEW_MOUSE_SENSITIVITY, -PREVIEW_MAX_PITCH, PREVIEW_MAX_PITCH);

        Camera3D camera{};
        camera.position = {0.0f, 1.6f, 4.3f};
        camera.target = {0.0f, 0.9f, 0.0f};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 25.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        BeginTextureMode(target);
        ClearBackground(BLANK);
        BeginMode3D(camera);
        PlayerRenderer{}.draw_flat({0.0f, 0.0f, 0.0f}, yaw, pitch, WHITE);
        EndMode3D();
        EndTextureMode();

        // RenderTexture2D content is stored bottom-up - a negative source
        // height flips it back to normal screen orientation.
        Rectangle source = {0.0f, 0.0f, static_cast<float>(target.texture.width), -static_cast<float>(target.texture.height)};
        DrawTexturePro(target.texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
    }

    // hotbar.png uses the vanilla HUD layout: nine 20px cells inside a
    // 182x22 texture. Item artwork starts 3px from the texture's top-left.
    constexpr float HOTBAR_SLOT_STRIDE_PX = 20.0f;
    constexpr Vector2 HOTBAR_ICON_ORIGIN     = {3.0f, 3.0f};
    constexpr Vector2 HOTBAR_SELECTOR_ORIGIN = {-1.0f, -1.0f};

    constexpr float HOTBAR_BOTTOM_MARGIN = 18.0f;

    // Health row, drawn just above the hotbar - see draw_hearts(). Vanilla's
    // own 9x9 heart icons overlap by 1px at an 8px horizontal stride
    // (rather than sitting edge-to-edge at 9px), which is what gives the
    // row its slightly-tucked-together look instead of visibly separate
    // squares.
    const char* HEART_FULL_TEXTURE_PATH  = "sprites/gui/hearts/heart0.png";
    const char* HEART_HALF_TEXTURE_PATH  = "sprites/gui/hearts/heart1.png";
    const char* HEART_EMPTY_TEXTURE_PATH = "sprites/gui/hearts/heart2.png";
    constexpr float HEART_ICON_PX = 9.0f;
    constexpr float HEART_STRIDE_PX = 8.0f;
    constexpr float HEART_ROW_GAP = 5.0f; // above the hotbar's own top edge

    constexpr Color SELECTION_HIGHLIGHT_COLOR = {255, 255, 255, 80};
    constexpr int STACK_COUNT_FONT_SIZE = 13;

    constexpr float DURABILITY_BAR_HEIGHT_PX = 1.0f;
    constexpr float DURABILITY_BAR_MARGIN_PX = 1.0f;
    constexpr Color DURABILITY_BAR_BACKGROUND = {0, 0, 0, 180};

    Rectangle slot_bounds(Vector2 content_origin, float scale, float size_px = ITEM_SIZE_PX) {
        // Align both edges to the same framebuffer pixels as the atlas.
        const float left = std::round(content_origin.x);
        const float top  = std::round(content_origin.y);
        return {left, top, std::round(content_origin.x + size_px * scale) - left,
                           std::round(content_origin.y + size_px * scale) - top};
    }

    void draw_item_stack(Rectangle cell, const ItemStack& stack, float scale) {
        if (stack.empty()) return;

        const float icon_size = std::min({std::round(ITEM_SIZE_PX * scale), cell.width, cell.height});
        Rectangle bounds = {std::round(cell.x + (cell.width - icon_size) * 0.5f),
                            std::round(cell.y + (cell.height - icon_size) * 0.5f),
                            icon_size, icon_size};

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

        if (stack.is_material()) {
            ui::item_icon(bounds, stack.tool);
        } else {
            ui::block_icon(bounds, stack.block);
        }

        if (stack.count > 1) {
            std::string count = std::to_string(stack.count);
            const Font& font = FontManager::get();
            const float count_font_size = static_cast<float>(ui::scaled_font(STACK_COUNT_FONT_SIZE));
            Vector2 size = MeasureTextEx(font, count.c_str(), count_font_size, 1.0f);
            Vector2 pos = {bounds.x + bounds.width - size.x, bounds.y + bounds.height - size.y};
            DrawTextEx(font, count.c_str(), {pos.x + 1.0f, pos.y + 1.0f}, count_font_size, 1.0f, BLACK);
            DrawTextEx(font, count.c_str(), pos, count_font_size, 1.0f, WHITE);
        }
    }

    void draw_slot_highlight(Rectangle bounds) {
        DrawRectangleRec(bounds, SELECTION_HIGHLIGHT_COLOR);
    }

    std::string stack_name(const ItemStack& stack) {
        return stack.holds_item() ? ui::item_display_name(stack.tool) : ui::block_display_name(stack.block);
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

void InventoryHud::open_container(ContainerKind new_kind, int x, int y, int z)
{
    kind = new_kind;
    chest_x = x;
    chest_y = y;
    chest_z = z;
    open = true;
}

void InventoryHud::close(Inventory& inventory)
{
    if (!carried_stack.empty()) {
        inventory.put_back(carried_stack);
        carried_stack.clear();
    }
    last_clicked_slot = nullptr;
    open = false;
}

void InventoryHud::draw_hotbar(const Inventory& inventory) const
{
    const Texture2D& texture = TextureManager::get(HOTBAR_TEXTURE_PATH);
    const Texture2D& selector = TextureManager::get(HOTBAR_SELECTOR_TEXTURE_PATH);

    const float hotbar_scale = HOTBAR_SCALE * ui::scale_factor();
    float hotbar_w = static_cast<float>(texture.width) * hotbar_scale;
    float hotbar_h = static_cast<float>(texture.height) * hotbar_scale;
    float hotbar_x = std::round((GetScreenWidth() - hotbar_w) / 2.0f);
    float hotbar_y = std::round(GetScreenHeight() - ui::scaled(HOTBAR_BOTTOM_MARGIN) - hotbar_h);

    Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, source, {hotbar_x, hotbar_y, hotbar_w, hotbar_h}, {0.0f, 0.0f}, 0.0f, WHITE);

    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        Vector2 icon_origin = {
            hotbar_x + (HOTBAR_ICON_ORIGIN.x + i * HOTBAR_SLOT_STRIDE_PX) * hotbar_scale,
            hotbar_y + HOTBAR_ICON_ORIGIN.y * hotbar_scale,
        };
        draw_item_stack(slot_bounds(icon_origin, hotbar_scale), inventory.hotbar[i], hotbar_scale);
    }

    if (inventory.selected_slot >= 0 && inventory.selected_slot < HOTBAR_SIZE) {
        float selector_size = static_cast<float>(selector.width) * hotbar_scale;
        float selector_x = hotbar_x +
            (HOTBAR_SELECTOR_ORIGIN.x + inventory.selected_slot * HOTBAR_SLOT_STRIDE_PX) * hotbar_scale;
        float selector_y = hotbar_y + HOTBAR_SELECTOR_ORIGIN.y * hotbar_scale;
        Rectangle selector_source = {
            0.0f, 0.0f, static_cast<float>(selector.width), static_cast<float>(selector.height)
        };
        DrawTexturePro(selector, selector_source,
            {selector_x, selector_y, selector_size, selector_size},
            {0.0f, 0.0f}, 0.0f, WHITE);
    }
}

void InventoryHud::draw_hearts(int health, int max_health) const
{
    const Texture2D& full  = TextureManager::get(HEART_FULL_TEXTURE_PATH);
    const Texture2D& half  = TextureManager::get(HEART_HALF_TEXTURE_PATH);
    const Texture2D& empty = TextureManager::get(HEART_EMPTY_TEXTURE_PATH);

    // Same on-screen position/scale math as draw_hotbar() uses for its own
    // texture, so the row lines up flush with the hotbar's left edge.
    const Texture2D& hotbar_texture = TextureManager::get(HOTBAR_TEXTURE_PATH);
    const float hotbar_scale = HOTBAR_SCALE * ui::scale_factor();
    float hotbar_w = static_cast<float>(hotbar_texture.width) * hotbar_scale;
    float hotbar_h = static_cast<float>(hotbar_texture.height) * hotbar_scale;
    float hotbar_x = std::round((GetScreenWidth() - hotbar_w) / 2.0f);
    float hotbar_y = std::round(GetScreenHeight() - ui::scaled(HOTBAR_BOTTOM_MARGIN) - hotbar_h);

    // Rounded once, like draw_hotbar()'s own hotbar_x/hotbar_y, so the gap
    // above the hotbar comes out to a whole pixel too.
    float heart_size = std::round(HEART_ICON_PX * hotbar_scale);
    float stride = HEART_STRIDE_PX * hotbar_scale;
    float row_y = hotbar_y - heart_size - ui::scaled(HEART_ROW_GAP);

    Rectangle heart_source = {0.0f, 0.0f, HEART_ICON_PX, HEART_ICON_PX};
    int heart_count = max_health / 2;
    for (int i = 0; i < heart_count; ++i) {
        int points = health - i * 2;
        const Texture2D& texture = points >= 2 ? full : points == 1 ? half : empty;
        // slot_bounds() (see draw_item_stack()'s own icons above) snaps
        // both edges to whole pixels independently - at a non-integer
        // hotbar_scale (2.5 by default), HEART_ICON_PX * hotbar_scale is
        // itself fractional (22.5px), and handing that straight to
        // DrawTexturePro() let the GPU round its left/right and top/bottom
        // edges independently, occasionally 1px apart - a 9x9 heart
        // rendering visibly non-square.
        Rectangle destination = slot_bounds({hotbar_x + i * stride, row_y}, hotbar_scale, HEART_ICON_PX);
        DrawTexturePro(texture, heart_source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
    }
}

std::optional<ItemStack> InventoryHud::update_grid(Inventory& inventory, GameMode game_mode, World* world, const Binding& drop_binding)
{
    tooltip.clear();

    if (kind == ContainerKind::Inventory && game_mode == GameMode::Creative) {
        constexpr int columns = 9;
        constexpr int rows = 5;
        const float cell = ui::scaled(42.0f);
        const float padding = ui::scaled(18.0f);
        const float header = ui::scaled(42.0f);
        std::vector<BlockType> blocks = all_placeable_blocks();
        int page_size = columns * rows;
        int page_count = std::max(1, static_cast<int>((blocks.size() + page_size - 1) / page_size));
        creative_page = std::clamp(creative_page, 0, page_count - 1);
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) creative_page = std::clamp(creative_page - static_cast<int>(wheel), 0, page_count - 1);

        float panel_w = columns * cell + padding * 2.0f;
        float panel_h = rows * cell + padding * 2.0f + header;
        float panel_x = (GetScreenWidth() - panel_w) * 0.5f;
        float panel_y = (GetScreenHeight() - panel_h) * 0.5f;
        ui::panel({panel_x, panel_y, panel_w, panel_h}, Color{198, 198, 198, 255});
        ui::label({panel_x, panel_y + ui::scaled(8.0f), panel_w, ui::scaled(24.0f)}, ui::tr("inventory.creative"), Color{45,45,45,255});

        Vector2 mouse = GetMousePosition();
        int begin = creative_page * page_size;
        int end = std::min(begin + page_size, static_cast<int>(blocks.size()));
        for (int i = begin; i < end; ++i) {
            int local = i - begin;
            int col = local % columns;
            int row = local / columns;
            Rectangle slot = {panel_x + padding + col * cell,
                              panel_y + header + padding + row * cell, cell - ui::scaled(4.0f), cell - ui::scaled(4.0f)};
            bool hovered = CheckCollisionPointRec(mouse, slot);
            DrawRectangleRec(slot, Color{139,139,139,255});
            draw_item_stack(slot,
                ItemStack{blocks[static_cast<size_t>(i)], ItemType::None, MAX_ITEM_STACK, 0}, ui::scaled(2.0f));
            if (hovered) {
                draw_slot_highlight(slot);
                tooltip.show(ui::block_display_name(blocks[static_cast<size_t>(i)]), mouse);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    ItemStack& selected = inventory.hotbar[inventory.selected_slot];
                    selected = {blocks[static_cast<size_t>(i)], ItemType::None, MAX_ITEM_STACK, 0};
                }
            }
        }
        ui::label({panel_x, panel_y + panel_h - ui::scaled(22.0f), panel_w, ui::scaled(18.0f)},
                  ui::tr("inventory.pages") + "  " + std::to_string(creative_page + 1) + "/" + std::to_string(page_count),
                  Color{45,45,45,255});
        tooltip.draw();
        return std::nullopt;
    }

    const Texture2D& texture = TextureManager::get(container_texture_path(kind));
    const float inventory_scale = INVENTORY_SCALE * ui::scale_factor();

    float panel_w = static_cast<float>(texture.width) * inventory_scale;
    float panel_h = static_cast<float>(texture.height) * inventory_scale;
    float panel_x = std::round((GetScreenWidth() - panel_w) / 2.0f);
    float panel_y = std::round((GetScreenHeight() - panel_h) / 2.0f);

    Rectangle full_source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, full_source, {panel_x, panel_y, panel_w, panel_h}, {0.0f, 0.0f}, 0.0f, WHITE);

    // Null whenever kind isn't Chest/LargeChest (or, defensively, if world
    // somehow isn't loaded) - every chest-specific block below checks this
    // instead of re-deriving the same condition. LargeChest additionally
    // fills `chest_secondary` - `chest_x/y/z` is whichever half was
    // actually clicked (open_container()'s own comment), so this resolves
    // its stored ChestPart/facing to figure out which position is primary
    // (top 27 slots) vs. secondary (bottom 27) for a stable layout
    // regardless of which half the player opened it from. Never moves or
    // copies either chest's own storage - both stay independently keyed by
    // World::chest_inventory(), exactly like a lone Chest.
    std::array<ItemStack, INVENTORY_STORAGE_SIZE>* chest = nullptr;
    std::array<ItemStack, INVENTORY_STORAGE_SIZE>* chest_secondary = nullptr;
    if (kind == ContainerKind::Chest && world) {
        chest = &world->chest_inventory(chest_x, chest_y, chest_z);
    } else if (kind == ContainerKind::LargeChest && world) {
        uint16_t packed = world->get_block_state(chest_x, chest_y, chest_z);
        ChestPart part = static_cast<ChestPart>((packed & BlockStateBits::MULTIBLOCK_PART_MASK) >> BlockStateBits::MULTIBLOCK_PART_SHIFT);
        HorizontalDirection facing = world->get_block_orientation(chest_x, chest_y, chest_z);
        DirectionOffset right_step = horizontal_direction_offset(horizontal_direction_right_of(facing));
        int sign = part == ChestPart::Primary ? 1 : -1;
        int partner_x = chest_x + right_step.dx * sign;
        int partner_z = chest_z + right_step.dz * sign;
        std::array<ItemStack, INVENTORY_STORAGE_SIZE>* this_half = &world->chest_inventory(chest_x, chest_y, chest_z);
        std::array<ItemStack, INVENTORY_STORAGE_SIZE>* partner_half = &world->chest_inventory(partner_x, chest_y, partner_z);
        chest = part == ChestPart::Primary ? this_half : partner_half;
        chest_secondary = part == ChestPart::Primary ? partner_half : this_half;
    }

    if (kind == ContainerKind::Inventory) {
        Rectangle preview_destination = {
            panel_x + INVENTORY_PREVIEW_BOX.x * inventory_scale, panel_y + INVENTORY_PREVIEW_BOX.y * inventory_scale,
            INVENTORY_PREVIEW_BOX.width * inventory_scale, INVENTORY_PREVIEW_BOX.height * inventory_scale,
        };
        draw_player_preview(preview_destination, GetMousePosition());
    }

    // The open furnace's own state (World::furnace_state(), keyed by the
    // same position chest_x/y/z open_container() stored) - null for every
    // other kind.
    FurnaceState* furnace = (kind == ContainerKind::Furnace && world)
        ? &world->furnace_state(chest_x, chest_y, chest_z) : nullptr;

    if (furnace) {
        // Both icons are drawn only partly, as progress: the flame from the
        // bottom up by how much of the current fuel item is left, the arrow
        // left to right by how far the current item has cooked - the
        // panel's own art already shows their empty outlines underneath.
        const Texture2D& fuel = TextureManager::get(FUEL_PROGRESS_TEXTURE_PATH);
        const Texture2D& smelting = TextureManager::get(SMELTING_PROGRESS_TEXTURE_PATH);
        auto draw_icon_part = [&](const Texture2D& icon, Vector2 origin, Rectangle source) {
            if (source.width <= 0.0f || source.height <= 0.0f) return;
            Rectangle destination = {
                panel_x + (origin.x + source.x) * inventory_scale, panel_y + (origin.y + source.y) * inventory_scale,
                source.width * inventory_scale, source.height * inventory_scale,
            };
            DrawTexturePro(icon, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        };
        if (furnace->burning() && furnace->burn_ticks_total > 0) {
            const float h = static_cast<float>(fuel.height);
            float lit = std::ceil(h * static_cast<float>(furnace->burn_ticks_left) / static_cast<float>(furnace->burn_ticks_total));
            draw_icon_part(fuel, FUEL_PROGRESS_ORIGIN, {0.0f, h - lit, static_cast<float>(fuel.width), lit});
        }
        float cooked = std::floor(static_cast<float>(smelting.width) * static_cast<float>(furnace->cook_ticks) /
                                  static_cast<float>(smelting_cook_ticks()));
        draw_icon_part(smelting, SMELTING_PROGRESS_ORIGIN, {0.0f, 0.0f, cooked, static_cast<float>(smelting.height)});
    }

    Vector2 mouse = GetMousePosition();
    ItemStack* hovered_slot = nullptr;
    bool hovered_hotbar = false;
    bool hovered_chest = false;     // a container-side slot (chest, furnace) rather than the player's own
    bool hovered_take_only = false; // furnace output - items can be taken out, never put in

    auto process_slot = [&](ItemStack& stack, Vector2 slot_origin, bool is_hotbar, bool is_chest = false,
                            bool take_only = false, float size_px = ITEM_SIZE_PX) {
        const Rectangle bounds = slot_bounds(slot_origin, inventory_scale, size_px);
        draw_item_stack(bounds, stack, inventory_scale);

        if (CheckCollisionPointRec(mouse, bounds)) {
            hovered_slot = &stack;
            hovered_hotbar = is_hotbar;
            hovered_chest = is_chest;
            hovered_take_only = take_only;
            draw_slot_highlight(bounds);
            if (!stack.empty()) {
                tooltip.show(stack_name(stack), mouse);
            }
        }
    };

    if (chest) {
        // A lone Chest draws its 27 slots at row 0-2 of CHEST_GRID_ORIGIN;
        // LargeChest draws the primary's 27 at rows 0-2 and the
        // secondary's 27 directly below at rows 3-5, same origin/stride -
        // container4.png's own art is exactly this single 9x6 grid with no
        // internal divider between the two halves.
        for (int i = 0; i < INVENTORY_STORAGE_SIZE; ++i) {
            int col = i % MAIN_GRID_COLUMNS;
            int row = i / MAIN_GRID_COLUMNS;
            Vector2 slot_origin = {
                panel_x + (CHEST_GRID_ORIGIN.x + col * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
                panel_y + (CHEST_GRID_ORIGIN.y + row * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
            };
            process_slot((*chest)[i], slot_origin, false, true);
        }
        if (chest_secondary) {
            constexpr int SECONDARY_ROW_OFFSET = INVENTORY_STORAGE_SIZE / MAIN_GRID_COLUMNS; // 3
            for (int i = 0; i < INVENTORY_STORAGE_SIZE; ++i) {
                int col = i % MAIN_GRID_COLUMNS;
                int row = SECONDARY_ROW_OFFSET + i / MAIN_GRID_COLUMNS;
                Vector2 slot_origin = {
                    panel_x + (CHEST_GRID_ORIGIN.x + col * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
                    panel_y + (CHEST_GRID_ORIGIN.y + row * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
                };
                process_slot((*chest_secondary)[i], slot_origin, false, true);
            }
        }
    }

    if (furnace) {
        auto origin = [&](Vector2 o) { return Vector2{panel_x + o.x * inventory_scale, panel_y + o.y * inventory_scale}; };
        process_slot(furnace->input, origin(FURNACE_INPUT_ORIGIN), false, true);
        process_slot(furnace->fuel, origin(FURNACE_FUEL_ORIGIN), false, true);
        process_slot(furnace->output, origin(FURNACE_OUTPUT_ORIGIN), false, true, true, FURNACE_OUTPUT_SIZE_PX);
    }

    // LargeChest's own container4.png is taller (LARGE_CHEST_GRID_ROWS
    // storage rows instead of Chest's 3), so its player-storage/hotbar
    // section sits at its own shifted origin - see LARGE_CHEST_MAIN_GRID_
    // ORIGIN/LARGE_CHEST_HOTBAR_ORIGIN's own comment. Every other kind
    // keeps the plain, shared origin.
    Vector2 main_grid_origin = kind == ContainerKind::LargeChest ? LARGE_CHEST_MAIN_GRID_ORIGIN : MAIN_GRID_ORIGIN;
    Vector2 hotbar_origin = kind == ContainerKind::LargeChest ? LARGE_CHEST_HOTBAR_ORIGIN : INVENTORY_HOTBAR_ORIGIN;

    for (int i = 0; i < INVENTORY_STORAGE_SIZE; ++i) {
        int col = i % MAIN_GRID_COLUMNS;
        int row = i / MAIN_GRID_COLUMNS;
        Vector2 slot_origin = {
            panel_x + (main_grid_origin.x + col * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
            panel_y + (main_grid_origin.y + row * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
        };
        process_slot(inventory.storage[i], slot_origin, false);
    }

    // This is the same data as the always-visible hotbar, not a copy.
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        Vector2 slot_origin = {
            panel_x + (hotbar_origin.x + i * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
            panel_y + hotbar_origin.y * inventory_scale,
        };
        process_slot(inventory.hotbar[i], slot_origin, true);
    }

    // Real crafting grid cells - plain ItemStack slots exactly like storage
    // above, so they fall through process_slot() into the very same
    // hovered_slot/carried_stack drag-and-drop handling below for free (a
    // player can place/take/swap/split ingredients in them exactly like any
    // other slot). Only the output slot below needs its own special
    // handling - it has no backing ItemStack of its own to drag.
    const int craft_cols = kind == ContainerKind::Workbench ? 3 : 2;
    if (kind == ContainerKind::Workbench) {
        for (int i = 0; i < static_cast<int>(workbench_craft_grid.size()); ++i) {
            int col = i % craft_cols, row = i / craft_cols;
            Vector2 slot_origin = {
                panel_x + (WORKBENCH_CRAFT_ORIGIN.x + col * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
                panel_y + (WORKBENCH_CRAFT_ORIGIN.y + row * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
            };
            process_slot(workbench_craft_grid[i], slot_origin, false);
        }
    } else if (kind == ContainerKind::Inventory) {
        for (int i = 0; i < static_cast<int>(inventory_craft_grid.size()); ++i) {
            int col = i % craft_cols, row = i / craft_cols;
            Vector2 slot_origin = {
                panel_x + (INVENTORY_CRAFT_ORIGIN.x + col * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
                panel_y + (INVENTORY_CRAFT_ORIGIN.y + row * CONTAINER_SLOT_STRIDE_PX) * inventory_scale,
            };
            process_slot(inventory_craft_grid[i], slot_origin, false);
        }
    }

    auto same_stack = [](const ItemStack& a, const ItemStack& b) {
        if (a.empty() || b.empty() || a.is_tool() || b.is_tool()) return false;
        // A material's `block` field is unused (stays Air) same as another
        // material's - comparing blocks alone would read any two different
        // materials as "the same stack". Compare by item type instead
        // whenever either side actually holds one.
        if (a.holds_item() || b.holds_item()) return a.tool == b.tool;
        return a.block == b.block;
    };
    auto quick_move = [&](ItemStack& source, bool from_hotbar, bool from_chest) {
        if (source.empty()) return;
        auto transfer_to = [&](auto& destination) {
            if (source.is_tool()) {
                for (ItemStack& slot : destination) {
                    if (slot.empty()) { slot = source; source.clear(); return; }
                }
                return;
            }
            for (ItemStack& slot : destination) {
                if (!same_stack(slot, source) || slot.count >= MAX_ITEM_STACK) continue;
                int moved = std::min(source.count, MAX_ITEM_STACK - slot.count);
                slot.count += moved;
                source.count -= moved;
                if (source.count <= 0) { source.clear(); return; }
            }
            for (ItemStack& slot : destination) {
                if (!slot.empty()) continue;
                slot = source;
                source.clear();
                return;
            }
        };
        if (from_chest) {
            // Out of the chest and into whichever of the player's own two
            // areas has room - storage first, hotbar as overflow.
            transfer_to(inventory.storage);
            if (!source.empty()) transfer_to(inventory.hotbar);
        } else if (chest) {
            // A chest is open and this shift-click came from the player's
            // own side (storage/hotbar/craft grid) - send it into the
            // chest instead of just shuffling storage<->hotbar. LargeChest
            // overflows into its secondary half once the primary is full.
            transfer_to(*chest);
            if (chest_secondary && !source.empty()) transfer_to(*chest_secondary);
        } else if (furnace && (smelting_result(source) || fuel_burn_ticks(source) > 0)) {
            // Into the furnace, vanilla's order: anything smeltable goes
            // to the input slot (a log smelts before it burns), otherwise
            // fuel to the fuel slot.
            ItemStack& slot = smelting_result(source) ? furnace->input : furnace->fuel;
            if (slot.empty()) {
                slot = source;
                source.clear();
            } else if (same_stack(slot, source) && slot.count < MAX_ITEM_STACK) {
                int moved = std::min(source.count, MAX_ITEM_STACK - slot.count);
                slot.count += moved;
                source.count -= moved;
                if (source.count <= 0) source.clear();
            }
        } else if (from_hotbar) {
            transfer_to(inventory.storage);
        } else {
            transfer_to(inventory.hotbar);
        }
    };

    std::optional<ItemStack> dropped;
    const bool left_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool right_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (hovered_slot && hovered_take_only && (left_pressed || right_pressed)) {
        // Furnace output: take-only. Shift+click sends it all to the
        // inventory; a click takes the stack (right-click half of it), or
        // tops up a matching stack already on the cursor - nothing can
        // ever be dropped *into* it.
        if (hovered_slot->empty()) {
            // nothing to take
        } else if (left_pressed && IsKeyDown(KEY_LEFT_SHIFT) && carried_stack.empty()) {
            quick_move(*hovered_slot, false, true);
        } else if (carried_stack.empty()) {
            int amount = left_pressed ? hovered_slot->count : (hovered_slot->count + 1) / 2;
            carried_stack = *hovered_slot;
            carried_stack.count = amount;
            hovered_slot->count -= amount;
            if (hovered_slot->count <= 0) hovered_slot->clear();
        } else if (left_pressed && same_stack(*hovered_slot, carried_stack)) {
            int moved = std::min(hovered_slot->count, MAX_ITEM_STACK - carried_stack.count);
            carried_stack.count += moved;
            hovered_slot->count -= moved;
            if (hovered_slot->count <= 0) hovered_slot->clear();
        }
    } else if (hovered_slot && carried_stack.empty() && IsKeyDown(KEY_LEFT_SHIFT) && left_pressed) {
        quick_move(*hovered_slot, hovered_hotbar, hovered_chest);
    } else if (hovered_slot && left_pressed) {
        double now = GetTime();
        bool double_click = last_clicked_slot == hovered_slot && now - last_click_time <= 0.25;
        last_click_time = now;
        last_clicked_slot = hovered_slot;
        if (carried_stack.empty()) {
            carried_stack = *hovered_slot;
            hovered_slot->clear();
        } else if (double_click && !carried_stack.is_tool()) {
            auto gather = [&](auto& slots) {
                for (ItemStack& slot : slots) {
                    if (!same_stack(slot, carried_stack)) continue;
                    int moved = std::min(slot.count, MAX_ITEM_STACK - carried_stack.count);
                    carried_stack.count += moved;
                    slot.count -= moved;
                    if (slot.count <= 0) slot.clear();
                    if (carried_stack.count >= MAX_ITEM_STACK) return;
                }
            };
            gather(inventory.storage);
            if (carried_stack.count < MAX_ITEM_STACK) gather(inventory.hotbar);
            if (chest && carried_stack.count < MAX_ITEM_STACK) gather(*chest);
            if (chest_secondary && carried_stack.count < MAX_ITEM_STACK) gather(*chest_secondary);
        } else if (hovered_slot->empty()) {
            *hovered_slot = carried_stack;
            carried_stack.clear();
        } else if (same_stack(*hovered_slot, carried_stack)) {
            int moved = std::min(carried_stack.count, MAX_ITEM_STACK - hovered_slot->count);
            hovered_slot->count += moved;
            carried_stack.count -= moved;
            if (carried_stack.count <= 0) carried_stack.clear();
        } else {
            std::swap(*hovered_slot, carried_stack);
        }
    } else if (hovered_slot && right_pressed) {
        if (carried_stack.empty() && !hovered_slot->empty()) {
            if (hovered_slot->is_tool()) {
                carried_stack = *hovered_slot;
                hovered_slot->clear();
            } else {
                int amount = (hovered_slot->count + 1) / 2;
                carried_stack = *hovered_slot;
                carried_stack.count = amount;
                hovered_slot->count -= amount;
                if (hovered_slot->count <= 0) hovered_slot->clear();
            }
        } else if (!carried_stack.empty() && hovered_slot->empty()) {
            *hovered_slot = carried_stack;
            if (!carried_stack.is_tool()) hovered_slot->count = 1;
            if (carried_stack.is_tool() || --carried_stack.count <= 0) carried_stack.clear();
        } else if (same_stack(*hovered_slot, carried_stack) && hovered_slot->count < MAX_ITEM_STACK) {
            ++hovered_slot->count;
            if (--carried_stack.count <= 0) carried_stack.clear();
        }
    }

    if (hovered_slot && carried_stack.empty() && !hovered_take_only) {
        for (int i = 0; i < HOTBAR_SIZE; ++i) {
            if (IsKeyPressed(KEY_ONE + i)) std::swap(*hovered_slot, inventory.hotbar[i]);
        }
    }

    // Output slot - not a real backing ItemStack (see the crafting grid
    // loops above), just whatever match_recipe() currently reports for the
    // active grid. Plain left-click takes one craft into an empty cursor;
    // Shift+click instead crafts repeatedly (consuming ingredients each
    // time, re-matching after every craft so it naturally stops the moment
    // the grid can no longer supply one) and sends every result straight
    // into the inventory via put_back() rather than the cursor - it
    // already dispatches correctly whether the output is a stacking
    // block/material or a non-stacking tool (each tool lands in its own
    // slot instead of trying to pile up).
    // Set true the instant a click on the output slot is actually handled
    // below - without this, the exact same still-"pressed" click would
    // also satisfy the "clicked outside any slot while carrying something"
    // throw case further down (the output slot deliberately isn't a real
    // hovered_slot, so from that check's point of view a click here looks
    // just like a click on bare panel background), immediately throwing
    // away the item this same click had just crafted into the cursor.
    bool clicked_output_slot = false;
    if (kind == ContainerKind::Inventory || kind == ContainerKind::Workbench) {
        bool is_workbench = kind == ContainerKind::Workbench;
        Vector2 output_origin = is_workbench ? WORKBENCH_CRAFT_OUTPUT_ORIGIN : INVENTORY_CRAFT_OUTPUT_ORIGIN;
        const Rectangle output_bounds = slot_bounds(
            {panel_x + output_origin.x * inventory_scale, panel_y + output_origin.y * inventory_scale},
            inventory_scale, is_workbench ? WORKBENCH_CRAFT_OUTPUT_SIZE_PX : INVENTORY_CRAFT_OUTPUT_SIZE_PX);
        const bool hovered_output = CheckCollisionPointRec(mouse, output_bounds);

        int grid_dim = is_workbench ? 3 : 2;
        std::vector<ItemStack> grid_snapshot = is_workbench
            ? std::vector<ItemStack>(workbench_craft_grid.begin(), workbench_craft_grid.end())
            : std::vector<ItemStack>(inventory_craft_grid.begin(), inventory_craft_grid.end());
        std::optional<ItemStack> result = match_recipe(grid_snapshot, grid_dim, grid_dim);

        if (result) {
            draw_item_stack(output_bounds, *result, inventory_scale);
            if (hovered_output) {
                tooltip.show(stack_name(*result), mouse);
                if (carried_stack.empty() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    clicked_output_slot = true;
                    if (IsKeyDown(KEY_LEFT_SHIFT)) {
                        for (int crafted = 0; crafted < MAX_ITEM_STACK; ++crafted) {
                            std::optional<ItemStack> next = match_recipe(grid_snapshot, grid_dim, grid_dim);
                            if (!next || !inventory.put_back(*next)) break;
                            consume_recipe_ingredients(grid_snapshot, grid_dim, grid_dim);
                        }
                    } else {
                        carried_stack = *result;
                        consume_recipe_ingredients(grid_snapshot, grid_dim, grid_dim);
                    }
                    if (is_workbench) {
                        std::copy(grid_snapshot.begin(), grid_snapshot.end(), workbench_craft_grid.begin());
                    } else {
                        std::copy(grid_snapshot.begin(), grid_snapshot.end(), inventory_craft_grid.begin());
                    }
                }
            }
        }
        if (hovered_output) draw_slot_highlight(output_bounds);
    }

    if (!carried_stack.empty()) {
        float size = ITEM_SIZE_PX * inventory_scale;
        tooltip.clear();
        draw_item_stack({mouse.x - size / 2.0f, mouse.y - size / 2.0f, size, size}, carried_stack, inventory_scale);
    }

    // Draw as the final inventory layer so later block slots cannot cover it.
    tooltip.draw();

    // drop_binding drops one item out of whatever's hovered, Shift+it the
    // whole stack - only while nothing's actively being dragged
    // (carried_stack empty), same as real Minecraft's own inventory screen
    // gates it. GameEngine turns the returned stack into an actual
    // DroppedItem thrown out in front of the player; this class has no
    // notion of world position to spawn one itself.
    if (hovered_slot && carried_stack.empty() && !hovered_slot->empty() && binding_pressed(drop_binding)) {
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            dropped = *hovered_slot;
            hovered_slot->clear();
        } else {
            dropped = take_one_item(*hovered_slot);
        }
    } else if (!hovered_slot && !clicked_output_slot && !carried_stack.empty() &&
               !CheckCollisionPointRec(mouse, {panel_x, panel_y, panel_w, panel_h}) &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        dropped = carried_stack;
        carried_stack.clear();
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

std::optional<InventoryHud::ContainerKind> resolve_container_kind(const World& world, int x, int y, int z)
{
    BlockType type = world.get_block(x, y, z);
    if (type != BlockType::Chest) return container_kind_for_block(type);

    uint16_t packed = world.get_block_state(x, y, z);
    ChestPart part = static_cast<ChestPart>((packed & BlockStateBits::MULTIBLOCK_PART_MASK) >> BlockStateBits::MULTIBLOCK_PART_SHIFT);
    return part == ChestPart::Single ? InventoryHud::ContainerKind::Chest : InventoryHud::ContainerKind::LargeChest;
}
