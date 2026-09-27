// The model editor's "Blocks" tab: every full-cube block the game loads
// from assets/blocks/<name>.json (content/BlockFile.hpp) - listed on the
// left, turned about in the middle, its properties, faces and textures on
// the right.
#include "ModelEditor.hpp"
#include "EditorStyle.hpp"
#include "EditorText.hpp"
#include "model/EntityModelRenderer.hpp"
#include "rendering/BlockIcon.hpp"

#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

    using editor_text::tr;
    using editor_text::tr_format;
    using namespace editor_style;

    constexpr float BLOCK_LIST_WIDTH  = 290.0f;
    constexpr float BLOCK_PANEL_WIDTH = 390.0f;
    constexpr float LIST_ROW          = 30.0f;

    // Each face's brightness in the preview, like the game's own face shading.
    constexpr float FACE_SHADE[6] = {1.0f, 0.5f, 0.8f, 0.8f, 0.6f, 0.6f};
    constexpr const char* FACE_KEYS[6] = {"editor.block_face.top",   "editor.block_face.bottom", "editor.block_face.north",
                                          "editor.block_face.south", "editor.block_face.east",   "editor.block_face.west"};
    // Each face's corners exactly as the game's chunk mesher lays a cube out
    // (Chunk.cpp's unit_cube_faces(), counter-clockwise from outside), a
    // unit cube about the origin; textured u = 0,1,1,0 and v = 0,0,1,1 over
    // them, as the game does - so a face's picture sits the same way here.
    constexpr Vector3 FACE_CORNERS[6][4] = {
        {{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}},     // top
        {{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}}, // bottom
        {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}, // north (-Z)
        {{0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}},     // south (+Z)
        {{0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}},     // east (+X)
        {{-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}}, // west (-X)
    };
    constexpr float CORNER_U[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    constexpr float CORNER_V[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr Vector3 FACE_NORMALS[6] = {{0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}};

    Color gui_color(int control, int property) {
        return GetColor(static_cast<unsigned int>(GuiGetStyle(control, property)));
    }

    // The game's own name for a block ("block.<name>"), or its id.
    std::string display_name(const std::string& name) {
        const std::string key = "block." + name;
        const std::string& text = tr(key);
        return text == key ? name : text;
    }

    std::string lower(std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // One 16px tile of terrain.png, in pixels.
    Rectangle tile_source(int x, int y) {
        return {x * 16.0f, y * 16.0f, 16.0f, 16.0f};
    }

    // A face's corners, the side ones pulled in by the block's side inset.
    Vector3 face_corner(const block_file::BlockFile& block, int face, int corner) {
        Vector3 p = FACE_CORNERS[face][corner];
        if (face >= 2 && block.side_inset > 0) p = Vector3Subtract(p, Vector3Scale(FACE_NORMALS[face], block.side_inset / 16.0f));
        return p;
    }
}

// ---------------------------------------------------------------- Setup --

const Texture2D& ModelEditor::terrain_atlas()
{
    if (preview_blocks_atlas.id == 0) {
        preview_blocks_atlas = LoadTexture(ASSETS_PATH "sprites/terrain.png");
        SetTextureFilter(preview_blocks_atlas, TEXTURE_FILTER_POINT);
    }
    return preview_blocks_atlas;
}

void ModelEditor::load_blocks()
{
    blocks = block_file::load_all();
    block_dirty.assign(blocks.size(), false);
    blocks_loaded = true;
    block_undo_stack.clear();
    block_redo_stack.clear();
    select_block(blocks.empty() ? -1 : 0);
}

void ModelEditor::select_block(int index)
{
    commit_block_history(true);
    selected_block = index;
    editing_widget = -1;
    if (index >= 0) {
        block_committed = blocks[static_cast<size_t>(index)];
        block_committed_index = index;
    } else {
        block_committed_index = -1;
    }
}

void ModelEditor::save_block(int index)
{
    if (index < 0 || index >= static_cast<int>(blocks.size())) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(index)];
    const std::string path = block_file::directory() + block.name + ".json";
    if (block_file::save(block, path)) {
        block_dirty[static_cast<size_t>(index)] = false;
        set_status(tr_format("editor.block_saved", {display_name(block.name)}));
    } else {
        set_status(tr_format("editor.block_save_failed", {path}));
    }
}

void ModelEditor::save_all_blocks()
{
    int saved = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (!block_dirty[i]) continue;
        if (block_file::save(blocks[i], block_file::directory() + blocks[i].name + ".json")) {
            block_dirty[i] = false;
            ++saved;
        }
    }
    set_status(tr_format("editor.blocks_saved_all", {std::to_string(saved)}));
}

// -------------------------------------------------------------- History --

void ModelEditor::mark_block_dirty()
{
    if (selected_block < 0) return;
    block_dirty[static_cast<size_t>(selected_block)] = true;
    block_uncommitted = true;
}

void ModelEditor::commit_block_history(bool force)
{
    if (!block_uncommitted || block_committed_index < 0) return;
    const bool mouse_busy = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (!force && (mouse_busy || typing())) return; // still in the middle of this change
    block_undo_stack.push_back({block_committed_index, block_committed});
    if (block_undo_stack.size() > 200) block_undo_stack.erase(block_undo_stack.begin());
    block_redo_stack.clear();
    if (selected_block >= 0) block_committed = blocks[static_cast<size_t>(selected_block)];
    block_committed_index = selected_block;
    block_uncommitted = false;
}

void ModelEditor::block_undo()
{
    commit_block_history(true);
    if (block_undo_stack.empty()) {
        set_status(tr("editor.nothing_to_undo"));
        return;
    }
    const auto [index, state] = block_undo_stack.back();
    block_undo_stack.pop_back();
    block_redo_stack.push_back({index, blocks[static_cast<size_t>(index)]});
    blocks[static_cast<size_t>(index)] = state;
    block_dirty[static_cast<size_t>(index)] = true;
    selected_block = index;
    block_committed = state;
    block_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.undone", {std::to_string(block_undo_stack.size())}));
}

void ModelEditor::block_redo()
{
    commit_block_history(true);
    if (block_redo_stack.empty()) {
        set_status(tr("editor.nothing_to_redo"));
        return;
    }
    const auto [index, state] = block_redo_stack.back();
    block_redo_stack.pop_back();
    block_undo_stack.push_back({index, blocks[static_cast<size_t>(index)]});
    blocks[static_cast<size_t>(index)] = state;
    block_dirty[static_cast<size_t>(index)] = true;
    selected_block = index;
    block_committed = state;
    block_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.redone", {std::to_string(block_redo_stack.size())}));
}

// ---------------------------------------------------------------- Frame --

void ModelEditor::run_blocks_frame()
{
    if (!blocks_loaded) load_blocks();
    SetMouseCursor(MOUSE_CURSOR_DEFAULT); // a number field under the mouse sets its own

    const float width = static_cast<float>(GetScreenWidth()), height = static_cast<float>(GetScreenHeight());
    const Rectangle list = {0, HEADER_HEIGHT, BLOCK_LIST_WIDTH, height - HEADER_HEIGHT};
    const Rectangle panel = {width - BLOCK_PANEL_WIDTH, HEADER_HEIGHT, BLOCK_PANEL_WIDTH, height - HEADER_HEIGHT};
    const Rectangle view = {BLOCK_LIST_WIDTH, HEADER_HEIGHT, width - BLOCK_LIST_WIDTH - BLOCK_PANEL_WIDTH, height - HEADER_HEIGHT};

    if (!typing()) {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
                          IsKeyDown(KEY_RIGHT_SUPER);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool z = IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z);
        const bool y = IsKeyPressed(KEY_Y) || IsKeyPressedRepeat(KEY_Y);
        if (ctrl && IsKeyPressed(KEY_S)) save_block(selected_block);
        if (ctrl && z && !shift) block_undo();
        else if (ctrl && ((z && shift) || y)) block_redo();
    }

    update_block_camera(view);
    draw_block_preview(view);

    BeginDrawing();
    ClearBackground(gui_color(DEFAULT, BACKGROUND_COLOR));
    DrawTexturePro(block_view_texture.texture,
                   {0, 0, static_cast<float>(block_view_texture.texture.width), -static_cast<float>(block_view_texture.texture.height)},
                   view, {0, 0}, 0.0f, WHITE);
    label({view.x + PAD, view.y + view.height - ROW - 4, view.width - PAD * 2, ROW}, tr("editor.block_view_hint"));
    if (selected_block >= 0) {
        const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
        label({view.x + PAD, view.y + PAD, view.width - PAD * 2, ROW},
              display_name(block.name) + "  (" + block.name + ", id " + std::to_string(block.id) + ")   " +
                  tr(FACE_KEYS[selected_face]));
    }
    // Over the view's top right corner: how the game will show it.
    const float side = std::min(340.0f, view.width * 0.45f);
    const Rectangle game_box = {view.x + view.width - side - PAD, view.y + PAD, side, side * 0.68f};
    draw_block_game_view(game_box);
    const float icon_box_height = 16.0f * 2.0f * game_ui_scale + ROW + PAD * 3.0f;
    draw_block_inventory_icon({game_box.x, game_box.y + game_box.height + GAP, side, icon_box_height});
    draw_block_list(list);
    draw_block_properties(panel);
    draw_blocks_top_bar(top_bar_rect());
    draw_tabs();
    EndDrawing();
    commit_block_history();
}

void ModelEditor::draw_blocks_top_bar(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    float x = bounds.x + PAD;
    const float y = bounds.y + (bounds.height - ROW) * 0.5f;
    GuiSetState(selected_block >= 0 ? STATE_NORMAL : STATE_DISABLED);
    if (GuiButton({x, y, 210, ROW}, tr("editor.block_save").c_str())) save_block(selected_block);
    x += 210 + GAP;
    GuiSetState(STATE_NORMAL);
    if (GuiButton({x, y, 170, ROW}, tr("editor.block_save_all").c_str())) save_all_blocks();
    x += 170 + GAP * 3;

    int unsaved = 0;
    for (bool dirty_block : block_dirty) unsaved += dirty_block ? 1 : 0;
    std::string line = unsaved > 0 ? tr_format("editor.blocks_unsaved", {std::to_string(unsaved)}) : tr("editor.blocks_restart_hint");
    if (GetTime() - status_time < 4.0) line = status;
    label({x, y, bounds.width - x - PAD, ROW}, line);
}

// ----------------------------------------------------------------- List --

void ModelEditor::draw_block_list(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    // Search by the game's name or the id.
    label({bounds.x + PAD, bounds.y + PAD, 70, ROW}, tr("editor.blocks_search"));
    string_field({bounds.x + PAD + 70, bounds.y + PAD, bounds.width - PAD * 2 - 70, ROW}, block_search);

    std::vector<int> shown;
    const std::string needle = lower(block_search);
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (needle.empty() || lower(blocks[i].name).find(needle) != std::string::npos ||
            lower(display_name(blocks[i].name)).find(needle) != std::string::npos) {
            shown.push_back(static_cast<int>(i));
        }
    }

    const Rectangle area = {bounds.x, bounds.y + PAD * 2 + ROW, bounds.width, bounds.height - PAD * 2 - ROW};
    Rectangle view{};
    const float content_height = static_cast<float>(shown.size()) * LIST_ROW + PAD;
    GuiScrollPanel(area, nullptr, {0, 0, area.width - 14, content_height}, &block_list_scroll, &view);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const Texture2D& atlas = terrain_atlas();
    const Vector2 mouse = GetMousePosition();
    const bool mouse_inside = CheckCollisionPointRec(mouse, view);
    for (size_t row = 0; row < shown.size(); ++row) {
        const int index = shown[row];
        const block_file::BlockFile& block = blocks[static_cast<size_t>(index)];
        const Rectangle r = {view.x, view.y + block_list_scroll.y + row * LIST_ROW, view.width, LIST_ROW};
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        const bool hovered = mouse_inside && CheckCollisionPointRec(mouse, r);
        if (index == selected_block) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
        else if (hovered) DrawRectangleRec(r, Fade(WHITE, 0.06f));
        // Its top face as an icon.
        const block_file::Face& top = block.faces[0];
        DrawTexturePro(atlas, tile_source(top.tile_x, top.tile_y), {r.x + 6, r.y + 3, 24, 24}, {0, 0}, 0.0f, top.tint);
        const std::string text = display_name(block.name) + (block_dirty[static_cast<size_t>(index)] ? " *" : "");
        label({r.x + 34, r.y, r.width - 40, r.height}, text);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && index != selected_block) select_block(index);
    }
    EndScissorMode();
}

// ----------------------------------------------------------- Properties --

void ModelEditor::draw_block_properties(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 900.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_panel_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    const float label_w = 200.0f;
    float y = view.y + block_panel_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto combo = [&](const std::string& caption, int& value, const char* const* ids, int count, const char* key_prefix) {
        label({x, y, label_w, ROW}, caption);
        std::string items;
        for (int i = 0; i < count; ++i) items += (i ? ";" : "") + tr(std::string(key_prefix) + ids[i]);
        int chosen = value;
        GuiComboBox({x + label_w, y, width - label_w, ROW}, items.c_str(), &chosen);
        if (chosen != value) {
            value = chosen;
            mark_block_dirty();
        }
        y += ROW + GAP;
    };
    auto number = [&](const std::string& caption, float& value, float min_value, float max_value) {
        label({x, y, label_w, ROW}, caption);
        if (float_field({x + label_w, y, width - label_w, ROW}, value, min_value, max_value)) mark_block_dirty();
        y += ROW + GAP;
    };
    auto whole = [&](const std::string& caption, int& value, int min_value, int max_value) {
        label({x, y, label_w, ROW}, caption);
        if (int_field({x + label_w, y, width - label_w, ROW}, value, min_value, max_value)) mark_block_dirty();
        y += ROW + GAP;
    };
    auto flag = [&](const std::string& caption, bool& value) {
        bool checked = value;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, caption.c_str(), &checked);
        if (checked != value) {
            value = checked;
            mark_block_dirty();
        }
        y += ROW + 2;
    };

    // What it is and how it behaves.
    section(tr("editor.block_properties"));
    combo(tr("editor.block_sound"), block.sound, block_file::SOUND_IDS, block_file::SOUND_COUNT, "editor.sound.");
    combo(tr("editor.block_tool"), block.tool, block_file::TOOL_IDS, block_file::TOOL_COUNT, "editor.tool.");
    number(tr("editor.block_hardness"), block.hardness, 0.0f, 1000.0f);
    number(tr("editor.block_density"), block.density, 0.0f, 50.0f);
    whole(tr("editor.block_luminance"), block.luminance, 0, 15);
    whole(tr("editor.block_side_inset"), block.side_inset, 0, 8);
    y += GAP;
    flag(tr("editor.block_solid"), block.solid);
    flag(tr("editor.block_selectable"), block.selectable);
    flag(tr("editor.block_replaceable"), block.replaceable);
    flag(tr("editor.block_transparent"), block.transparent);
    flag(tr("editor.block_translucent"), block.translucent);
    flag(tr("editor.block_cutout"), block.cutout);
    flag(tr("editor.block_keep_same_faces"), block.keep_same_faces);
    flag(tr("editor.block_damages_on_touch"), block.damages_on_touch);
    flag(tr("editor.block_directional"), block.directional);

    // Its six faces: pick one, then its tile below.
    y += GAP;
    section(tr("editor.block_faces"));
    const Texture2D& atlas = terrain_atlas();
    for (int f = 0; f < 6; ++f) {
        const block_file::Face& face = block.faces[f];
        bool picked = f == selected_face;
        GuiToggle({x, y, 150, ROW}, tr(FACE_KEYS[f]).c_str(), &picked);
        if (picked) selected_face = f;
        DrawTexturePro(atlas, tile_source(face.tile_x, face.tile_y), {x + 158, y + 1, ROW - 2, ROW - 2}, {0, 0}, 0.0f, face.tint);
        label({x + 158 + ROW + 4, y, 90, ROW}, std::to_string(face.tile_x) + ", " + std::to_string(face.tile_y));
        y += ROW + 2;
    }
    y += GAP;
    const float half = (width - GAP) * 0.5f;
    if (GuiButton({x, y, half, ROW}, tr("editor.block_to_all").c_str())) {
        for (int f = 0; f < 6; ++f) block.faces[f] = block.faces[selected_face];
        mark_block_dirty();
    }
    if (GuiButton({x + half + GAP, y, half, ROW}, tr("editor.block_to_sides").c_str())) {
        for (int f = 2; f < 6; ++f) block.faces[f] = block.faces[selected_face];
        mark_block_dirty();
    }
    y += ROW + GAP;

    // The picked face's tint: its swatch, then red, green, blue, alpha.
    {
        block_file::Face& face = block.faces[selected_face];
        label({x, y, width - ROW - GAP, ROW}, tr("editor.block_tint"));
        DrawRectangleRec({x + width - ROW, y, ROW, ROW}, face.tint);
        DrawRectangleLinesEx({x + width - ROW, y, ROW, ROW}, 1.0f, gui_color(DEFAULT, LINE_COLOR));
        y += ROW + GAP;
        const float field = (width - GAP * 3) / 4.0f;
        unsigned char* channels[4] = {&face.tint.r, &face.tint.g, &face.tint.b, &face.tint.a};
        for (int c = 0; c < 4; ++c) {
            float value = *channels[c];
            if (float_field({x + c * (field + GAP), y, field, ROW}, value, 0.0f, 255.0f)) {
                *channels[c] = static_cast<unsigned char>(std::lround(value));
                mark_block_dirty();
            }
        }
        y += ROW + GAP * 2;
    }

    // The atlas: click a tile to put it on the picked face.
    section(tr("editor.block_atlas"));
    {
        const float cell = std::floor(width / block_file::ATLAS_TILES);
        const Rectangle grid = {x, y, cell * block_file::ATLAS_TILES, cell * block_file::ATLAS_TILES};
        DrawRectangleRec(grid, Color{30, 30, 34, 255});
        DrawTexturePro(atlas, {0, 0, static_cast<float>(atlas.width), static_cast<float>(atlas.height)}, grid, {0, 0}, 0.0f, WHITE);
        // Tiles this block uses, faintly; the picked face's, brightly.
        for (int f = 0; f < 6; ++f) {
            const block_file::Face& face = block.faces[f];
            DrawRectangleLinesEx({grid.x + face.tile_x * cell, grid.y + face.tile_y * cell, cell, cell}, 1.0f, Fade(WHITE, 0.5f));
        }
        const block_file::Face& picked = block.faces[selected_face];
        DrawRectangleLinesEx({grid.x + picked.tile_x * cell - 1, grid.y + picked.tile_y * cell - 1, cell + 2, cell + 2}, 2.0f, SELECTION);
        const Vector2 mouse = GetMousePosition();
        if (mouse_inside && CheckCollisionPointRec(mouse, grid)) {
            const int tx = std::clamp(static_cast<int>((mouse.x - grid.x) / cell), 0, block_file::ATLAS_TILES - 1);
            const int ty = std::clamp(static_cast<int>((mouse.y - grid.y) / cell), 0, block_file::ATLAS_TILES - 1);
            DrawRectangleLinesEx({grid.x + tx * cell, grid.y + ty * cell, cell, cell}, 1.0f, YELLOW);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                block.faces[selected_face].tile_x = tx;
                block.faces[selected_face].tile_y = ty;
                mark_block_dirty();
            }
        }
        y += grid.height + PAD;
    }

    content_height = y - (view.y + block_panel_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// -------------------------------------------------------------- Preview --

void ModelEditor::update_block_camera(Rectangle view)
{
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, view);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (over && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        block_view_dragging = IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || alt;
        block_press_position = mouse;
    }
    if (block_view_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 delta = GetMouseDelta();
            block_yaw -= delta.x * 0.008f;
            block_pitch = std::clamp(block_pitch + delta.y * 0.008f, -1.5f, 1.5f);
        } else {
            block_view_dragging = false;
        }
    }
    if (over) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) block_distance = std::clamp(block_distance * std::pow(0.88f, wheel), 1.2f, 8.0f);
    }

    // A plain click on a face picks it.
    if (over && !block_view_dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
        Vector2Distance(block_press_position, mouse) < 4.0f && selected_block >= 0) {
        Camera3D cam{};
        cam.target = {0, 0, 0};
        cam.position = {block_distance * std::cos(block_pitch) * std::sin(block_yaw), block_distance * std::sin(block_pitch),
                        block_distance * std::cos(block_pitch) * std::cos(block_yaw)};
        cam.up = {0, 1, 0};
        cam.fovy = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        const Ray ray = GetScreenToWorldRayEx(Vector2Subtract(mouse, {view.x, view.y}), cam, static_cast<int>(view.width),
                                              static_cast<int>(view.height));
        const RayCollision hit = GetRayCollisionBox(ray, {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
        if (hit.hit) {
            for (int f = 0; f < 6; ++f) {
                if (Vector3DotProduct(hit.normal, FACE_NORMALS[f]) > 0.9f) selected_face = f;
            }
        }
    }
}

void ModelEditor::draw_block_preview(Rectangle view)
{
    const int width = std::max(1, static_cast<int>(view.width));
    const int height = std::max(1, static_cast<int>(view.height));
    if (block_view_texture.id == 0 || block_view_texture.texture.width != width || block_view_texture.texture.height != height) {
        if (block_view_texture.id != 0) UnloadRenderTexture(block_view_texture);
        block_view_texture = LoadRenderTexture(width, height);
    }

    Camera3D cam{};
    cam.target = {0, 0, 0};
    cam.position = {block_distance * std::cos(block_pitch) * std::sin(block_yaw), block_distance * std::sin(block_pitch),
                    block_distance * std::cos(block_pitch) * std::cos(block_yaw)};
    cam.up = {0, 1, 0};
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(block_view_texture);
    ClearBackground(VIEWPORT_BACKGROUND);
    BeginMode3D(cam);
    // A floor grid a little under it, for a sense of up.
    for (int i = -3; i <= 3; ++i) {
        DrawLine3D({static_cast<float>(i), -0.5f, -3.0f}, {static_cast<float>(i), -0.5f, 3.0f}, Fade(WHITE, 0.08f));
        DrawLine3D({-3.0f, -0.5f, static_cast<float>(i)}, {3.0f, -0.5f, static_cast<float>(i)}, Fade(WHITE, 0.08f));
    }
    if (selected_block >= 0) {
        const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
        const Texture2D& atlas = terrain_atlas();
        const float aw = static_cast<float>(atlas.width), ah = static_cast<float>(atlas.height);
        BeginShaderMode(entity_cutout_shader()); // see-through pixels (leaves, glass) stay see-through
        rlDisableBackfaceCulling();              // a cutout block's far faces show through its holes
        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
        for (int f = 0; f < 6; ++f) {
            const block_file::Face& face = block.faces[f];
            const Color tint = face.tint;
            rlColor4ub(static_cast<unsigned char>(tint.r * FACE_SHADE[f]), static_cast<unsigned char>(tint.g * FACE_SHADE[f]),
                       static_cast<unsigned char>(tint.b * FACE_SHADE[f]), tint.a);
            for (int c = 0; c < 4; ++c) {
                const Vector3 p = face_corner(block, f, c);
                rlTexCoord2f((face.tile_x + CORNER_U[c]) * 16.0f / aw, (face.tile_y + CORNER_V[c]) * 16.0f / ah);
                rlVertex3f(p.x, p.y, p.z);
            }
        }
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        EndShaderMode();

        // The picked face outlined, just off the surface.
        const Vector3 lift = Vector3Scale(FACE_NORMALS[selected_face], 0.004f);
        for (int c = 0; c < 4; ++c) {
            DrawLine3D(Vector3Add(face_corner(block, selected_face, c), lift),
                       Vector3Add(face_corner(block, selected_face, (c + 1) % 4), lift), SELECTION);
        }
    }
    EndMode3D();
    EndTextureMode();
}

// ------------------------------------------------------- As in the game --

namespace {
    // Minecraft-style vertex AO, as Chunk.cpp: 0 (darkest) .. 3.
    constexpr float AO_BRIGHTNESS[4] = {0.5f, 0.65f, 0.8f, 1.0f};
    constexpr Color GAME_SKY = {128, 172, 222, 255};

    // A few blocks on a grass floor - enough to show faces between two of
    // the same block, and the floor's shadow on its sides.
    struct Scene {
        struct Cell {
            int x, y, z;
            const block_file::BlockFile* block;
        };
        std::vector<Cell> cells;

        const block_file::BlockFile* at(int x, int y, int z) const
        {
            for (const Cell& cell : cells) {
                if (cell.x == x && cell.y == y && cell.z == z) return cell.block;
            }
            return nullptr;
        }
        bool solid(int x, int y, int z) const
        {
            const block_file::BlockFile* block = at(x, y, z);
            return block && block->solid;
        }
    };

    // Chunk.cpp's vertex_ao(): the two cells along the corner's edges and the
    // diagonal one, just outside the face.
    int vertex_ao(const Scene& scene, int x, int y, int z, Vector3 normal, Vector3 corner)
    {
        const int n[3] = {static_cast<int>(normal.x), static_cast<int>(normal.y), static_cast<int>(normal.z)};
        const int c[3] = {corner.x > 0.0f ? 1 : -1, corner.y > 0.0f ? 1 : -1, corner.z > 0.0f ? 1 : -1};
        int axis1 = -1, axis2 = -1;
        for (int axis = 0; axis < 3; ++axis) {
            if (n[axis] == 0) (axis1 == -1 ? axis1 : axis2) = axis;
        }
        const int base[3] = {x + n[0], y + n[1], z + n[2]};
        int side1[3] = {base[0], base[1], base[2]};
        side1[axis1] += c[axis1];
        int side2[3] = {base[0], base[1], base[2]};
        side2[axis2] += c[axis2];
        int diagonal[3] = {side1[0], side1[1], side1[2]};
        diagonal[axis2] += c[axis2];
        const bool s1 = scene.solid(side1[0], side1[1], side1[2]);
        const bool s2 = scene.solid(side2[0], side2[1], side2[2]);
        const bool cc = scene.solid(diagonal[0], diagonal[1], diagonal[2]);
        if (s1 && s2) return 0;
        return 3 - (static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cc));
    }

    // Every visible face of the scene, lit as the game lights a block in
    // full daylight: tint x face direction shade x corner AO. Opaque and
    // cutout faces first, then (`translucent_pass`) the blended ones.
    void draw_scene(const Scene& scene, const Texture2D& atlas, bool translucent_pass)
    {
        const float aw = static_cast<float>(atlas.width), ah = static_cast<float>(atlas.height);
        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
        for (const Scene::Cell& cell : scene.cells) {
            const block_file::BlockFile& block = *cell.block;
            if (block.translucent != translucent_pass) continue;
            for (int f = 0; f < 6; ++f) {
                const Vector3 normal = FACE_NORMALS[f];
                const int nx = cell.x + static_cast<int>(normal.x), ny = cell.y + static_cast<int>(normal.y),
                          nz = cell.z + static_cast<int>(normal.z);
                // The game's face culling: hidden behind anything opaque, and
                // between two of the same see-through block (unless it keeps
                // those faces, like leaves); an inset side is never hidden.
                const bool inset_side = block.side_inset > 0 && f >= 2;
                if (!inset_side) {
                    if (const block_file::BlockFile* neighbor = scene.at(nx, ny, nz)) {
                        if (!neighbor->transparent) continue;
                        if (block.transparent && neighbor == cell.block && !block.keep_same_faces) continue;
                    }
                }
                // A directional block faces south: its front there, its east
                // texture on every other side.
                int texture_face = f;
                if (block.directional && f >= 2) texture_face = f == 3 ? 3 : 4;
                const block_file::Face& face = block.faces[texture_face];

                float ao[4];
                for (int c = 0; c < 4; ++c) ao[c] = AO_BRIGHTNESS[vertex_ao(scene, cell.x, cell.y, cell.z, normal, FACE_CORNERS[f][c])];
                // Split along the brighter diagonal, as the game does.
                const int first = ao[1] + ao[3] > ao[0] + ao[2] ? 1 : 0;
                for (int k = 0; k < 4; ++k) {
                    const int c = (first + k) % 4;
                    const float light = FACE_SHADE[f] * ao[c];
                    rlColor4ub(static_cast<unsigned char>(face.tint.r * light), static_cast<unsigned char>(face.tint.g * light),
                               static_cast<unsigned char>(face.tint.b * light), face.tint.a);
                    Vector3 p = Vector3Add({static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z)},
                                           face_corner(block, f, c));
                    rlTexCoord2f((face.tile_x + CORNER_U[c]) * 16.0f / aw, (face.tile_y + CORNER_V[c]) * 16.0f / ah);
                    rlVertex3f(p.x, p.y, p.z);
                }
            }
        }
        rlEnd();
        rlSetTexture(0);
    }

    // A titled box to draw a little preview in; returns its inside.
    Rectangle preview_box(Rectangle bounds, const std::string& title)
    {
        DrawRectangleRec(bounds, Color{24, 24, 28, 235});
        DrawRectangleLinesEx(bounds, 1.0f, gui_color(DEFAULT, BORDER_COLOR_NORMAL));
        DrawTextEx(editor_text::font(), title.c_str(), {bounds.x + 8, bounds.y + 5}, 16, 1, gui_color(DEFAULT, TEXT_COLOR_NORMAL));
        return {bounds.x + 4, bounds.y + ROW, bounds.width - 8, bounds.height - ROW - 4};
    }
}

void ModelEditor::draw_block_game_view(Rectangle bounds)
{
    const Rectangle inside = preview_box(bounds, tr("editor.block_in_game"));
    if (selected_block < 0) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    const int width = std::max(1, static_cast<int>(inside.width));
    const int height = std::max(1, static_cast<int>(inside.height));
    if (block_game_texture.id == 0 || block_game_texture.texture.width != width || block_game_texture.texture.height != height) {
        if (block_game_texture.id != 0) UnloadRenderTexture(block_game_texture);
        block_game_texture = LoadRenderTexture(width, height);
    }

    // Two of it side by side on a grass floor.
    const block_file::BlockFile* floor = nullptr;
    for (const block_file::BlockFile& candidate : blocks) {
        if (candidate.name == "grass") floor = &candidate;
    }
    if (!floor) floor = &block;
    Scene scene;
    for (int x = -3; x <= 4; ++x) {
        for (int z = -3; z <= 3; ++z) scene.cells.push_back({x, -1, z, floor});
    }
    scene.cells.push_back({0, 0, 0, &block});
    scene.cells.push_back({1, 0, 0, &block});

    // A couple of steps away, a little above them, through the game's own
    // 60-degree field of view.
    Camera3D cam{};
    cam.position = {-0.55f, 1.05f, 2.25f};
    cam.target = {0.5f, 0.05f, 0.0f};
    cam.up = {0, 1, 0};
    cam.fovy = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const Texture2D& atlas = terrain_atlas();
    BeginTextureMode(block_game_texture);
    ClearBackground(GAME_SKY);
    BeginMode3D(cam);
    BeginShaderMode(entity_cutout_shader()); // the world's shader drops see-through texels too
    draw_scene(scene, atlas, false);
    rlDrawRenderBatchActive();
    rlDisableDepthMask(); // blended faces don't hide what's behind them
    draw_scene(scene, atlas, true);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndShaderMode();
    EndMode3D();
    EndTextureMode();

    DrawTexturePro(block_game_texture.texture, {0, 0, static_cast<float>(width), -static_cast<float>(height)}, inside, {0, 0},
                   0.0f, WHITE);
}

void ModelEditor::draw_block_inventory_icon(Rectangle bounds)
{
    const Rectangle inside = preview_box(bounds, tr_format("editor.block_in_inventory", {std::to_string(game_ui_scale)}));
    if (selected_block < 0) return;
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];

    // One inventory slot at the game's own size (16-pixel icons, x2, x the
    // UI scale): Minecraft's grey slot with its sunken edge.
    const float scale = 2.0f * static_cast<float>(game_ui_scale);
    const float slot = 18.0f * scale;
    const Rectangle cell = {inside.x + PAD, inside.y + (inside.height - slot) * 0.5f, slot, slot};
    DrawRectangleRec(cell, Color{139, 139, 139, 255});
    DrawRectangleRec({cell.x, cell.y, cell.width, scale}, Color{55, 55, 55, 255});
    DrawRectangleRec({cell.x, cell.y, scale, cell.height}, Color{55, 55, 55, 255});
    DrawRectangleRec({cell.x, cell.y + cell.height - scale, cell.width, scale}, WHITE);
    DrawRectangleRec({cell.x + cell.width - scale, cell.y, scale, cell.height}, WHITE);

    const Texture2D& atlas = terrain_atlas();
    auto face = [&](int f) {
        const block_file::Face& source = block.faces[f];
        return CubeIconFace{{source.tile_x / 16.0f, source.tile_y / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f}, source.tint};
    };
    const Rectangle icon = {cell.x + scale, cell.y + scale, 16.0f * scale, 16.0f * scale};
    draw_cube_icon(icon, atlas, face(0), face(3), face(4), block.side_inset / 16.0f);
    label({cell.x + cell.width + PAD, inside.y, inside.width - cell.width - PAD * 2, inside.height}, display_name(block.name));
}
