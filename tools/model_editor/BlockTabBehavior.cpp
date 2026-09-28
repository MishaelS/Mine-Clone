// The model editor's "Blocks" tab, continued: making new blocks (from a
// template, as a copy), deleting and renaming them, and the "Behavior"
// panel - a block's Lua script, its state properties, what it can stand on,
// what it grows into.
#include "ModelEditor.hpp"
#include "EditorStyle.hpp"
#include "EditorText.hpp"
#include "EditorWidgets.hpp"
#include "content/StructureFile.hpp"
#include "core/BlockShape.hpp"

#include "raygui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>

namespace {

    using editor_text::tr;
    using editor_text::tr_format;
    using editor_ui::block_display_name;
    using editor_ui::draw_hint;
    using namespace editor_style;

    // What "New" starts from: a copy of an existing block of that kind,
    // everything set up the way that kind needs - a two-cell one both of
    // its halves.
    struct BlockTemplate {
        const char* key;    // editor.block_template.<key>
        const char* source; // the block copied
    };
    constexpr BlockTemplate BLOCK_TEMPLATES[] = {
        {"cube", "stone"},         {"log", "oak_log"},       {"front", "pumpkin"},        {"glass", "glass"},
        {"leaves", "foliage"},     {"light", "glowstone"},   {"slab", "oak_slab"},        {"stairs", "oak_stairs"},
        {"trapdoor", "oak_trapdoor"}, {"cake", "cake"},      {"torch", "torch"},          {"plant", "oak_sapling"},
        {"door", "oak_door_lower"},   {"bed", "bed_foot"},
    };
    constexpr int TEMPLATE_COUNT = static_cast<int>(sizeof(BLOCK_TEMPLATES) / sizeof(BLOCK_TEMPLATES[0]));

    // Bits one state property with values 0..max takes.
    int property_bits(int max)
    {
        int bits = 1;
        while ((1 << bits) <= max && bits < 32) ++bits;
        return bits;
    }

    std::string script_path(const std::string& name)
    {
        return std::string(ASSETS_PATH) + "scripts/" + name + ".lua";
    }

    // "Farm/Wheat.lua " -> "farm/wheat": as a block file names its script.
    std::string clean_script_name(std::string name)
    {
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }), name.end());
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0) name.resize(name.size() - 4);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return name;
    }

    bool file_exists(const std::string& path)
    {
        std::error_code error;
        return std::filesystem::exists(path, error);
    }
}

// --------------------------------------------------------- New / delete --

bool ModelEditor::block_is_builtin(int index) const
{
    if (index < 0 || index >= static_cast<int>(blocks.size())) return false;
    return blocks[static_cast<size_t>(index)].id < static_cast<int>(BlockType::Count);
}

std::string ModelEditor::free_block_name(const std::string& base) const
{
    auto taken = [&](const std::string& name) {
        return std::any_of(blocks.begin(), blocks.end(), [&](const block_file::BlockFile& block) { return block.name == name; }) ||
               file_exists(block_file::directory() + name + ".json");
    };
    std::string name = base;
    for (int n = 2; taken(name); ++n) name = base + "_" + std::to_string(n);
    return name;
}

void ModelEditor::add_block_from_template(int template_index)
{
    if (template_index < 0 || template_index >= TEMPLATE_COUNT) return;
    const BlockTemplate& chosen = BLOCK_TEMPLATES[template_index];
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].name == chosen.source) {
            copy_block(static_cast<int>(i), std::string("new_") + chosen.key);
            return;
        }
    }
    set_status(tr_format("editor.block_template_missing", {chosen.source}));
}

void ModelEditor::duplicate_block()
{
    copy_block(selected_block, "");
}

bool ModelEditor::copy_block(int index, std::string base)
{
    if (index < 0 || index >= static_cast<int>(blocks.size())) return false;
    commit_block_history(true);
    const block_file::BlockFile original = blocks[static_cast<size_t>(index)];

    // A two-cell block (a door, a bed) is copied whole: both halves, each
    // pointing at the other copy.
    std::optional<block_file::BlockFile> partner;
    if (is_pair_kind(static_cast<BlockShapeKind>(original.shape))) {
        for (const block_file::BlockFile& other : blocks) {
            if (other.name == original.partner && other.name != original.name) partner = other;
        }
    }
    // Free ids for it (and its other half).
    std::vector<int> ids;
    for (int id = static_cast<int>(BlockType::Count); id < MAX_BLOCK_TYPES && ids.size() < (partner ? 2u : 1u); ++id) {
        if (std::none_of(blocks.begin(), blocks.end(), [&](const block_file::BlockFile& block) { return block.id == id; })) ids.push_back(id);
    }
    if (ids.size() < (partner ? 2u : 1u)) {
        set_status(tr("editor.block_no_free_id"));
        return false;
    }

    if (base.empty()) {
        base = original.name;
        // "oak_door_lower" -> "oak_door"
        const std::string suffix = std::string("_") + block_file::half_id(original.shape, original.half);
        if (partner && base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
            base.resize(base.size() - suffix.size());
        }
        base += "_copy";
    }

    block_file::BlockFile copy = original;
    copy.id = ids[0];
    copy.script.clear();
    if (partner) {
        block_file::BlockFile other = *partner;
        other.id = ids[1];
        other.script.clear();
        copy.name = free_block_name(base + "_" + block_file::half_id(copy.shape, copy.half));
        other.name = free_block_name(base + "_" + block_file::half_id(other.shape, other.half));
        copy.partner = other.name;
        other.partner = copy.name;
        blocks.push_back(copy);
        blocks.push_back(other);
    } else {
        copy.name = free_block_name(base);
        blocks.push_back(copy);
    }
    block_dirty.resize(blocks.size(), true);
    block_file_names.resize(blocks.size());
    const int first = static_cast<int>(blocks.size()) - (partner ? 2 : 1);
    select_block(first);
    set_status(tr_format("editor.block_created", {blocks[static_cast<size_t>(first)].name}));
    return true;
}

void ModelEditor::delete_block()
{
    if (selected_block < 0) return;
    if (block_is_builtin(selected_block)) {
        set_status(tr("editor.block_builtin_no_delete"));
        return;
    }
    if (pending_block_delete != selected_block) {
        pending_block_delete = selected_block; // a second click deletes
        set_status(tr_format("editor.block_delete_confirm", {blocks[static_cast<size_t>(selected_block)].name}));
        return;
    }
    // Both halves of a two-cell block.
    std::vector<int> doomed = {selected_block};
    const block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
    if (is_pair_kind(static_cast<BlockShapeKind>(block.shape))) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].name == block.partner && static_cast<int>(i) != selected_block && !block_is_builtin(static_cast<int>(i))) {
                doomed.push_back(static_cast<int>(i));
            }
        }
    }
    std::sort(doomed.rbegin(), doomed.rend());
    const std::string name = block.name;
    for (int index : doomed) {
        const std::string& file_name = block_file_names[static_cast<size_t>(index)];
        if (!file_name.empty()) {
            std::error_code error;
            std::filesystem::remove(block_file::directory() + file_name + ".json", error);
        }
        blocks.erase(blocks.begin() + index);
        block_dirty.erase(block_dirty.begin() + index);
        block_file_names.erase(block_file_names.begin() + index);
    }
    // Its steps point at indices that moved.
    block_undo_stack.clear();
    block_redo_stack.clear();
    block_uncommitted = false;
    block_committed_index = -1;
    pending_block_delete = -1;
    selected_block = -1;
    select_block(blocks.empty() ? -1 : std::min(doomed.back(), static_cast<int>(blocks.size()) - 1));
    set_status(tr_format("editor.block_deleted", {name}));
}

bool ModelEditor::rename_block(const std::string& new_name)
{
    if (selected_block < 0) return false;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
    if (new_name == block.name) return false;
    if (block_is_builtin(selected_block)) {
        set_status(tr("editor.block_builtin_hint"));
        return false;
    }
    if (!structure_file::valid_name(new_name)) {
        set_status(tr("editor.block_name_invalid"));
        return false;
    }
    for (const block_file::BlockFile& other : blocks) {
        if (other.name == new_name) {
            set_status(tr_format("editor.block_name_taken", {new_name}));
            return false;
        }
    }
    const std::string old_name = block.name;
    // Its other half follows it; its titles move to the new name.
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (static_cast<int>(i) != selected_block && blocks[i].partner == old_name) {
            blocks[i].partner = new_name;
            block_dirty[i] = true;
        }
    }
    for (const std::string& language : editor_text::languages()) {
        const std::string title = editor_text::translation(language, "block." + old_name);
        if (!title.empty()) editor_text::set_translation(language, "block." + new_name, title);
    }
    block.name = new_name;
    mark_block_dirty();
    return true;
}

bool ModelEditor::draw_block_templates(Rectangle button)
{
    if (!block_templates_open) return false;
    const float row = ROW + 2;
    const Rectangle list = {button.x, button.y + button.height + 2, std::max(button.width, 230.0f), row * TEMPLATE_COUNT + PAD};
    DrawRectangleRec({list.x + 3, list.y + 3, list.width, list.height}, Fade(BLACK, 0.35f)); // its shadow
    GuiPanel(list, nullptr);
    const Vector2 mouse = GetMousePosition();
    for (int t = 0; t < TEMPLATE_COUNT; ++t) {
        const Rectangle r = {list.x + PAD * 0.5f, list.y + PAD * 0.5f + t * row, list.width - PAD, ROW};
        const bool hovered = CheckCollisionPointRec(mouse, r);
        if (hovered) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
        label({r.x + 8, r.y, r.width - 16, r.height}, tr(std::string("editor.block_template.") + BLOCK_TEMPLATES[t].key));
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            block_templates_open = false;
            add_block_from_template(t);
            return true;
        }
    }
    const bool over = CheckCollisionPointRec(mouse, list) || CheckCollisionPointRec(mouse, button);
    if (!over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) block_templates_open = false; // a click elsewhere closes it
    return over;
}

// ------------------------------------------------------------- Scripts --

void ModelEditor::refresh_script_list()
{
    script_names.clear();
    const std::filesystem::path root = std::string(ASSETS_PATH) + "scripts";
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
        std::filesystem::path relative = std::filesystem::relative(entry.path(), root, error);
        relative.replace_extension();
        script_names.push_back(relative.generic_string());
    }
    std::sort(script_names.begin(), script_names.end());
    script_list_loaded = true;
}

// ------------------------------------------------------------- Behavior --

void ModelEditor::draw_block_behavior_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 800.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &block_behavior_scroll, &view);
    if (selected_block < 0) return;
    block_file::BlockFile& block = blocks[static_cast<size_t>(selected_block)];
    if (!script_list_loaded) refresh_script_list();

    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    const float label_w = 150.0f;
    float y = view.y + block_behavior_scroll.y + PAD;
    auto fold = [&](const std::string& title) { return editor_ui::fold_header(x, y, width, title, folded_sections); };
    auto hint = [&](const std::string& text, bool error = false) { y = draw_hint(x, y, width, text, error); };

    // Its script: what it does, in Lua.
    if (fold(tr("editor.behavior_script"))) {
        label({x, y, label_w, ROW}, tr("editor.behavior_script_name"));
        std::string script = block.script;
        if (string_field({x + label_w, y, width - label_w, ROW}, script)) {
            block.script = clean_script_name(script);
            mark_block_dirty();
        }
        y += ROW + 2;
        const bool exists = !block.script.empty() && file_exists(script_path(block.script));
        if (block.script.empty()) hint(tr("editor.behavior_script_none"));
        else if (exists) hint("assets/scripts/" + block.script + ".lua");
        else hint(tr_format("editor.behavior_script_missing", {block.script}), true);

        const float third = (width - GAP * 2) / 3.0f;
        GuiSetState(exists ? STATE_DISABLED : STATE_NORMAL);
        if (GuiButton({x, y, third, ROW}, tr("editor.behavior_script_create").c_str()) && !exists) {
            if (block.script.empty()) {
                block.script = block.name;
                mark_block_dirty();
            }
            const std::string path = script_path(block.script);
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
            std::ofstream out(path, std::ios::binary);
            out << "-- " << block_display_name(block.name) << " (" << block.name << ") - what it does.\n"
                << "-- Every event and world.* call: assets/scripts/README.md\n"
                << "-- Edited while the game runs? Type /reload in its chat.\n"
                << "return {\n"
                << "    -- on_placed = function(pos) end,\n"
                << "    -- on_broken = function(pos) end,\n"
                << "    -- on_use = function(pos, use) return false end,\n"
                << "    -- on_random_tick = function(pos) end,\n"
                << "    -- on_scheduled_tick = function(pos) end,\n"
                << "    -- on_neighbor_changed = function(pos, from) end,\n"
                << "    -- can_place = function(pos) return true end,\n"
                << "    -- can_stay = function(pos) return true end,\n"
                << "}\n";
            set_status(out ? tr_format("editor.behavior_script_created", {path}) : tr_format("editor.block_save_failed", {path}));
            refresh_script_list();
        }
        GuiSetState(exists ? STATE_NORMAL : STATE_DISABLED);
        if (GuiButton({x + third + GAP, y, third, ROW}, tr("editor.behavior_script_open").c_str()) && exists) {
            OpenURL(script_path(block.script).c_str());
        }
        GuiSetState(block.script.empty() ? STATE_DISABLED : STATE_NORMAL);
        if (GuiButton({x + (third + GAP) * 2, y, third, ROW}, tr("editor.behavior_script_detach").c_str()) && !block.script.empty()) {
            block.script.clear();
            mark_block_dirty();
        }
        GuiSetState(STATE_NORMAL);
        y += ROW + GAP;

        // Every script there is - click one to give it to this block.
        if (!script_names.empty()) {
            label({x, y, width - 90, ROW}, tr("editor.behavior_script_list"));
            if (GuiButton({x + width - 90, y, 90, ROW}, tr("editor.behavior_script_refresh").c_str())) refresh_script_list();
            y += ROW + 2;
            const Vector2 mouse = GetMousePosition();
            for (const std::string& name : script_names) {
                const Rectangle r = {x, y, width, ROW - 4};
                const bool hovered = mouse_inside && CheckCollisionPointRec(mouse, r);
                if (name == block.script) DrawRectangleRec(r, Fade(SELECTION, 0.35f));
                else if (hovered) DrawRectangleRec(r, Fade(WHITE, 0.06f));
                label({r.x + 8, r.y - 2, r.width - 16, ROW}, name);
                if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && name != block.script) {
                    block.script = name;
                    mark_block_dirty();
                }
                y += ROW - 2;
            }
            y += GAP;
        }
        hint(tr("editor.behavior_script_hint"));
        y += GAP;
    }

    // Values each placed one carries - a crop's age.
    if (fold(tr("editor.behavior_properties"))) {
        const float delete_w = ROW;
        const float number_w = 84.0f;
        const float name_w = width - number_w * 2 - delete_w - GAP * 3;
        if (!block.properties.empty()) {
            label({x, y, name_w, ROW}, tr("editor.behavior_property_name"));
            label({x + name_w + GAP, y, number_w, ROW}, tr("editor.behavior_property_max"));
            label({x + name_w + number_w + GAP * 2, y, number_w, ROW}, tr("editor.behavior_property_default"));
            y += ROW;
        }
        int remove = -1;
        int bits = 0;
        for (size_t i = 0; i < block.properties.size(); ++i) {
            block_file::StateProperty& property = block.properties[i];
            std::string name = property.name;
            if (string_field({x, y, name_w, ROW}, name)) {
                property.name = structure_file::valid_name(name) ? name : property.name;
                if (!structure_file::valid_name(name)) set_status(tr("editor.block_name_invalid"));
                mark_block_dirty();
            }
            if (int_field({x + name_w + GAP, y, number_w, ROW}, property.max, 1, 65535)) {
                property.default_value = std::min(property.default_value, property.max);
                mark_block_dirty();
            }
            if (int_field({x + name_w + number_w + GAP * 2, y, number_w, ROW}, property.default_value, 0, property.max)) mark_block_dirty();
            if (GuiButton({x + width - delete_w, y, delete_w, ROW}, "x")) remove = static_cast<int>(i);
            bits += property_bits(property.max);
            y += ROW + 2;
        }
        if (remove >= 0) {
            block.properties.erase(block.properties.begin() + remove);
            mark_block_dirty();
        }
        y += GAP;
        if (GuiButton({x, y, width, ROW}, tr("editor.behavior_property_add").c_str())) {
            block_file::StateProperty property;
            property.name = "value";
            for (int n = 2; std::any_of(block.properties.begin(), block.properties.end(),
                                        [&](const block_file::StateProperty& other) { return other.name == property.name; });
                 ++n) {
                property.name = "value_" + std::to_string(n);
            }
            block.properties.push_back(property);
            mark_block_dirty();
        }
        y += ROW + GAP;
        // Names must differ, and all of them fit in 32 bits.
        bool duplicate = false;
        for (size_t i = 0; i < block.properties.size(); ++i) {
            for (size_t j = i + 1; j < block.properties.size(); ++j) duplicate = duplicate || block.properties[i].name == block.properties[j].name;
        }
        if (duplicate) hint(tr("editor.behavior_property_duplicate"), true);
        hint(tr_format("editor.behavior_property_bits", {std::to_string(bits)}), bits > 32);
        hint(tr("editor.behavior_property_hint"));
        y += GAP;
    }

    // Where it can be placed: only on these blocks (a plant's soil).
    if (fold(tr("editor.behavior_placement"))) {
        std::string soil;
        for (size_t i = 0; i < block.placed_on.size(); ++i) soil += (i ? ", " : "") + block.placed_on[i];
        label({x, y, label_w, ROW}, tr("editor.block_placed_on"));
        if (string_field({x + label_w, y, width - label_w, ROW}, soil)) {
            block.placed_on.clear();
            std::string name;
            for (size_t i = 0; i <= soil.size(); ++i) {
                const char c = i < soil.size() ? soil[i] : ',';
                if (c == ',' || c == ' ' || c == ';') {
                    if (!name.empty()) block.placed_on.push_back(name);
                    name.clear();
                } else {
                    name += c;
                }
            }
            mark_block_dirty();
        }
        y += ROW + 2;
        std::string unknown;
        for (const std::string& name : block.placed_on) {
            const bool found = std::any_of(blocks.begin(), blocks.end(), [&](const block_file::BlockFile& other) { return other.name == name; });
            if (!found) unknown += (unknown.empty() ? "" : ", ") + name;
        }
        if (!unknown.empty()) hint(tr_format("editor.block_placed_on_unknown", {unknown}), true);
        else hint(tr(block.placed_on.empty() ? "editor.block_placed_on_anywhere" : "editor.block_placed_on_hint"));
        hint(tr("editor.behavior_placement_hint"));
        y += GAP;
    }

    // What it grows into - set on the structure's side (the Structures tab).
    if (fold(tr("editor.behavior_growth"))) {
        if (grows_into_for != selected_block) {
            grows_into.clear();
            for (const structure_file::StructureFile& structure : structure_file::load_all()) {
                if (structure.grows_from == block.name) grows_into.push_back(structure.name);
            }
            grows_into_for = selected_block;
        }
        if (grows_into.empty()) {
            hint(tr("editor.behavior_growth_none"));
        } else {
            std::string names;
            for (const std::string& name : grows_into) names += (names.empty() ? "" : ", ") + name;
            hint(tr_format("editor.behavior_growth_into", {names}));
        }
        y += GAP;
    }

    content_height = y - (view.y + block_behavior_scroll.y) + PAD;
    EndScissorMode();
    GuiUnlock();
}
