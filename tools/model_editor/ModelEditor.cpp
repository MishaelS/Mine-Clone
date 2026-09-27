#include "ModelEditor.hpp"
#include "EditorText.hpp"
#include "model/EntityModelRenderer.hpp"

#include "raygui.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

void editor_load_dark_style(); // RayGuiImpl.cpp

namespace {
    using editor_text::tr;
    using editor_text::tr_format;

    constexpr float MODEL_SCALE       = 1.0f / 16.0f; // 16 model pixels = one block = one grid cell
    constexpr float TOP_BAR_HEIGHT    = 40.0f;
    constexpr float RIGHT_PANEL_WIDTH = 360.0f;
    constexpr float TIMELINE_HEIGHT   = 150.0f;
    constexpr float ROW               = 26.0f;     // one control row
    constexpr float GAP               = 6.0f;
    constexpr float PAD               = 10.0f;
    constexpr float TIME_SNAP         = 0.05f; // keyframes land on 1/20 s - one game tick

    constexpr Color VIEWPORT_BACKGROUND = {48, 48, 52, 255};
    constexpr Color GRID_LINE           = {68, 68, 74, 255};
    constexpr Color GRID_SUBLINE        = {56, 56, 61, 255};
    constexpr Color AXIS_X              = {200, 70, 70, 255};
    constexpr Color AXIS_Z              = {110, 170, 60, 255};
    constexpr Color SELECTION           = {255, 160, 20, 255};
    constexpr Color PIVOT               = {90, 170, 255, 255};
    constexpr Color KEY_SELECTED        = {255, 200, 60, 255};
    constexpr Color KEY_OTHER           = {120, 120, 130, 255};
    constexpr Color PLAYHEAD            = {80, 150, 255, 255};

    std::string models_directory() { return std::string(ASSETS_PATH) + "models/"; }

    Color gui_color(int control, int property) {
        return GetColor(static_cast<unsigned int>(GuiGetStyle(control, property)));
    }

    std::string format_seconds(float seconds) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f", seconds);
        return buffer;
    }

    // A part's own transform (rotation about its pivot), the same one
    // EntityModelRenderer applies - raymath's MatrixMultiply(a, b) applies a
    // first.
    Matrix part_local_matrix(const ModelPart& part, const PartPose& pose) {
        Vector3 pivot = Vector3Scale(part.pivot, MODEL_SCALE);
        Vector3 offset = Vector3Scale(pose.offset, MODEL_SCALE);
        Matrix m = MatrixTranslate(-pivot.x, -pivot.y, -pivot.z);
        m = MatrixMultiply(m, MatrixRotate({1, 0, 0}, pose.rotation.x * DEG2RAD));
        m = MatrixMultiply(m, MatrixRotate({0, 1, 0}, pose.rotation.y * DEG2RAD));
        m = MatrixMultiply(m, MatrixRotate({0, 0, 1}, pose.rotation.z * DEG2RAD));
        m = MatrixMultiply(m, MatrixTranslate(pivot.x, pivot.y, pivot.z));
        return MatrixMultiply(m, MatrixTranslate(offset.x, offset.y, offset.z));
    }

    Matrix part_world_matrix(const EntityModel& model, const ModelPose& pose, int part) {
        Matrix m = MatrixIdentity();
        for (int i = part, guard = 0; i >= 0 && guard < 64; i = model.find_part(model.parts[i].parent), ++guard) {
            m = MatrixMultiply(m, part_local_matrix(model.parts[i], pose[i]));
        }
        return m;
    }
}

ModelEditor::ModelEditor()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1440, 900, "MineToo Model Editor");
    SetWindowMinSize(1100, 700);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    editor_text::load();
    SetWindowTitle(tr("editor.title").c_str());
    editor_load_dark_style();
    GuiSetFont(editor_text::font());
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);

    std::error_code error;
    const std::string skins_root = std::string(ASSETS_PATH) + "sprites/entities";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(skins_root, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            skin_paths.push_back(std::filesystem::relative(entry.path(), ASSETS_PATH).generic_string());
        }
    }
    std::sort(skin_paths.begin(), skin_paths.end());

    // Start on the player model if it was saved before, else the template.
    open_model();
    if (model.parts.empty()) new_model();
    status.clear();
}

ModelEditor::~ModelEditor()
{
    if (skin.id != 0) UnloadTexture(skin);
    if (viewport_texture.id != 0) UnloadRenderTexture(viewport_texture);
    editor_text::unload();
    CloseWindow();
}

void ModelEditor::run()
{
    while (!WindowShouldClose()) {
        widget_counter = 0;
        const Rectangle viewport = viewport_rect();

        if (!typing()) {
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                        IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
            if (ctrl && IsKeyPressed(KEY_S)) save_model();
            if (IsKeyPressed(KEY_SPACE) && current_animation()) playing = !playing;
        }
        if (playing) {
            if (EntityAnimation* animation = current_animation()) {
                time += GetFrameTime();
                if (time > animation->length) {
                    if (animation->loop) time = std::fmod(time, animation->length);
                    else { time = animation->length; playing = false; }
                }
            } else {
                playing = false;
            }
        }

        update_camera(viewport);
        draw_viewport(viewport);

        BeginDrawing();
        ClearBackground(gui_color(DEFAULT, BACKGROUND_COLOR));
        DrawTexturePro(viewport_texture.texture,
                       {0, 0, static_cast<float>(viewport_texture.texture.width), -static_cast<float>(viewport_texture.texture.height)},
                       viewport, {0, 0}, 0.0f, WHITE);
        label({viewport.x + PAD, viewport.y + viewport.height - ROW - 4, viewport.width - PAD * 2, ROW},
              tr("editor.viewport_hint"));
        draw_timeline(timeline_rect());
        draw_right_panel(right_panel_rect());
        if (graph_open) draw_graph_window(graph_window_rect(viewport));
        draw_top_bar(top_bar_rect());
        EndDrawing();
    }
}

// --------------------------------------------------------------- Layout --

Rectangle ModelEditor::top_bar_rect() const
{
    return {0, 0, static_cast<float>(GetScreenWidth()), TOP_BAR_HEIGHT};
}

Rectangle ModelEditor::right_panel_rect() const
{
    return {GetScreenWidth() - RIGHT_PANEL_WIDTH, TOP_BAR_HEIGHT, RIGHT_PANEL_WIDTH,
            GetScreenHeight() - TOP_BAR_HEIGHT};
}

Rectangle ModelEditor::timeline_rect() const
{
    return {0, GetScreenHeight() - TIMELINE_HEIGHT, GetScreenWidth() - RIGHT_PANEL_WIDTH, TIMELINE_HEIGHT};
}

Rectangle ModelEditor::viewport_rect() const
{
    return {0, TOP_BAR_HEIGHT, GetScreenWidth() - RIGHT_PANEL_WIDTH,
            GetScreenHeight() - TOP_BAR_HEIGHT - TIMELINE_HEIGHT};
}

// ------------------------------------------------------------- Viewport --

Camera3D ModelEditor::camera() const
{
    Camera3D cam{};
    cam.target = orbit_target;
    cam.position = Vector3Add(orbit_target, {orbit_distance * std::cos(orbit_pitch) * std::sin(orbit_yaw),
                                             orbit_distance * std::sin(orbit_pitch),
                                             orbit_distance * std::cos(orbit_pitch) * std::cos(orbit_yaw)});
    cam.up = {0, 1, 0};
    cam.fovy = orthographic ? orbit_distance : 45.0f;
    cam.projection = orthographic ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    return cam;
}

void ModelEditor::update_camera(Rectangle viewport)
{
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, viewport) &&
                      !(graph_open && CheckCollisionPointRec(mouse, graph_window_rect(viewport)));
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool navigating = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (alt && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    static bool drag_started_here = false;
    if ((IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        drag_started_here = over;
        press_position = mouse;
    }

    if (navigating && drag_started_here) {
        Vector2 delta = GetMouseDelta();
        if (shift) {
            Camera3D cam = camera();
            Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
            Vector3 up = Vector3CrossProduct(right, forward);
            float speed = orbit_distance * 0.0015f;
            orbit_target = Vector3Add(orbit_target, Vector3Scale(right, -delta.x * speed));
            orbit_target = Vector3Add(orbit_target, Vector3Scale(up, delta.y * speed));
        } else {
            orbit_yaw -= delta.x * 0.008f;
            orbit_pitch = std::clamp(orbit_pitch + delta.y * 0.008f, -1.55f, 1.55f);
        }
    }
    if (over && !typing()) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) orbit_distance = std::clamp(orbit_distance * std::pow(0.88f, wheel), 0.5f, 60.0f);

        // Blender's numpad views.
        if (IsKeyPressed(KEY_KP_1)) { orbit_yaw = 0.0f; orbit_pitch = 0.0f; }
        if (IsKeyPressed(KEY_KP_3)) { orbit_yaw = PI / 2.0f; orbit_pitch = 0.0f; }
        if (IsKeyPressed(KEY_KP_7)) { orbit_pitch = 1.55f; }
        if (IsKeyPressed(KEY_KP_5)) orthographic = !orthographic;
        if (IsKeyPressed(KEY_F) && selected_part >= 0) {
            orbit_target = Vector3Scale(model.parts[selected_part].pivot, MODEL_SCALE);
        }
    }

    // A plain left click (no Alt, barely moved) selects the part under it.
    if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !alt && drag_started_here &&
        Vector2Distance(press_position, mouse) < 4.0f) {
        pick_part(viewport);
    }
}

void ModelEditor::draw_viewport(Rectangle viewport)
{
    const int width = std::max(1, static_cast<int>(viewport.width));
    const int height = std::max(1, static_cast<int>(viewport.height));
    if (viewport_texture.id == 0 || viewport_texture.texture.width != width || viewport_texture.texture.height != height) {
        if (viewport_texture.id != 0) UnloadRenderTexture(viewport_texture);
        viewport_texture = LoadRenderTexture(width, height);
    }

    BeginTextureMode(viewport_texture);
    ClearBackground(VIEWPORT_BACKGROUND);
    BeginMode3D(camera());
    draw_grid();
    rlPushMatrix();
    rlRotatef(displayed_body_yaw(), 0.0f, 1.0f, 0.0f); // the look preview may turn the whole body
    draw_entity_model(model, skin, displayed_pose(), MODEL_SCALE);
    draw_selection();
    rlPopMatrix();
    EndMode3D();
    EndTextureMode();
}

void ModelEditor::draw_grid() const
{
    constexpr int BLOCKS = 8;
    // Fine pixel grid right around the model, for measuring.
    for (int i = -32; i <= 32; ++i) {
        float p = i * MODEL_SCALE;
        DrawLine3D({p, 0, -2}, {p, 0, 2}, GRID_SUBLINE);
        DrawLine3D({-2, 0, p}, {2, 0, p}, GRID_SUBLINE);
    }
    // One cell per block.
    for (int i = -BLOCKS; i <= BLOCKS; ++i) {
        if (i == 0) continue;
        DrawLine3D({static_cast<float>(i), 0, -BLOCKS}, {static_cast<float>(i), 0, BLOCKS}, GRID_LINE);
        DrawLine3D({-BLOCKS, 0, static_cast<float>(i)}, {BLOCKS, 0, static_cast<float>(i)}, GRID_LINE);
    }
    DrawLine3D({-BLOCKS, 0, 0}, {BLOCKS, 0, 0}, AXIS_X);
    DrawLine3D({0, 0, -BLOCKS}, {0, 0, BLOCKS}, AXIS_Z);
}

void ModelEditor::draw_selection() const
{
    if (selected_part < 0 || selected_part >= static_cast<int>(model.parts.size())) return;
    const ModelPart& part = model.parts[selected_part];
    ModelPose pose = const_cast<ModelEditor*>(this)->displayed_pose();

    rlDrawRenderBatchActive();
    rlDisableDepthTest(); // like Blender's selection outline: always on top
    push_part_transform(model, pose, selected_part, MODEL_SCALE);
    for (size_t c = 0; c < part.cubes.size(); ++c) {
        const ModelCube& cube = part.cubes[c];
        Vector3 size = Vector3Scale(cube.size, MODEL_SCALE);
        Vector3 center = Vector3Add(Vector3Scale(cube.origin, MODEL_SCALE), Vector3Scale(size, 0.5f));
        DrawCubeWiresV(center, size, static_cast<int>(c) == selected_cube ? SELECTION : Fade(SELECTION, 0.45f));
    }
    Vector3 pivot = Vector3Scale(part.pivot, MODEL_SCALE);
    const float arm = 0.12f;
    DrawLine3D(Vector3Subtract(pivot, {arm, 0, 0}), Vector3Add(pivot, {arm, 0, 0}), AXIS_X);
    DrawLine3D(Vector3Subtract(pivot, {0, arm, 0}), Vector3Add(pivot, {0, arm, 0}), PIVOT);
    DrawLine3D(Vector3Subtract(pivot, {0, 0, arm}), Vector3Add(pivot, {0, 0, arm}), AXIS_Z);
    DrawSphere(pivot, 0.025f, PIVOT);
    rlPopMatrix();
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

void ModelEditor::pick_part(Rectangle viewport)
{
    Vector2 local = Vector2Subtract(GetMousePosition(), {viewport.x, viewport.y});
    Ray ray = GetScreenToWorldRayEx(local, camera(), static_cast<int>(viewport.width), static_cast<int>(viewport.height));
    ModelPose pose = displayed_pose();
    const Matrix body_turn = MatrixRotate({0, 1, 0}, displayed_body_yaw() * DEG2RAD);

    int best_part = -1, best_cube = 0;
    float best_distance = 1e9f;
    for (size_t p = 0; p < model.parts.size(); ++p) {
        // The ray in this part's own (rest) space, so its cubes stay plain boxes.
        Matrix inverse = MatrixInvert(MatrixMultiply(part_world_matrix(model, pose, static_cast<int>(p)), body_turn));
        Vector3 origin = Vector3Transform(ray.position, inverse);
        Vector3 direction = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(ray.position, ray.direction), inverse), origin));
        for (size_t c = 0; c < model.parts[p].cubes.size(); ++c) {
            const ModelCube& cube = model.parts[p].cubes[c];
            BoundingBox box{Vector3Scale(cube.origin, MODEL_SCALE),
                            Vector3Scale(Vector3Add(cube.origin, cube.size), MODEL_SCALE)};
            RayCollision hit = GetRayCollisionBox({origin, direction}, box);
            if (hit.hit && hit.distance < best_distance) {
                best_distance = hit.distance;
                best_part = static_cast<int>(p);
                best_cube = static_cast<int>(c);
            }
        }
    }
    if (best_part >= 0) {
        select_part(best_part);
        selected_cube = best_cube;
    }
}

// --------------------------------------------------------------- Widgets --

void ModelEditor::label(Rectangle bounds, const std::string& text) const
{
    GuiLabel(bounds, text.c_str());
}

bool ModelEditor::text_field(Rectangle bounds, char* buffer, int size)
{
    const int id = widget_counter++;
    const bool editing = editing_widget == id;
    if (GuiTextBox(bounds, buffer, size, editing)) {
        if (editing) {
            editing_widget = -1;
            return true;
        }
        editing_widget = id;
    }
    return false;
}

bool ModelEditor::int_field(Rectangle bounds, int& value, int min_value, int max_value)
{
    const int id = widget_counter++;
    const bool editing = editing_widget == id;
    const int before = value;
    if (GuiSpinner(bounds, nullptr, &value, min_value, max_value, editing)) {
        editing_widget = editing ? -1 : id;
    }
    return value != before;
}

bool ModelEditor::float_as_int_field(Rectangle bounds, float& value, int min_value, int max_value)
{
    int whole = static_cast<int>(std::lround(value));
    if (!int_field(bounds, whole, min_value, max_value)) return false;
    value = static_cast<float>(whole);
    return true;
}

// ---------------------------------------------------------------- Top bar --

void ModelEditor::draw_top_bar(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    float x = bounds.x + PAD;
    const float y = bounds.y + (bounds.height - ROW) * 0.5f;
    label({x, y, 70, ROW}, tr("editor.model"));
    x += 70;
    text_field({x, y, 180, ROW}, model_name, sizeof(model_name));
    x += 180 + GAP;
    if (GuiButton({x, y, 90, ROW}, tr("editor.new").c_str())) new_model();
    x += 90 + GAP;
    if (GuiButton({x, y, 90, ROW}, tr("editor.open").c_str())) open_model();
    x += 90 + GAP;
    if (GuiButton({x, y, 110, ROW}, tr("editor.save").c_str())) save_model();
    x += 110 + GAP * 2;
    GuiToggle({x, y, 170, ROW}, tr("editor.graph").c_str(), &graph_open);
    x += 170 + GAP * 2;

    std::string line = dirty ? tr("editor.unsaved") : std::string();
    if (GetTime() - status_time < 4.0) line = status + (dirty ? "   " + line : "");
    label({x, y, bounds.width - x - PAD, ROW}, line);
}

// ------------------------------------------------------------ Right panel --

void ModelEditor::draw_right_panel(Rectangle bounds)
{
    const float content_width = bounds.width - 14.0f;
    static float content_height = 800.0f;
    Rectangle view{};
    GuiScrollPanel(bounds, nullptr, {0, 0, content_width, content_height}, &panel_scroll, &view);

    // Controls scrolled out of the panel stay drawn under the scissor - keep
    // them from catching clicks meant for whatever is actually there.
    const bool mouse_inside = CheckCollisionPointRec(GetMousePosition(), view);
    if (!mouse_inside) GuiLock();
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));

    const float x = view.x + PAD;
    const float width = content_width - PAD * 2;
    float y = view.y + panel_scroll.y + PAD;

    // Skin
    GuiLine({x, y, width, ROW}, tr("editor.skin").c_str());
    y += ROW;
    {
        std::string items = tr("editor.no_skin");
        int active = 0;
        for (size_t i = 0; i < skin_paths.size(); ++i) {
            items += ";" + std::filesystem::path(skin_paths[i]).filename().string();
            if (skin_paths[i] == model.skin) active = static_cast<int>(i) + 1;
        }
        int chosen = active;
        GuiListView({x, y, width, 84}, items.c_str(), &skin_list_scroll, &chosen);
        if (chosen >= 0 && chosen != active) {
            load_skin(chosen == 0 ? std::string() : skin_paths[chosen - 1]);
            mark_dirty();
        }
        y += 84 + GAP;
    }

    // Parts
    GuiLine({x, y, width, ROW}, tr("editor.parts").c_str());
    y += ROW;
    {
        std::vector<int> depths;
        std::vector<int> order = part_display_order(&depths);
        std::string items;
        int active = -1;
        for (size_t i = 0; i < order.size(); ++i) {
            if (i) items += ";";
            items += std::string(static_cast<size_t>(depths[i]) * 3, ' ') + model.parts[order[i]].name;
            if (order[i] == selected_part) active = static_cast<int>(i);
        }
        int chosen = active;
        GuiListView({x, y, width, 150}, items.c_str(), &part_list_scroll, &chosen);
        if (chosen >= 0 && chosen != active) select_part(order[chosen]);
        y += 150 + GAP;

        const float half = (width - GAP) * 0.5f;
        if (GuiButton({x, y, half, ROW}, tr("editor.add_part").c_str())) add_part();
        GuiSetState(selected_part >= 0 ? STATE_NORMAL : STATE_DISABLED);
        if (GuiButton({x + half + GAP, y, half, ROW}, tr("editor.delete_part").c_str())) delete_selected_part();
        GuiSetState(STATE_NORMAL);
        y += ROW + GAP * 2;
    }

    if (selected_part >= 0) y = draw_part_properties(x, y, width);

    EndScissorMode();
    GuiUnlock();
    content_height = y - (view.y + panel_scroll.y) + PAD;
}

float ModelEditor::draw_part_properties(float x, float y, float width)
{
    ModelPart& part = model.parts[selected_part];
    const float label_w = 110.0f;
    const float field_w = (width - label_w - GAP * 2) / 3.0f;

    // Three spinners on one row after a label.
    auto vec3_row = [&](const std::string& caption, Vector3& value, int min_value, int max_value) {
        label({x, y, label_w, ROW}, caption);
        bool changed = false;
        changed |= float_as_int_field({x + label_w, y, field_w, ROW}, value.x, min_value, max_value);
        changed |= float_as_int_field({x + label_w + field_w + GAP, y, field_w, ROW}, value.y, min_value, max_value);
        changed |= float_as_int_field({x + label_w + (field_w + GAP) * 2, y, field_w, ROW}, value.z, min_value, max_value);
        y += ROW + GAP;
        return changed;
    };

    GuiLine({x, y, width, ROW}, tr("editor.part").c_str());
    y += ROW;

    label({x, y, label_w, ROW}, tr("editor.name"));
    if (text_field({x + label_w, y, width - label_w, ROW}, part_name, sizeof(part_name))) rename_selected_part(part_name);
    y += ROW + GAP;

    // Parent: every part except itself and its own descendants.
    {
        std::vector<int> candidates{-1};
        std::string items = tr("editor.root");
        int active = 0;
        for (size_t i = 0; i < model.parts.size(); ++i) {
            if (static_cast<int>(i) == selected_part || is_descendant(static_cast<int>(i), selected_part)) continue;
            candidates.push_back(static_cast<int>(i));
            items += ";" + model.parts[i].name;
            if (model.parts[i].name == part.parent) active = static_cast<int>(candidates.size()) - 1;
        }
        label({x, y, label_w, ROW}, tr("editor.parent"));
        int chosen = active;
        GuiComboBox({x + label_w, y, width - label_w, ROW}, items.c_str(), &chosen);
        if (chosen != active) {
            part.parent = candidates[chosen] < 0 ? std::string() : model.parts[candidates[chosen]].name;
            mark_dirty();
        }
        y += ROW + GAP;
    }

    if (vec3_row(tr("editor.pivot"), part.pivot, -256, 256)) mark_dirty();

    // Look node - the part that turns toward where the entity looks.
    y += GAP;
    GuiLine({x, y, width, ROW}, tr("editor.look").c_str());
    y += ROW;
    {
        bool look = part.look;
        GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.look_enabled").c_str(), &look);
        if (look != part.look) {
            for (ModelPart& other : model.parts) other.look = false; // one look part per model
            part.look = look;
            mark_dirty();
        }
        y += ROW + GAP;
        if (part.look) {
            label({x, y, width - 90, ROW}, tr("editor.body_turn_angle"));
            if (float_as_int_field({x + width - 90, y, 90, ROW}, part.body_turn_angle, 0, 180)) mark_dirty();
            y += ROW + GAP;

            // Try it out: where the entity looks (the game drives this from
            // the player's own view).
            char value_text[16];
            std::snprintf(value_text, sizeof(value_text), "%.0f", look_yaw);
            GuiSliderBar({x + 90, y, width - 140, ROW}, tr("editor.look_yaw").c_str(), value_text, &look_yaw, -180.0f, 180.0f);
            y += ROW + GAP;
            std::snprintf(value_text, sizeof(value_text), "%.0f", look_pitch);
            GuiSliderBar({x + 90, y, width - 140, ROW}, tr("editor.look_pitch").c_str(), value_text, &look_pitch, -90.0f, 90.0f);
            y += ROW + GAP;
            if (GuiButton({x, y, width, ROW}, tr("editor.look_reset").c_str())) look_yaw = look_pitch = 0.0f;
            y += ROW + GAP;
        }
    }

    // Cubes
    y += GAP;
    {
        const float third = (width - GAP * 2) / 3.0f;
        if (part.cubes.empty()) selected_cube = 0;
        selected_cube = std::clamp(selected_cube, 0, std::max(0, static_cast<int>(part.cubes.size()) - 1));
        if (GuiButton({x, y, third, ROW}, "<") && selected_cube > 0) --selected_cube;
        label({x + third + GAP, y, third, ROW},
              tr_format("editor.cube", {std::to_string(part.cubes.empty() ? 0 : selected_cube + 1), std::to_string(part.cubes.size())}));
        if (GuiButton({x + (third + GAP) * 2, y, third, ROW}, ">") && selected_cube + 1 < static_cast<int>(part.cubes.size())) ++selected_cube;
        y += ROW + GAP;
        const float half = (width - GAP) * 0.5f;
        if (GuiButton({x, y, half, ROW}, tr("editor.add_cube").c_str())) {
            ModelCube cube;
            cube.origin = Vector3Subtract(part.pivot, {2, 2, 2});
            cube.size = {4, 4, 4};
            part.cubes.push_back(cube);
            selected_cube = static_cast<int>(part.cubes.size()) - 1;
            mark_dirty();
        }
        GuiSetState(part.cubes.empty() ? STATE_DISABLED : STATE_NORMAL);
        if (GuiButton({x + half + GAP, y, half, ROW}, tr("editor.delete_cube").c_str()) && !part.cubes.empty()) {
            part.cubes.erase(part.cubes.begin() + selected_cube);
            selected_cube = std::max(0, selected_cube - 1);
            mark_dirty();
        }
        GuiSetState(STATE_NORMAL);
        y += ROW + GAP;

        if (!part.cubes.empty()) {
            ModelCube& cube = part.cubes[selected_cube];
            if (vec3_row(tr("editor.origin"), cube.origin, -256, 256)) mark_dirty();
            if (vec3_row(tr("editor.size"), cube.size, 1, 128)) mark_dirty();
            label({x, y, label_w, ROW}, tr("editor.uv"));
            if (float_as_int_field({x + label_w, y, field_w, ROW}, cube.uv.x, 0, model.skin_width)) mark_dirty();
            if (float_as_int_field({x + label_w + field_w + GAP, y, field_w, ROW}, cube.uv.y, 0, model.skin_height)) mark_dirty();
            y += ROW + GAP;
        }
    }

    // Rotation limits
    y += GAP;
    GuiLine({x, y, width, ROW}, tr("editor.limits").c_str());
    y += ROW;
    if (vec3_row(tr("editor.min"), part.rotation_min, -180, 180)) {
        part.rotation_max = Vector3Max(part.rotation_max, part.rotation_min);
        mark_dirty();
    }
    if (vec3_row(tr("editor.max"), part.rotation_max, -180, 180)) {
        part.rotation_min = Vector3Min(part.rotation_min, part.rotation_max);
        mark_dirty();
    }

    // Current rotation - with an animation selected, moving a slider sets
    // this part's keyframe at the current time (auto-key).
    y += GAP;
    GuiLine({x, y, width, ROW}, tr("editor.rotation").c_str());
    y += ROW;
    {
        const PartPose pose = current_pose()[selected_part];
        const Vector3 rotation = pose.rotation;
        Vector3 edited = rotation;
        float* axes[3] = {&edited.x, &edited.y, &edited.z};
        const float mins[3] = {part.rotation_min.x, part.rotation_min.y, part.rotation_min.z};
        const float maxs[3] = {part.rotation_max.x, part.rotation_max.y, part.rotation_max.z};
        const char* names[3] = {"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) {
            char value_text[16];
            std::snprintf(value_text, sizeof(value_text), "%.0f", *axes[axis]);
            GuiSliderBar({x + 20, y, width - 70, ROW}, names[axis], value_text, axes[axis], mins[axis],
                         std::max(mins[axis] + 0.001f, maxs[axis]));
            y += ROW + GAP;
        }
        if (edited.x != rotation.x || edited.y != rotation.y || edited.z != rotation.z) {
            set_selected_pose({edited, pose.offset});
        }

        // Offset - moves the part (and everything attached to it), e.g. to
        // lower the body into a crouch.
        Vector3 offset = pose.offset;
        if (vec3_row(tr("editor.offset"), offset, -64, 64)) set_selected_pose({edited, offset});
        label({x, y, width, ROW}, tr(current_animation() ? "editor.rotation_keys_hint" : "editor.rotation_preview_hint"));
        y += ROW + GAP;
    }
    return y;
}

// --------------------------------------------------------------- Timeline --

void ModelEditor::draw_timeline(Rectangle bounds)
{
    GuiPanel(bounds, nullptr);
    float x = bounds.x + PAD;
    float y = bounds.y + PAD;

    // Row 1: clip picker and clip settings.
    {
        std::string items = tr("editor.no_animation");
        for (const EntityAnimation& animation : model.animations) items += ";" + animation.name;
        int active = animation_index + 1;
        label({x, y, 90, ROW}, tr("editor.animation"));
        x += 90;
        int chosen = active;
        GuiComboBox({x, y, 170, ROW}, items.c_str(), &chosen);
        if (chosen != active) {
            animation_index = chosen - 1;
            time = 0.0f;
            playing = false;
            if (EntityAnimation* animation = current_animation()) {
                std::snprintf(animation_name, sizeof(animation_name), "%s", animation->name.c_str());
            }
        }
        x += 170 + GAP;
        if (GuiButton({x, y, 90, ROW}, tr("editor.new_animation").c_str())) {
            EntityAnimation animation;
            animation.name = "animation_" + std::to_string(model.animations.size() + 1);
            model.animations.push_back(animation);
            animation_index = static_cast<int>(model.animations.size()) - 1;
            std::snprintf(animation_name, sizeof(animation_name), "%s", animation.name.c_str());
            time = 0.0f;
            mark_dirty();
        }
        x += 90 + GAP;

        EntityAnimation* animation = current_animation();
        GuiSetState(animation ? STATE_NORMAL : STATE_DISABLED);
        if (GuiButton({x, y, 90, ROW}, tr("editor.delete_animation").c_str()) && animation) {
            const std::string removed = animation->name;
            model.animations.erase(model.animations.begin() + animation_index);
            for (EntityAnimation& other : model.animations) {
                other.links.erase(std::remove(other.links.begin(), other.links.end(), removed), other.links.end());
            }
            animation_index = -1;
            animation = nullptr;
            playing = false;
            mark_dirty();
        }
        x += 90 + GAP * 2;
        if (animation) {
            if (text_field({x, y, 150, ROW}, animation_name, sizeof(animation_name))) {
                rename_animation(*animation, animation_name);
            }
            x += 150 + GAP;
            label({x, y, 80, ROW}, tr("editor.length"));
            x += 80;
            int tenths = static_cast<int>(std::lround(animation->length * 10.0f));
            if (int_field({x, y, 100, ROW}, tenths, 1, 600)) {
                animation->length = tenths / 10.0f;
                time = std::min(time, animation->length);
                mark_dirty();
            }
            x += 100 + GAP;
            bool loop = animation->loop;
            GuiCheckBox({x, y + 4, ROW - 8, ROW - 8}, tr("editor.loop").c_str(), &loop);
            if (loop != animation->loop) {
                animation->loop = loop;
                mark_dirty();
            }
        }
        GuiSetState(STATE_NORMAL);
    }

    // Row 2: transport and keyframe buttons.
    x = bounds.x + PAD;
    y += ROW + GAP;
    EntityAnimation* animation = current_animation();
    GuiSetState(animation ? STATE_NORMAL : STATE_DISABLED);
    if (GuiButton({x, y, 90, ROW}, tr(playing ? "editor.pause" : "editor.play").c_str()) && animation) playing = !playing;
    x += 90 + GAP;
    const bool can_key = animation && selected_part >= 0;
    GuiSetState(can_key ? STATE_NORMAL : STATE_DISABLED);
    if (GuiButton({x, y, 90, ROW}, tr("editor.add_key").c_str()) && can_key) {
        const PartPose pose = current_pose()[selected_part];
        set_keyframe(*find_track(*animation, model.parts[selected_part].name, true), snapped_time(), pose.rotation,
                     pose.offset);
        mark_dirty();
    }
    x += 90 + GAP;
    if (GuiButton({x, y, 130, ROW}, tr("editor.delete_key").c_str()) && can_key) {
        if (ModelTrack* track = find_track(*animation, model.parts[selected_part].name, false)) {
            int key = keyframe_at(*track, time, TIME_SNAP * 0.5f);
            if (key >= 0) {
                track->keys.erase(track->keys.begin() + key);
                mark_dirty();
            }
        }
    }
    GuiSetState(STATE_NORMAL);
    x += 130 + GAP * 2;
    label({x, y, 150, ROW}, tr_format("editor.time", {format_seconds(time)}));
    x += 150 + GAP;

    // When the game plays this animation by itself.
    if (animation) {
        label({x, y, 80, ROW}, tr("editor.trigger"));
        x += 80;
        // Same order as AnimationTrigger.
        std::string items = tr("editor.trigger_manual") + ";" + tr("editor.trigger_always") + ";" +
                            tr("editor.trigger_moving") + ";" + tr("editor.trigger_sneaking");
        int active = static_cast<int>(animation->trigger);
        int chosen = active;
        GuiComboBox({x, y, 170, ROW}, items.c_str(), &chosen);
        if (chosen != active) {
            animation->trigger = static_cast<AnimationTrigger>(chosen);
            mark_dirty();
        }
        x += 170 + GAP;
        if (animation->trigger == AnimationTrigger::Moving) {
            label({x, y, 150, ROW}, tr("editor.blocks_per_loop"));
            x += 150;
            int tenths = static_cast<int>(std::lround(animation->blocks_per_loop * 10.0f));
            if (int_field({x, y, 100, ROW}, tenths, 1, 200)) {
                animation->blocks_per_loop = tenths / 10.0f;
                mark_dirty();
            }
        }
    }

    // Row 3: the timeline itself - click or drag to scrub, keyframes as
    // diamonds (the selected part's bright, the rest dim).
    y += ROW + GAP;
    Rectangle bar = {bounds.x + PAD, y, bounds.width - PAD * 2, bounds.y + bounds.height - PAD - y};
    DrawRectangleRec(bar, Color{32, 32, 36, 255});
    DrawRectangleLinesEx(bar, 1.0f, gui_color(DEFAULT, LINE_COLOR));
    if (!animation) {
        label({bar.x + PAD, bar.y, bar.width, bar.height}, tr("editor.timeline_empty"));
        return;
    }

    const float length = std::max(0.05f, animation->length);
    auto time_to_x = [&](float t) { return bar.x + PAD + (bar.width - PAD * 2) * (t / length); };
    for (int step = 0; step <= static_cast<int>(length * 10.0f + 0.5f); ++step) {
        float t = step / 10.0f;
        bool whole = step % 10 == 0;
        float tx = time_to_x(t);
        DrawLineV({tx, bar.y}, {tx, bar.y + (whole ? 14.0f : 7.0f)}, gui_color(DEFAULT, LINE_COLOR));
        if (whole) DrawTextEx(editor_text::font(), std::to_string(step / 10).c_str(), {tx + 3, bar.y + 2}, 14, 1,
                              gui_color(DEFAULT, TEXT_COLOR_NORMAL));
    }
    const float selected_row = bar.y + bar.height * 0.45f;
    const float other_row = bar.y + bar.height * 0.78f;
    for (const ModelTrack& track : animation->tracks) {
        bool is_selected = selected_part >= 0 && track.part == model.parts[selected_part].name;
        for (const ModelKeyframe& key : track.keys) {
            Vector2 c = {time_to_x(key.time), is_selected ? selected_row : other_row};
            float r = is_selected ? 7.0f : 4.0f;
            DrawPoly(c, 4, r, 45.0f, is_selected ? KEY_SELECTED : KEY_OTHER);
        }
    }
    const float head_x = time_to_x(time);
    DrawLineEx({head_x, bar.y}, {head_x, bar.y + bar.height}, 2.0f, PLAYHEAD);

    const Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, bar)) scrubbing = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) scrubbing = false;
    if (scrubbing) {
        float t = (mouse.x - bar.x - PAD) / (bar.width - PAD * 2) * length;
        time = std::clamp(std::round(t / TIME_SNAP) * TIME_SNAP, 0.0f, length);
        playing = false;
    }
}

// ------------------------------------------------------ Model operations --

void ModelEditor::new_model()
{
    model = make_humanoid_model();
    if (model_name[0] != '\0') model.name = model_name;
    load_skin(model.skin);
    animation_index = -1;
    time = 0.0f;
    playing = false;
    select_part(model.parts.empty() ? -1 : 0);
    dirty = true;
    set_status(tr("editor.created"));
}

void ModelEditor::open_model()
{
    const std::string path = models_directory() + model_name + ".json";
    std::optional<EntityModel> loaded = load_entity_model(path);
    if (!loaded) {
        set_status(tr_format("editor.open_failed", {path}));
        return;
    }
    model = std::move(*loaded);
    if (model.name.empty()) model.name = model_name;
    load_skin(model.skin);
    animation_index = model.animations.empty() ? -1 : 0;
    if (EntityAnimation* animation = current_animation()) {
        std::snprintf(animation_name, sizeof(animation_name), "%s", animation->name.c_str());
    }
    time = 0.0f;
    playing = false;
    select_part(model.parts.empty() ? -1 : 0);
    dirty = false;
    set_status(tr_format("editor.opened", {path}));
}

void ModelEditor::save_model()
{
    if (model_name[0] == '\0') return;
    model.name = model_name;
    std::error_code error;
    std::filesystem::create_directories(models_directory(), error);
    const std::string path = models_directory() + model_name + ".json";
    if (save_entity_model(model, path)) {
        dirty = false;
        set_status(tr_format("editor.saved", {path}));
    } else {
        set_status(tr_format("editor.save_failed", {path}));
    }
}

void ModelEditor::load_skin(const std::string& path)
{
    if (skin.id != 0) UnloadTexture(skin);
    skin = {};
    model.skin = path;
    if (path.empty()) return;
    skin = LoadTexture((std::string(ASSETS_PATH) + path).c_str());
    if (skin.id != 0) {
        SetTextureFilter(skin, TEXTURE_FILTER_POINT);
        model.skin_width = skin.width;
        model.skin_height = skin.height;
    }
}

void ModelEditor::select_part(int index)
{
    selected_part = index;
    selected_cube = 0;
    if (editing_widget >= 0) editing_widget = -1;
    std::snprintf(part_name, sizeof(part_name), "%s", index >= 0 ? model.parts[index].name.c_str() : "");
    preview_pose.resize(model.parts.size());
}

void ModelEditor::add_part()
{
    ModelPart part;
    int suffix = static_cast<int>(model.parts.size()) + 1;
    do {
        part.name = "part_" + std::to_string(suffix++);
    } while (model.find_part(part.name) >= 0);
    if (selected_part >= 0) {
        part.parent = model.parts[selected_part].name;
        part.pivot = model.parts[selected_part].pivot;
    }
    ModelCube cube;
    cube.origin = Vector3Subtract(part.pivot, {2, 0, 2});
    cube.size = {4, 4, 4};
    part.cubes.push_back(cube);
    model.parts.push_back(part);
    preview_pose.push_back({});
    select_part(static_cast<int>(model.parts.size()) - 1);
    mark_dirty();
}

void ModelEditor::delete_selected_part()
{
    if (selected_part < 0) return;
    const std::string name = model.parts[selected_part].name;
    const std::string parent = model.parts[selected_part].parent;
    // Children move up to the deleted part's own parent instead of vanishing.
    for (ModelPart& part : model.parts) {
        if (part.parent == name) part.parent = parent;
    }
    for (EntityAnimation& animation : model.animations) {
        animation.tracks.erase(std::remove_if(animation.tracks.begin(), animation.tracks.end(),
                                              [&name](const ModelTrack& track) { return track.part == name; }),
                               animation.tracks.end());
    }
    model.parts.erase(model.parts.begin() + selected_part);
    if (selected_part < static_cast<int>(preview_pose.size())) preview_pose.erase(preview_pose.begin() + selected_part);
    select_part(model.parts.empty() ? -1 : std::min(selected_part, static_cast<int>(model.parts.size()) - 1));
    mark_dirty();
}

void ModelEditor::rename_selected_part(const std::string& new_name)
{
    if (selected_part < 0) return;
    const std::string old_name = model.parts[selected_part].name;
    if (new_name.empty() || new_name == old_name || model.find_part(new_name) >= 0) {
        std::snprintf(part_name, sizeof(part_name), "%s", old_name.c_str()); // rejected - show the real name again
        return;
    }
    model.parts[selected_part].name = new_name;
    for (ModelPart& part : model.parts) {
        if (part.parent == old_name) part.parent = new_name;
    }
    for (EntityAnimation& animation : model.animations) {
        for (ModelTrack& track : animation.tracks) {
            if (track.part == old_name) track.part = new_name;
        }
    }
    mark_dirty();
}

bool ModelEditor::is_descendant(int part, int ancestor) const
{
    for (int i = model.find_part(model.parts[part].parent), guard = 0; i >= 0 && guard < 64;
         i = model.find_part(model.parts[i].parent), ++guard) {
        if (i == ancestor) return true;
    }
    return false;
}

std::vector<int> ModelEditor::part_display_order(std::vector<int>* depths) const
{
    std::vector<int> order;
    std::vector<bool> placed(model.parts.size(), false);
    auto visit = [&](auto&& self, int index, int depth) -> void {
        if (placed[index] || depth > 64) return;
        placed[index] = true;
        order.push_back(index);
        if (depths) depths->push_back(depth);
        for (size_t child = 0; child < model.parts.size(); ++child) {
            if (model.parts[child].parent == model.parts[index].name) self(self, static_cast<int>(child), depth + 1);
        }
    };
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const std::string& parent = model.parts[i].parent;
        if (parent.empty() || model.find_part(parent) < 0) visit(visit, static_cast<int>(i), 0);
    }
    for (size_t i = 0; i < model.parts.size(); ++i) visit(visit, static_cast<int>(i), 0); // anything left in a parent loop
    return order;
}

void ModelEditor::set_status(const std::string& text)
{
    status = text;
    status_time = GetTime();
}

// -------------------------------------------------------------- Animation --

EntityAnimation* ModelEditor::current_animation()
{
    if (animation_index < 0 || animation_index >= static_cast<int>(model.animations.size())) return nullptr;
    return &model.animations[animation_index];
}

ModelPose ModelEditor::current_pose()
{
    if (EntityAnimation* animation = current_animation()) return sample_pose(model, animation, time);
    preview_pose.resize(model.parts.size());
    ModelPose pose = preview_pose;
    clamp_pose(model, pose);
    return pose;
}

ModelPose ModelEditor::displayed_pose()
{
    ModelPose pose = current_pose();
    apply_head_look(model, pose, look_yaw - displayed_body_yaw(), look_pitch);
    return pose;
}

float ModelEditor::displayed_body_yaw() const
{
    // The body starts facing forward and only turns once the look passes
    // the look part's body_turn_angle - same rule the game uses.
    return body_yaw_following_look(model, 0.0f, look_yaw);
}

void ModelEditor::set_selected_pose(PartPose pose)
{
    if (selected_part < 0) return;
    pose.rotation = clamp_rotation(model.parts[selected_part], pose.rotation);
    if (EntityAnimation* animation = current_animation()) {
        playing = false;
        set_keyframe(*find_track(*animation, model.parts[selected_part].name, true), snapped_time(), pose.rotation,
                     pose.offset);
        mark_dirty();
    } else {
        preview_pose.resize(model.parts.size());
        preview_pose[selected_part] = pose;
    }
}

float ModelEditor::snapped_time() const
{
    return std::round(time / TIME_SNAP) * TIME_SNAP;
}

// ------------------------------------------------------ Animation graph --

void ModelEditor::rename_animation(EntityAnimation& animation, const std::string& new_name)
{
    const std::string old_name = animation.name;
    bool taken = false;
    for (const EntityAnimation& other : model.animations) taken |= &other != &animation && other.name == new_name;
    if (new_name.empty() || new_name == old_name || taken) {
        std::snprintf(animation_name, sizeof(animation_name), "%s", old_name.c_str()); // rejected
        return;
    }
    animation.name = new_name;
    for (EntityAnimation& other : model.animations) {
        std::replace(other.links.begin(), other.links.end(), old_name, new_name);
    }
    mark_dirty();
}

Rectangle ModelEditor::graph_window_rect(Rectangle viewport) const
{
    const float width = std::min(620.0f, viewport.width - 24.0f);
    const float height = std::min(340.0f, viewport.height - 60.0f);
    return {viewport.x + 12.0f, viewport.y + 12.0f, width, height};
}

void ModelEditor::draw_graph_window(Rectangle bounds)
{
    constexpr float NODE_W = 160.0f, NODE_H = 54.0f, PORT = 7.0f;
    constexpr Color NODE_FILL[] = {
        {70, 70, 78, 255},  // manual
        {52, 84, 120, 255}, // always
        {50, 105, 70, 255}, // moving
        {120, 80, 45, 255}, // sneaking
    };
    const Color text_color = gui_color(DEFAULT, TEXT_COLOR_NORMAL);

    if (GuiWindowBox(bounds, tr("editor.graph_title").c_str())) {
        graph_open = false;
        return;
    }
    const Rectangle area = {bounds.x + 2, bounds.y + 26, bounds.width - 4, bounds.height - 28};
    const Vector2 mouse = GetMousePosition();
    const bool mouse_in_area = CheckCollisionPointRec(mouse, area);

    // Blocks without a stored place get a tidy grid position.
    for (size_t i = 0; i < model.animations.size(); ++i) {
        Vector2& p = model.animations[i].graph_position;
        if (p.x == 0.0f && p.y == 0.0f) p = {20.0f + (i % 3) * (NODE_W + 40.0f), 16.0f + (i / 3) * (NODE_H + 30.0f)};
    }
    auto node_rect = [&](size_t i) {
        Vector2 p = model.animations[i].graph_position;
        return Rectangle{area.x + p.x, area.y + p.y, NODE_W, NODE_H};
    };
    auto input_port = [&](size_t i) { Rectangle r = node_rect(i); return Vector2{r.x, r.y + r.height * 0.5f}; };
    auto output_port = [&](size_t i) { Rectangle r = node_rect(i); return Vector2{r.x + r.width, r.y + r.height * 0.5f}; };
    auto node_under_mouse = [&]() -> int {
        for (int i = static_cast<int>(model.animations.size()) - 1; i >= 0; --i) {
            Rectangle r = node_rect(i);
            Rectangle grab = {r.x - PORT * 2, r.y, r.width + PORT * 2, r.height}; // the input port counts too
            if (CheckCollisionPointRec(mouse, grab)) return i;
        }
        return -1;
    };

    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width), static_cast<int>(area.height));
    DrawRectangleRec(area, Color{30, 30, 34, 255});
    if (model.animations.empty()) {
        label({area.x + PAD, area.y + PAD, area.width - PAD * 2, ROW}, tr("editor.graph_empty"));
    }

    // Links - a curve from a block's right side into the linked block's
    // left side; the dot on it removes the link.
    int remove_from = -1, remove_link = -1;
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const EntityAnimation& animation = model.animations[i];
        for (size_t l = 0; l < animation.links.size(); ++l) {
            int target = -1;
            for (size_t j = 0; j < model.animations.size(); ++j) {
                if (model.animations[j].name == animation.links[l]) target = static_cast<int>(j);
            }
            if (target < 0) continue;
            Vector2 from = output_port(i), to = input_port(target);
            DrawLineBezier(from, to, 2.5f, KEY_SELECTED);
            Vector2 middle = Vector2Lerp(from, to, 0.5f);
            bool hovered = CheckCollisionPointCircle(mouse, middle, 8.0f);
            DrawCircleV(middle, 8.0f, hovered ? Color{220, 70, 70, 255} : Color{90, 90, 96, 255});
            DrawTextEx(editor_text::font(), "x", {middle.x - 4, middle.y - 8}, 16, 1, WHITE);
            if (hovered && mouse_in_area && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                remove_from = static_cast<int>(i);
                remove_link = static_cast<int>(l);
            }
        }
    }

    // Blocks
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const EntityAnimation& animation = model.animations[i];
        Rectangle r = node_rect(i);
        const bool selected = static_cast<int>(i) == animation_index;
        DrawRectangleRounded(r, 0.18f, 6, NODE_FILL[std::min<int>(static_cast<int>(animation.trigger), 3)]);
        DrawRectangleRoundedLinesEx(r, 0.18f, 6, selected ? 2.5f : 1.0f, selected ? SELECTION : Color{20, 20, 24, 255});
        DrawTextEx(editor_text::font(), animation.name.c_str(), {r.x + 12, r.y + 8}, 18, 1, WHITE);
        const char* trigger_keys[] = {"editor.trigger_manual", "editor.trigger_always", "editor.trigger_moving", "editor.trigger_sneaking"};
        DrawTextEx(editor_text::font(), tr(trigger_keys[std::min<int>(static_cast<int>(animation.trigger), 3)]).c_str(),
                   {r.x + 12, r.y + 30}, 15, 1, Fade(text_color, 0.8f));
        DrawCircleV(input_port(i), PORT, Color{180, 180, 190, 255});
        const bool out_hovered = CheckCollisionPointCircle(mouse, output_port(i), PORT + 3);
        DrawCircleV(output_port(i), PORT, out_hovered ? KEY_SELECTED : Color{180, 180, 190, 255});
    }

    // Interaction: drag from a right-side dot onto another block to link
    // them; drag a block to move it; click a block to open that animation.
    if (mouse_in_area && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && remove_from < 0) {
        graph_link_from = -1;
        for (size_t i = 0; i < model.animations.size(); ++i) {
            if (CheckCollisionPointCircle(mouse, output_port(i), PORT + 3)) graph_link_from = static_cast<int>(i);
        }
        if (graph_link_from < 0) {
            int node = node_under_mouse();
            if (node >= 0) {
                graph_drag_node = node;
                Rectangle r = node_rect(node);
                graph_drag_grab = {mouse.x - r.x, mouse.y - r.y};
                if (animation_index != node) {
                    animation_index = node;
                    time = 0.0f;
                    playing = false;
                    std::snprintf(animation_name, sizeof(animation_name), "%s", model.animations[node].name.c_str());
                }
            }
        }
    }
    if (graph_link_from >= 0 && graph_link_from < static_cast<int>(model.animations.size())) {
        DrawLineBezier(output_port(graph_link_from), mouse, 2.0f, Fade(KEY_SELECTED, 0.7f));
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            int target = node_under_mouse();
            if (target >= 0 && target != graph_link_from) {
                std::vector<std::string>& links = model.animations[graph_link_from].links;
                const std::string& name = model.animations[target].name;
                if (std::find(links.begin(), links.end(), name) == links.end()) {
                    links.push_back(name);
                    mark_dirty();
                }
            }
            graph_link_from = -1;
        }
    }
    if (graph_drag_node >= 0 && graph_drag_node < static_cast<int>(model.animations.size())) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 target = {mouse.x - graph_drag_grab.x - area.x, mouse.y - graph_drag_grab.y - area.y};
            target.x = std::clamp(target.x, 1.0f, std::max(1.0f, area.width - NODE_W));
            target.y = std::clamp(target.y, 1.0f, std::max(1.0f, area.height - NODE_H));
            Vector2& position = model.animations[graph_drag_node].graph_position;
            if (position.x != target.x || position.y != target.y) {
                position = target;
                mark_dirty();
            }
        } else {
            graph_drag_node = -1;
        }
    }
    if (remove_from >= 0) {
        std::vector<std::string>& links = model.animations[remove_from].links;
        links.erase(links.begin() + remove_link);
        mark_dirty();
    }
    EndScissorMode();

    label({area.x + PAD, area.y + area.height - ROW - 2, area.width - PAD * 2, ROW}, tr("editor.graph_hint"));
}
