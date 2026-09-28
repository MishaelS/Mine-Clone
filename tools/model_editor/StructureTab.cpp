// The model editor's "Structures" tab: every structure the game loads from
// assets/structures/<name>.json (content/StructureFile.hpp) - listed on the
// left over the blocks to build with, built block by block in the middle,
// its variants and where the world generator puts it on the right.
#include "ModelEditor.hpp"
#include "BlockDraw.hpp"
#include "EditorStyle.hpp"
#include "EditorText.hpp"
#include "EditorWidgets.hpp"

#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {

    using editor_text::tr;
    using editor_text::tr_format;
    using editor_ui::block_display_name;
    using editor_ui::draw_hint;
    using namespace editor_style;

    constexpr float STRUCTURE_LIST_WIDTH  = 290.0f;
    constexpr float STRUCTURE_PANEL_WIDTH = 390.0f;
    constexpr float LIST_ROW              = 30.0f;
    constexpr int   GROUND_MARGIN         = 3;   // ground shown this far round the structure
    constexpr int   MAX_REACH             = 64;  // how far from the origin a block can be put
    constexpr int   CHUNK_WIDTH           = 16;  // the game's CHUNK_SIZE: a structure never crosses a chunk

    constexpr Color REQUIRED_COLOR = {255, 90, 80, 255};
    constexpr Color ANY_COLOR      = {255, 210, 60, 255};
    constexpr Color REMOVE_COLOR   = {255, 70, 70, 255};
    constexpr Color UNKNOWN_COLOR  = {255, 60, 255, 255};
    constexpr Color AXIS_X         = {200, 70, 70, 255};
    constexpr Color AXIS_Z         = {110, 170, 60, 255};

    constexpr const char* TOOL_KEYS[4] = {"editor.structure_tool.place", "editor.structure_tool.paint",
                                          "editor.structure_tool.remove", "editor.structure_tool.pick"};

    Color gui_color(int control, int property) {
        return GetColor(static_cast<unsigned int>(GuiGetStyle(control, property)));
    }


    std::string lower(std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }


    // "grass, sand" <-> {"grass", "sand"}
    std::vector<std::string> split_names(const std::string& text) {
        std::vector<std::string> names;
        std::string name;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == ',') {
                if (!name.empty()) names.push_back(name);
                name.clear();
            } else if (!std::isspace(static_cast<unsigned char>(text[i]))) {
                name += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            }
        }
        return names;
    }
    std::string join_names(const std::vector<std::string>& names) {
        std::string text;
        for (const std::string& name : names) text += (text.empty() ? "" : ", ") + name;
        return text;
    }

    Vector3 cell_center(int x, int y, int z) {
        return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    }
}

// ---------------------------------------------------------------- Setup --

const block_file::BlockFile* ModelEditor::block_named(const std::string& name) const
{
    for (const block_file::BlockFile& block : blocks) {
        if (block.name == name) return &block;
    }
    return nullptr;
}

structure_file::StructureFile* ModelEditor::current_structure()
{
    if (selected_structure < 0 || selected_structure >= static_cast<int>(structures.size())) return nullptr;
    return &structures[static_cast<size_t>(selected_structure)];
}

structure_file::Variant* ModelEditor::current_variant()
{
    structure_file::StructureFile* structure = current_structure();
    if (!structure || structure->variants.empty()) return nullptr;
    structure_variant = std::clamp(structure_variant, 0, static_cast<int>(structure->variants.size()) - 1);
    return &structure->variants[static_cast<size_t>(structure_variant)];
}

void ModelEditor::load_structures()
{
    if (!blocks_loaded) load_blocks(); // built from the blocks tab's blocks
    structures = structure_file::load_all();
    for (structure_file::StructureFile& structure : structures) {
        if (structure.variants.empty()) structure.variants.emplace_back(); // always one to build in
    }
    structure_file_names.clear();
    for (const structure_file::StructureFile& structure : structures) structure_file_names.push_back(structure.name);
    structure_dirty.assign(structures.size(), false);
    structures_loaded = true;
    structure_undo_stack.clear();
    structure_redo_stack.clear();
    structure_committed_index = -1;
    structure_uncommitted = false;
    if (!block_named(brush_block) && !blocks.empty()) brush_block = blocks.front().name;
    select_structure(structures.empty() ? -1 : 0);
}

void ModelEditor::select_structure(int index)
{
    commit_structure_history(true);
    selected_structure = index;
    structure_variant = 0;
    pending_structure_delete = -1;
    editing_widget = -1;
    if (index >= 0) {
        structure_committed = structures[static_cast<size_t>(index)];
        structure_committed_index = index;
    } else {
        structure_committed_index = -1;
    }
    frame_structure();
}

void ModelEditor::save_structure(int index)
{
    if (index < 0 || index >= static_cast<int>(structures.size())) return;
    const structure_file::StructureFile& structure = structures[static_cast<size_t>(index)];
    const std::string path = structure_file::directory() + structure.name + ".json";
    if (!structure_file::save(structure, path)) {
        set_status(tr_format("editor.structure_save_failed", {path}));
        return;
    }
    // Renamed: its old file goes - unless another structure is called that now.
    std::string& file_name = structure_file_names[static_cast<size_t>(index)];
    const bool old_taken = std::any_of(structures.begin(), structures.end(),
                                       [&](const structure_file::StructureFile& other) { return other.name == file_name; });
    if (!file_name.empty() && file_name != structure.name && !old_taken) {
        std::error_code error;
        std::filesystem::remove(structure_file::directory() + file_name + ".json", error);
    }
    // A structure renamed away from this name no longer owns this file.
    for (size_t i = 0; i < structures.size(); ++i) {
        if (static_cast<int>(i) != index && structure_file_names[i] == structure.name) structure_file_names[i].clear();
    }
    file_name = structure.name;
    structure_dirty[static_cast<size_t>(index)] = false;
    set_status(tr_format("editor.structure_saved", {structure.name}));
}

void ModelEditor::save_all_structures()
{
    int saved = 0;
    for (size_t i = 0; i < structures.size(); ++i) {
        if (!structure_dirty[i]) continue;
        save_structure(static_cast<int>(i));
        if (!structure_dirty[i]) ++saved;
    }
    set_status(tr_format("editor.structures_saved_all", {std::to_string(saved)}));
}

void ModelEditor::add_structure(bool copy_selected)
{
    commit_structure_history(true);
    structure_file::StructureFile structure;
    std::string base = "structure";
    if (copy_selected && current_structure()) {
        structure = *current_structure();
        base = structure.name + "_copy";
    } else {
        // Empty, standing on grass - and found nowhere until it's given a chance.
        structure.variants.emplace_back();
        structure.placed_on = {"grass"};
    }
    // A name no structure and no file has.
    auto taken = [&](const std::string& name) {
        for (const structure_file::StructureFile& other : structures) {
            if (other.name == name) return true;
        }
        std::error_code error;
        return std::filesystem::exists(structure_file::directory() + name + ".json", error);
    };
    std::string name = base;
    for (int n = 2; taken(name); ++n) name = base + "_" + std::to_string(n);
    structure.name = name;

    structures.push_back(std::move(structure));
    structure_file_names.emplace_back();
    structure_dirty.push_back(true);
    select_structure(static_cast<int>(structures.size()) - 1);
    set_status(tr_format("editor.structure_created", {name}));
}

void ModelEditor::delete_structure()
{
    if (!current_structure()) return;
    if (pending_structure_delete != selected_structure) {
        pending_structure_delete = selected_structure; // a second click deletes
        set_status(tr_format("editor.structure_delete_confirm", {current_structure()->name}));
        return;
    }
    const size_t index = static_cast<size_t>(selected_structure);
    const std::string name = structures[index].name;
    const std::string file_name = structure_file_names[index];
    structures.erase(structures.begin() + static_cast<std::ptrdiff_t>(index));
    structure_file_names.erase(structure_file_names.begin() + static_cast<std::ptrdiff_t>(index));
    structure_dirty.erase(structure_dirty.begin() + static_cast<std::ptrdiff_t>(index));
    const bool file_taken = std::any_of(structures.begin(), structures.end(),
                                        [&](const structure_file::StructureFile& other) { return other.name == file_name; });
    if (!file_name.empty() && !file_taken) {
        std::error_code error;
        std::filesystem::remove(structure_file::directory() + file_name + ".json", error);
    }
    // Its steps point at indices that moved.
    structure_undo_stack.clear();
    structure_redo_stack.clear();
    structure_uncommitted = false;
    structure_committed_index = -1;
    selected_structure = -1;
    select_structure(std::min(static_cast<int>(index), static_cast<int>(structures.size()) - 1));
    set_status(tr_format("editor.structure_deleted", {name}));
}

void ModelEditor::rename_structure(const std::string& new_name)
{
    structure_file::StructureFile* structure = current_structure();
    if (!structure || new_name == structure->name) return;
    if (!structure_file::valid_name(new_name)) {
        set_status(tr("editor.structure_name_invalid"));
        return;
    }
    for (const structure_file::StructureFile& other : structures) {
        if (other.name == new_name) {
            set_status(tr_format("editor.structure_name_taken", {new_name}));
            return;
        }
    }
    structure->name = new_name;
    mark_structure_dirty();
}

// -------------------------------------------------------------- History --

void ModelEditor::mark_structure_dirty()
{
    if (selected_structure < 0) return;
    structure_dirty[static_cast<size_t>(selected_structure)] = true;
    structure_uncommitted = true;
}

void ModelEditor::commit_structure_history(bool force)
{
    if (!structure_uncommitted || structure_committed_index < 0) return;
    const bool mouse_busy = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (!force && (mouse_busy || typing())) return; // still in the middle of this change
    structure_undo_stack.push_back({structure_committed_index, structure_committed});
    if (structure_undo_stack.size() > 200) structure_undo_stack.erase(structure_undo_stack.begin());
    structure_redo_stack.clear();
    if (selected_structure >= 0) structure_committed = structures[static_cast<size_t>(selected_structure)];
    structure_committed_index = selected_structure;
    structure_uncommitted = false;
}

void ModelEditor::structure_undo()
{
    commit_structure_history(true);
    if (structure_undo_stack.empty()) {
        set_status(tr("editor.nothing_to_undo"));
        return;
    }
    const auto [index, state] = structure_undo_stack.back();
    structure_undo_stack.pop_back();
    structure_redo_stack.push_back({index, structures[static_cast<size_t>(index)]});
    structures[static_cast<size_t>(index)] = state;
    structure_dirty[static_cast<size_t>(index)] = true;
    selected_structure = index;
    structure_committed = state;
    structure_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.undone", {std::to_string(structure_undo_stack.size())}));
}

void ModelEditor::structure_redo()
{
    commit_structure_history(true);
    if (structure_redo_stack.empty()) {
        set_status(tr("editor.nothing_to_redo"));
        return;
    }
    const auto [index, state] = structure_redo_stack.back();
    structure_redo_stack.pop_back();
    structure_undo_stack.push_back({index, structures[static_cast<size_t>(index)]});
    structures[static_cast<size_t>(index)] = state;
    structure_dirty[static_cast<size_t>(index)] = true;
    selected_structure = index;
    structure_committed = state;
    structure_committed_index = index;
    editing_widget = -1;
    set_status(tr_format("editor.redone", {std::to_string(structure_redo_stack.size())}));
}

// ---------------------------------------------------------------- Frame --

void ModelEditor::run_structures_frame()
{
    if (!structures_loaded) load_structures();
    SetMouseCursor(MOUSE_CURSOR_DEFAULT); // a number field under the mouse sets its own

    const float width = static_cast<float>(GetScreenWidth()), height = static_cast<float>(GetScreenHeight());
    const float body = height - HEADER_HEIGHT;
    const float list_height = std::floor(body * 0.36f);
    const Rectangle list = {0, HEADER_HEIGHT, STRUCTURE_LIST_WIDTH, list_height};
    const Rectangle palette = {0, HEADER_HEIGHT + list_height, STRUCTURE_LIST_WIDTH, body - list_height};
    const Rectangle panel = {width - STRUCTURE_PANEL_WIDTH, HEADER_HEIGHT, STRUCTURE_PANEL_WIDTH, body};
    const Rectangle view = {STRUCTURE_LIST_WIDTH, HEADER_HEIGHT, width - STRUCTURE_LIST_WIDTH - STRUCTURE_PANEL_WIDTH, body};

    if (!typing()) {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) ||
                          IsKeyDown(KEY_RIGHT_SUPER);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool z = IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z);
        const bool y = IsKeyPressed(KEY_Y) || IsKeyPressedRepeat(KEY_Y);
        if (ctrl && IsKeyPressed(KEY_S)) save_structure(selected_structure);
        if (ctrl && z && !shift) structure_undo();
        else if (ctrl && ((z && shift) || y)) structure_redo();
        if (!ctrl) {
            if (IsKeyPressed(KEY_F)) frame_structure();
            constexpr int TOOL_KEYS_PRESSED[4] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
            for (int t = 0; t < 4; ++t) {
                if (IsKeyPressed(TOOL_KEYS_PRESSED[t])) structure_tool = t;
            }
        }
    }

    update_structure_view(view);
    draw_structure_view(view);

    BeginDrawing();
    ClearBackground(gui_color(DEFAULT, BACKGROUND_COLOR));
    DrawTexturePro(structure_view_texture.texture,
                   {0, 0, static_cast<float>(structure_view_texture.texture.width), -static_cast<float>(structure_view_texture.texture.height)},
                   view, {0, 0}, 0.0f, WHITE);
    // Over the view: what's open, and what's under the mouse.
    if (const structure_file::StructureFile* structure = current_structure()) {
        const structure_file::Bounds bounds = structure_file::bounds(*current_variant());
        std::string line = structure->name + "   " +
                           tr_format("editor.structure_variant_of", {std::to_string(structure_variant + 1),
                                                                     std::to_string(structure->variants.size())}) +
                           "   " + tr_format("editor.structure_block_count", {std::to_string(current_variant()->blocks.size())});
        if (!bounds.empty) {
            line += "   " + std::to_string(bounds.max_x - bounds.min_x + 1) + " x " + std::to_string(bounds.max_y - bounds.min_y + 1) +
                    " x " + std::to_string(bounds.max_z - bounds.min_z + 1);
        }
        label({view.x + PAD, view.y + PAD, view.width - PAD * 2, ROW}, line);
        label({view.x + PAD, view.y + PAD + ROW, view.width - PAD * 2, ROW},
              tr_format("editor.structure_tool_line", {tr(TOOL_KEYS[structure_tool]), block_display_name(brush_block)}));
        if (structure_hover.valid && structure_hover.on_block) {
            if (const structure_file::Block* block = structure_file::block_at(*current_variant(), structure_hover.x, structure_hover.y,
                                                                              structure_hover.z)) {
                std::string text = std::to_string(block->x) + ", " + std::to_string(block->y) + ", " + std::to_string(block->z) + "   " +
                                   block_display_name(block->block) + "   " +
                                   tr(std::string("editor.structure_replace.") + structure_file::REPLACE_IDS[block->replace]);
                if (block->required) text += "   " + tr("editor.structure_required");
                if (!block_named(block->block)) text += "   " + tr("editor.structure_unknown_block");
                label({view.x + PAD, view.y + PAD + ROW * 2, view.width - PAD * 2, ROW}, text);
            }
        }
    } else {
        label({view.x + PAD, view.y + PAD, view.width - PAD * 2, ROW}, tr("editor.structure_none"));
    }
    label({view.x + PAD, view.y + view.height - ROW - 4, view.width - PAD * 2, ROW}, tr("editor.structure_view_hint"));

    draw_structure_list(list);
    draw_structure_palette(palette);
    draw_structure_panel(panel);
    draw_structures_top_bar(top_bar_rect());
    draw_tabs();
    EndDrawing();
    commit_structure_history();
}

void ModelEditor::draw_structures_top_bar(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    float x = bounds.x + PAD;
    const float y = bounds.y + (bounds.height - ROW) * 0.5f;
    if (GuiButton({x, y, 100, ROW}, tr("editor.structure_new").c_str())) add_structure(false);
    x += 100 + GAP;
    GuiSetState(current_structure() ? STATE_NORMAL : STATE_DISABLED);
    if (GuiButton({x, y, 120, ROW}, tr("editor.structure_copy").c_str())) add_structure(true);
    x += 120 + GAP;
    const bool confirming = pending_structure_delete >= 0 && pending_structure_delete == selected_structure;
    if (GuiButton({x, y, 150, ROW}, tr(confirming ? "editor.structure_delete_sure" : "editor.structure_delete").c_str())) delete_structure();
    x += 150 + GAP * 3;
    label({x, y, 60, ROW}, tr("editor.name"));
    x += 60;
    if (structure_file::StructureFile* structure = current_structure()) {
        std::string name = structure->name;
        if (string_field({x, y, 190, ROW}, name)) rename_structure(name);
    }
    x += 190 + GAP * 3;
    if (GuiButton({x, y, 130, ROW}, tr("editor.block_save").c_str())) save_structure(selected_structure);
    x += 130 + GAP;
    GuiSetState(STATE_NORMAL);
    if (GuiButton({x, y, 150, ROW}, tr("editor.block_save_all").c_str())) save_all_structures();
    x += 150 + GAP * 3;

    int unsaved = 0;
    for (bool dirty_structure : structure_dirty) unsaved += dirty_structure ? 1 : 0;
    std::string line = unsaved > 0 ? tr_format("editor.structures_unsaved", {std::to_string(unsaved)}) : tr("editor.structures_restart_hint");
    if (GetTime() - status_time < 4.0) line = status;
    label({x, y, bounds.width - x - PAD, ROW}, line);
}

// ---------------------------------------------------------------- Lists --

void ModelEditor::draw_structure_list(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    GuiLine({bounds.x + PAD, bounds.y + PAD * 0.5f, bounds.width - PAD * 2, ROW}, tr("editor.structures").c_str());

    const Rectangle area = {bounds.x, bounds.y + PAD + ROW, bounds.width, bounds.height - PAD - ROW};
    Rectangle view{};
    const float content_height = static_cast<float>(structures.size()) * LIST_ROW + PAD;
    GuiScrollPanel(area, nullptr, {0, 0, area.width - 14, content_height}, &structure_list_scroll, &view);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const Vector2 mouse = GetMousePosition();
    const bool mouse_inside = CheckCollisionPointRec(mouse, view);
    for (size_t i = 0; i < structures.size(); ++i) {
        const int index = static_cast<int>(i);
        const Rectangle r = {view.x, view.y + structure_list_scroll.y + i * LIST_ROW, view.width, LIST_ROW};
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        const bool hovered = mouse_inside && CheckCollisionPointRec(mouse, r);
        if (index == selected_structure) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
        else if (hovered) DrawRectangleRec(r, Fade(WHITE, 0.06f));
        label({r.x + 10, r.y, r.width - 16, r.height}, structures[i].name + (structure_dirty[i] ? " *" : ""));
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && index != selected_structure) select_structure(index);
    }
    EndScissorMode();
}

void ModelEditor::draw_structure_palette(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    GuiLine({bounds.x + PAD, bounds.y + PAD * 0.5f, bounds.width - PAD * 2, ROW}, tr("editor.structure_palette").c_str());
    // Search by the game's name or the id.
    const float search_y = bounds.y + PAD + ROW;
    label({bounds.x + PAD, search_y, 70, ROW}, tr("editor.blocks_search"));
    string_field({bounds.x + PAD + 70, search_y, bounds.width - PAD * 2 - 70, ROW}, structure_palette_search);

    std::vector<const block_file::BlockFile*> shown;
    const std::string needle = lower(structure_palette_search);
    for (const block_file::BlockFile& block : blocks) {
        if (needle.empty() || lower(block.name).find(needle) != std::string::npos ||
            lower(block_display_name(block.name)).find(needle) != std::string::npos) {
            shown.push_back(&block);
        }
    }

    const float top = search_y + ROW + GAP;
    const Rectangle area = {bounds.x, top, bounds.width, bounds.y + bounds.height - top};
    Rectangle view{};
    const float content_height = static_cast<float>(shown.size()) * LIST_ROW + PAD;
    GuiScrollPanel(area, nullptr, {0, 0, area.width - 14, content_height}, &structure_palette_scroll, &view);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const Texture2D& terrain = terrain_atlas();
    const Texture2D& items = items_atlas();
    const Vector2 mouse = GetMousePosition();
    const bool mouse_inside = CheckCollisionPointRec(mouse, view);
    for (size_t row = 0; row < shown.size(); ++row) {
        const block_file::BlockFile& block = *shown[row];
        const Rectangle r = {view.x, view.y + structure_palette_scroll.y + row * LIST_ROW, view.width, LIST_ROW};
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        const bool hovered = mouse_inside && CheckCollisionPointRec(mouse, r);
        if (block.name == brush_block) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
        else if (hovered) DrawRectangleRec(r, Fade(WHITE, 0.06f));
        block_draw::draw_icon({r.x + 6, r.y + 3, 24, 24}, block, terrain, items);
        label({r.x + 34, r.y, r.width - 40, r.height}, block_display_name(block.name));
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            brush_block = block.name;
            // Picked to build with: back to a tool that uses it.
            if (structure_tool == 2 || structure_tool == 3) structure_tool = 0;
        }
    }
    EndScissorMode();
}

// ---------------------------------------------------------------- Panel --

void ModelEditor::draw_structure_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 900.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &structure_panel_scroll, &view);

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    const float label_w = 170.0f;
    float y = view.y + structure_panel_scroll.y + PAD;

    auto section = [&](const std::string& title) {
        GuiLine({x, y, width, ROW}, title.c_str());
        y += ROW;
    };
    auto small_hint = [&](const std::string& text, bool error = false) { y = draw_hint(x, y, width, text, error); };
    auto check = [&](const std::string& caption, bool& value) {
        bool checked = value;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, caption.c_str(), &checked);
        const bool changed = checked != value;
        value = checked;
        y += ROW + 2;
        return changed;
    };
    auto replace_items = [] {
        std::string items;
        for (int i = 0; i < structure_file::REPLACE_COUNT; ++i) {
            items += (i ? ";" : "") + tr(std::string("editor.structure_replace.") + structure_file::REPLACE_IDS[i]);
        }
        return items;
    };

    // What a click does, and with what.
    section(tr("editor.structure_brush"));
    {
        const float tool_width = (width - GAP * 3) / 4.0f;
        for (int t = 0; t < 4; ++t) {
            bool active = structure_tool == t;
            GuiToggle({x + t * (tool_width + GAP), y, tool_width, ROW}, tr(TOOL_KEYS[t]).c_str(), &active);
            if (active) structure_tool = t;
        }
        y += ROW + GAP;
        if (const block_file::BlockFile* block = block_named(brush_block)) {
            block_draw::draw_icon({x, y + 1, 24, 24}, *block, terrain_atlas(), items_atlas());
        }
        label({x + 32, y, width - 32, ROW}, block_display_name(brush_block) + "  (" + brush_block + ")");
        y += ROW + GAP;
        label({x, y, label_w, ROW}, tr("editor.structure_replace"));
        const std::string items = replace_items();
        GuiComboBox({x + label_w, y, width - label_w, ROW}, items.c_str(), &brush_replace);
        y += ROW + GAP;
        check(tr("editor.structure_required"), brush_required);
        small_hint(tr("editor.structure_brush_hint"));
    }

    structure_file::StructureFile* structure = current_structure();
    if (structure) {
        // Its variants: one is picked each time it's placed.
        y += GAP;
        section(tr("editor.structure_variants"));
        for (size_t v = 0; v < structure->variants.size(); ++v) {
            bool active = structure_variant == static_cast<int>(v);
            const std::string text = tr_format("editor.structure_variant_row",
                                               {std::to_string(v + 1), std::to_string(structure->variants[v].blocks.size())});
            GuiToggle({x, y, width, ROW}, text.c_str(), &active);
            if (active && structure_variant != static_cast<int>(v)) {
                structure_variant = static_cast<int>(v);
                editing_widget = -1;
            }
            y += ROW + 2;
        }
        y += GAP;
        const float third = (width - GAP * 2) / 3.0f;
        if (GuiButton({x, y, third, ROW}, tr("editor.structure_variant_add").c_str())) {
            structure->variants.emplace_back();
            structure_variant = static_cast<int>(structure->variants.size()) - 1;
            mark_structure_dirty();
        }
        if (GuiButton({x + third + GAP, y, third, ROW}, tr("editor.structure_variant_copy").c_str()) && current_variant()) {
            const structure_file::Variant copy = *current_variant();
            structure->variants.push_back(copy);
            structure_variant = static_cast<int>(structure->variants.size()) - 1;
            mark_structure_dirty();
        }
        GuiSetState(structure->variants.size() > 1 ? STATE_NORMAL : STATE_DISABLED);
        if (GuiButton({x + (third + GAP) * 2, y, third, ROW}, tr("editor.structure_variant_delete").c_str()) &&
            structure->variants.size() > 1) {
            structure->variants.erase(structure->variants.begin() + structure_variant);
            structure_variant = std::min(structure_variant, static_cast<int>(structure->variants.size()) - 1);
            mark_structure_dirty();
        }
        GuiSetState(STATE_NORMAL);
        y += ROW + GAP;

        // The whole variant moved a block along an axis, or turned about the origin.
        if (structure_file::Variant* variant = current_variant()) {
            label({x, y, width, ROW}, tr("editor.structure_move"));
            y += ROW;
            constexpr const char* MOVE_TEXT[6] = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};
            const float sixth = (width - GAP * 5) / 6.0f;
            for (int m = 0; m < 6; ++m) {
                if (!GuiButton({x + m * (sixth + GAP), y, sixth, ROW}, MOVE_TEXT[m])) continue;
                const int step = m % 2 == 0 ? -1 : 1;
                for (structure_file::Block& block : variant->blocks) {
                    (m < 2 ? block.x : m < 4 ? block.y : block.z) += step;
                }
                mark_structure_dirty();
            }
            y += ROW + GAP;
            const float half = (width - GAP) / 2.0f;
            if (GuiButton({x, y, half, ROW}, tr("editor.structure_rotate").c_str())) {
                for (structure_file::Block& block : variant->blocks) {
                    const int old_x = block.x;
                    block.x = -block.z;
                    block.z = old_x;
                }
                mark_structure_dirty();
            }
            if (GuiButton({x + half + GAP, y, half, ROW}, tr("editor.structure_clear").c_str()) && !variant->blocks.empty()) {
                variant->blocks.clear();
                mark_structure_dirty();
            }
            y += ROW + GAP;
        }

        // Where the world generator puts it.
        y += GAP;
        section(tr("editor.structure_generation"));
        label({x, y, label_w, ROW}, tr("editor.structure_on"));
        std::string on = join_names(structure->placed_on);
        if (string_field({x + label_w, y, width - label_w, ROW}, on)) {
            structure->placed_on = split_names(on);
            mark_structure_dirty();
        }
        y += ROW + GAP;
        for (const std::string& name : structure->placed_on) {
            if (!block_named(name)) small_hint(tr_format("editor.structure_no_block", {name}), true);
        }
        if (structure->placed_on.empty()) small_hint(tr("editor.structure_on_hint"));

        float total = 0.0f;
        for (int b = 0; b < structure_file::BIOME_COUNT; ++b) {
            float& chance = structure->chance[static_cast<size_t>(b)];
            label({x, y, label_w, ROW}, tr(std::string("editor.structure_biome.") + structure_file::BIOME_IDS[b]));
            if (float_field({x + label_w, y, width - label_w, ROW}, chance, 0.0f, 1.0f)) mark_structure_dirty();
            total += chance;
            y += ROW + GAP;
        }
        if (check(tr("editor.structure_tree_density"), structure->tree_density)) mark_structure_dirty();
        small_hint(tr("editor.structure_chance_hint"));

        // Why it wouldn't show up.
        const structure_file::Bounds bounds = structure_file::bounds(*structure);
        if (!bounds.empty && (bounds.max_x - bounds.min_x + 1 > CHUNK_WIDTH || bounds.max_z - bounds.min_z + 1 > CHUNK_WIDTH)) {
            small_hint(tr_format("editor.structure_too_wide", {std::to_string(CHUNK_WIDTH)}), true);
        }
        if (total <= 0.0f || structure->placed_on.empty()) small_hint(tr("editor.structure_never_generated"));
        bool any_required = false;
        for (const structure_file::Variant& variant : structure->variants) {
            for (const structure_file::Block& block : variant.blocks) any_required = any_required || block.required;
        }
        if (!any_required) small_hint(tr("editor.structure_no_required_hint"));

        // A sapling that grows into it.
        y += GAP;
        section(tr("editor.structure_growth"));
        label({x, y, label_w, ROW}, tr("editor.structure_grows_from"));
        std::string grows_from = structure->grows_from;
        if (string_field({x + label_w, y, width - label_w, ROW}, grows_from)) {
            const std::vector<std::string> names = split_names(grows_from);
            structure->grows_from = names.empty() ? std::string() : names.front();
            mark_structure_dirty();
        }
        y += ROW + GAP;
        if (!structure->grows_from.empty() && !block_named(structure->grows_from)) {
            small_hint(tr_format("editor.structure_no_block", {structure->grows_from}), true);
        }
        small_hint(tr("editor.structure_grows_hint"));
    }

    // How the view shows it.
    y += GAP;
    section(tr("editor.structure_view"));
    check(tr("editor.structure_show_ground"), structure_show_ground);
    check(tr("editor.structure_show_rules"), structure_show_rules);
    small_hint(tr("editor.structure_rules_hint"));
    check(tr("editor.structure_cut"), structure_cut);
    if (structure_cut) {
        label({x, y, label_w, ROW}, tr("editor.structure_cut_y"));
        int_field({x + label_w, y, width - label_w, ROW}, structure_cut_y, -MAX_REACH, MAX_REACH);
        y += ROW + GAP;
    }

    content_height = y - (view.y + structure_panel_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}

// ----------------------------------------------------------------- View --

Camera3D ModelEditor::structure_camera() const
{
    Camera3D cam{};
    cam.target = structure_target;
    cam.position = Vector3Add(structure_target, {structure_distance * std::cos(structure_pitch) * std::sin(structure_yaw),
                                                 structure_distance * std::sin(structure_pitch),
                                                 structure_distance * std::cos(structure_pitch) * std::cos(structure_yaw)});
    cam.up = {0, 1, 0};
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

void ModelEditor::frame_structure()
{
    structure_file::Variant* variant = current_variant();
    const structure_file::Bounds bounds = variant ? structure_file::bounds(*variant) : structure_file::Bounds{};
    if (bounds.empty) {
        structure_target = {0.0f, 1.0f, 0.0f};
        structure_distance = 12.0f;
        return;
    }
    structure_target = {(bounds.min_x + bounds.max_x) * 0.5f, (bounds.min_y + bounds.max_y) * 0.5f, (bounds.min_z + bounds.max_z) * 0.5f};
    const int size = std::max({bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y, bounds.max_z - bounds.min_z}) + 1;
    structure_distance = std::clamp(static_cast<float>(size) * 1.9f + 4.0f, 6.0f, 150.0f);
}

void ModelEditor::update_structure_view(Rectangle view)
{
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, view);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // Middle mouse (or Alt+left) orbits, with Shift pans; the wheel zooms.
    if (over && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || (alt && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
        structure_view_dragging = true;
        structure_view_panning = shift;
    }
    if (structure_view_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 delta = GetMouseDelta();
            if (structure_view_panning) {
                const Camera3D cam = structure_camera();
                const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, {0, 1, 0}));
                const Vector3 up = Vector3CrossProduct(right, forward);
                const float scale = structure_distance * 0.0016f;
                structure_target = Vector3Add(structure_target, Vector3Add(Vector3Scale(right, -delta.x * scale), Vector3Scale(up, delta.y * scale)));
            } else {
                structure_yaw -= delta.x * 0.008f;
                structure_pitch = std::clamp(structure_pitch + delta.y * 0.008f, -1.5f, 1.5f);
            }
        } else {
            structure_view_dragging = false;
        }
    }
    if (over) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) structure_distance = std::clamp(structure_distance * std::pow(0.88f, wheel), 2.0f, 150.0f);
    }

    // What's under the mouse: the nearest shown block's face, else the ground.
    structure_hover = {};
    structure_file::Variant* variant = current_variant();
    if (!over || !variant || structure_view_dragging) return;
    const Ray ray = GetScreenToWorldRayEx(Vector2Subtract(mouse, {view.x, view.y}), structure_camera(), static_cast<int>(view.width),
                                          static_cast<int>(view.height));
    float nearest = 1e9f;
    for (const structure_file::Block& block : variant->blocks) {
        if (structure_cut && block.y > structure_cut_y) continue;
        const Vector3 c = cell_center(block.x, block.y, block.z);
        const RayCollision hit = GetRayCollisionBox(ray, {Vector3Subtract(c, {0.5f, 0.5f, 0.5f}), Vector3Add(c, {0.5f, 0.5f, 0.5f})});
        if (!hit.hit || hit.distance >= nearest) continue;
        nearest = hit.distance;
        // The face it's aimed at: the hit point's farthest way out of the cell.
        const Vector3 d = Vector3Subtract(hit.point, c);
        int nx = 0, ny = 0, nz = 0;
        if (std::fabs(d.x) >= std::fabs(d.y) && std::fabs(d.x) >= std::fabs(d.z)) nx = d.x > 0 ? 1 : -1;
        else if (std::fabs(d.y) >= std::fabs(d.z)) ny = d.y > 0 ? 1 : -1;
        else nz = d.z > 0 ? 1 : -1;
        structure_hover = {true, true, block.x, block.y, block.z, block.x + nx, block.y + ny, block.z + nz};
    }
    if (ray.direction.y < 0.0f) {
        const float t = (-0.5f - ray.position.y) / ray.direction.y;
        if (t > 0.0f && t < nearest - 1e-3f) {
            const Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            const int cx = static_cast<int>(std::floor(p.x + 0.5f)), cz = static_cast<int>(std::floor(p.z + 0.5f));
            if (std::abs(cx) <= MAX_REACH && std::abs(cz) <= MAX_REACH) structure_hover = {true, false, cx, -1, cz, cx, 0, cz};
        }
    }
    if (!structure_hover.valid || alt) return;

    // Right click always takes a block out; left click uses the tool.
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    const int tool = right ? 2 : left ? structure_tool : -1;
    const StructureHover& hover = structure_hover;
    structure_file::Block* target = hover.on_block ? structure_file::block_at(*variant, hover.x, hover.y, hover.z) : nullptr;
    switch (tool) {
        case 0: {
            const bool in_reach = std::abs(hover.place_x) <= MAX_REACH && std::abs(hover.place_y) <= MAX_REACH &&
                                  std::abs(hover.place_z) <= MAX_REACH;
            if (in_reach && !structure_file::block_at(*variant, hover.place_x, hover.place_y, hover.place_z)) {
                variant->blocks.push_back({hover.place_x, hover.place_y, hover.place_z, brush_block, brush_replace, brush_required});
                mark_structure_dirty();
            }
            break;
        }
        case 1:
            if (target && (target->block != brush_block || target->replace != brush_replace || target->required != brush_required)) {
                target->block = brush_block;
                target->replace = brush_replace;
                target->required = brush_required;
                mark_structure_dirty();
            }
            break;
        case 2:
            if (target) {
                variant->blocks.erase(variant->blocks.begin() + (target - variant->blocks.data()));
                mark_structure_dirty();
                structure_hover.on_block = false;
            }
            break;
        case 3:
            if (target) {
                brush_block = target->block;
                brush_replace = target->replace;
                brush_required = target->required;
                structure_tool = 0;
            }
            break;
        default:
            break;
    }
}

void ModelEditor::draw_structure_view(Rectangle view)
{
    const int width = std::max(1, static_cast<int>(view.width));
    const int height = std::max(1, static_cast<int>(view.height));
    if (structure_view_texture.id == 0 || structure_view_texture.texture.width != width ||
        structure_view_texture.texture.height != height) {
        if (structure_view_texture.id != 0) UnloadRenderTexture(structure_view_texture);
        structure_view_texture = LoadRenderTexture(width, height);
    }

    const structure_file::StructureFile* structure = current_structure();
    const structure_file::Variant* variant = current_variant();
    auto shown = [&](const structure_file::Block& block) { return !structure_cut || block.y <= structure_cut_y; };

    // The ground round it: every variant's reach and a margin.
    structure_file::Bounds area = structure ? structure_file::bounds(*structure) : structure_file::Bounds{};
    if (area.empty) area = {0, 0, 0, 0, 0, 0, false};
    const int min_x = std::min(-GROUND_MARGIN, area.min_x - GROUND_MARGIN), max_x = std::max(GROUND_MARGIN, area.max_x + GROUND_MARGIN);
    const int min_z = std::min(-GROUND_MARGIN, area.min_z - GROUND_MARGIN), max_z = std::max(GROUND_MARGIN, area.max_z + GROUND_MARGIN);

    std::vector<block_draw::Cell> cells;
    std::vector<Vector3> unknown; // blocks named in the file the game doesn't have
    if (variant) {
        for (const structure_file::Block& block : variant->blocks) {
            if (!shown(block)) continue;
            if (const block_file::BlockFile* file = block_named(block.block)) cells.push_back({block.x, block.y, block.z, file});
            else unknown.push_back(cell_center(block.x, block.y, block.z));
        }
    }
    if (structure_show_ground) {
        // What it stands on (its first "on" block), as in the world.
        const block_file::BlockFile* ground = structure && !structure->placed_on.empty() ? block_named(structure->placed_on.front()) : nullptr;
        if (!ground) ground = block_named("grass");
        for (int x = min_x; ground && x <= max_x; ++x) {
            for (int z = min_z; z <= max_z; ++z) {
                if (variant && structure_file::block_at(*variant, x, -1, z)) continue;
                cells.push_back({x, -1, z, ground});
            }
        }
    }

    BeginTextureMode(structure_view_texture);
    ClearBackground(VIEWPORT_BACKGROUND);
    BeginMode3D(structure_camera());
    if (!cells.empty()) block_draw::draw_cells(cells, terrain_atlas());
    for (const Vector3& at : unknown) {
        DrawCube(at, 0.9f, 0.9f, 0.9f, Fade(UNKNOWN_COLOR, 0.35f));
        DrawCubeWires(at, 0.92f, 0.92f, 0.92f, UNKNOWN_COLOR);
    }

    // The ground's grid (with no ground drawn, the ground plane itself), the
    // axes from the origin, and the origin's cell.
    const float ground_y = structure_show_ground ? -0.49f : -0.5f;
    for (int x = min_x; x <= max_x + 1; ++x) {
        DrawLine3D({x - 0.5f, ground_y, min_z - 0.5f}, {x - 0.5f, ground_y, max_z + 0.5f}, Fade(WHITE, structure_show_ground ? 0.06f : 0.12f));
    }
    for (int z = min_z; z <= max_z + 1; ++z) {
        DrawLine3D({min_x - 0.5f, ground_y, z - 0.5f}, {max_x + 0.5f, ground_y, z - 0.5f}, Fade(WHITE, structure_show_ground ? 0.06f : 0.12f));
    }
    DrawLine3D({0, ground_y + 0.005f, 0}, {max_x + 0.5f, ground_y + 0.005f, 0}, AXIS_X);
    DrawLine3D({0, ground_y + 0.005f, 0}, {0, ground_y + 0.005f, max_z + 0.5f}, AXIS_Z);
    const float o = ground_y + 0.006f;
    DrawLine3D({-0.5f, o, -0.5f}, {0.5f, o, -0.5f}, WHITE);
    DrawLine3D({0.5f, o, -0.5f}, {0.5f, o, 0.5f}, WHITE);
    DrawLine3D({0.5f, o, 0.5f}, {-0.5f, o, 0.5f}, WHITE);
    DrawLine3D({-0.5f, o, 0.5f}, {-0.5f, o, -0.5f}, WHITE);

    // Its rules: required blocks and the ones placed over anything framed.
    if (variant && structure_show_rules) {
        for (const structure_file::Block& block : variant->blocks) {
            if (!shown(block)) continue;
            if (block.required) DrawCubeWires(cell_center(block.x, block.y, block.z), 1.02f, 1.02f, 1.02f, REQUIRED_COLOR);
            else if (block.replace == 2) DrawCubeWires(cell_center(block.x, block.y, block.z), 1.02f, 1.02f, 1.02f, ANY_COLOR);
        }
    }

    // Where a click lands.
    if (structure_hover.valid) {
        const StructureHover& hover = structure_hover;
        if (structure_tool == 0) {
            DrawCubeWires(cell_center(hover.place_x, hover.place_y, hover.place_z), 1.01f, 1.01f, 1.01f, SELECTION);
        } else if (hover.on_block) {
            const Color color = structure_tool == 2 ? REMOVE_COLOR : SELECTION;
            DrawCubeWires(cell_center(hover.x, hover.y, hover.z), 1.03f, 1.03f, 1.03f, color);
        }
    }
    EndMode3D();
    EndTextureMode();
}
