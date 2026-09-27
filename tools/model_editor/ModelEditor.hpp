#pragma once

#include "model/EntityModel.hpp"

#include "raylib.h"

#include <string>
#include <vector>

// The entity model editor - a developer-only tool, its own executable
// (MineTooModelEditor), not part of the game. Layout:
//   - top bar: model name, New/Open/Save, status line;
//   - center: a Blender-style 3D viewport over a grid of one-block cells
//     (16 model pixels each) - middle mouse orbits, Shift+middle pans, the
//     wheel zooms (Alt+left / Alt+Shift+left do the same without a middle
//     button), numpad 1/3/7 snap to front/side/top, numpad 5 toggles
//     orthographic, F frames the selected part, left click selects a part;
//   - right panel: the skin, the part tree and the selected part's pivot,
//     cubes, rotation limits and current rotation;
//   - bottom: the animation timeline - clips, play/pause, keyframes.
// Models load from and save to assets/models/<name>.json (see
// model/EntityModel.hpp for the format the game reads).
class ModelEditor {
public:
    ModelEditor();
    ~ModelEditor();

    void run();

private:
    // Layout
    Rectangle top_bar_rect() const;
    Rectangle right_panel_rect() const;
    Rectangle timeline_rect() const;
    Rectangle viewport_rect() const;

    // Viewport
    void update_camera(Rectangle viewport);
    Camera3D camera() const;
    void draw_viewport(Rectangle viewport);
    void draw_grid() const;
    void draw_selection() const;
    void pick_part(Rectangle viewport);

    // Panels
    void draw_top_bar(Rectangle bounds);
    void draw_right_panel(Rectangle bounds);
    float draw_part_properties(float x, float y, float width);
    void draw_timeline(Rectangle bounds);

    // Widgets - raygui needs to know which box is being typed into; these
    // track that by call order within the frame.
    bool text_field(Rectangle bounds, char* buffer, int size); // true when editing just finished
    bool int_field(Rectangle bounds, int& value, int min_value, int max_value);
    bool float_as_int_field(Rectangle bounds, float& value, int min_value, int max_value);
    void label(Rectangle bounds, const std::string& text) const;
    bool typing() const { return editing_widget >= 0; }

    // Model operations
    void new_model();
    void open_model();                        // the model named in model_name
    void open_model_named(const std::string& name);
    void save_model();
    // Every model saved in assets/models, by name - the "Open" list.
    void refresh_model_list();
    // The model opened last, remembered across editor runs in
    // model_editor.json next to the game's settings.json.
    void remember_last_model() const;
    std::string last_model() const;
    void load_skin(const std::string& path);
    void select_part(int index);
    void add_part();
    void delete_selected_part();
    void rename_selected_part(const std::string& new_name);
    bool is_descendant(int part, int ancestor) const;
    std::vector<int> part_display_order(std::vector<int>* depths) const;
    void set_status(const std::string& text);
    void mark_dirty() { dirty = true; uncommitted_change = true; }

    // Undo/redo (Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z or Ctrl/Cmd+Y): whole-model
    // snapshots. One drag or one edited field is one step - a change is
    // committed once the mouse is let go and no field is being typed in.
    struct HistoryState {
        EntityModel model;
        int selected_part = -1;
        int selected_cube = 0;
        int animation_index = -1;
    };
    HistoryState current_state() const;
    void restore_state(const HistoryState& state);
    void commit_history(bool force = false);
    void reset_history();
    void undo();
    void redo();

    // Animation
    EntityAnimation* current_animation();
    ModelPose current_pose();         // animation (or rotation preview) only - what the sliders edit
    ModelPose displayed_pose();       // current_pose() plus the look preview - what the viewport shows
    float displayed_body_yaw() const; // the body turned by the look preview, degrees
    // Sets the selected part's pose - a keyframe at the current time with
    // an animation selected (auto-key), else the unsaved preview.
    void set_selected_pose(PartPose pose);

    // Floating windows over the viewport (graph, UV): moved by the title
    // bar, resized by the bottom-right corner, kept inside the viewport; the
    // one clicked last sits on top and gets the mouse where they overlap.
    struct FloatingWindow {
        Rectangle rect = {0, 0, 0, 0};   // relative to the viewport's top-left; width 0 = not placed yet
        Rectangle screen = {0, 0, 0, 0}; // where it is this frame
        enum class Drag : uint8_t { None, Move, Resize } drag = Drag::None;
        Vector2 grab = {0, 0};
    };
    enum WindowId { GRAPH_WINDOW = 0, UV_WINDOW = 1 };
    void update_floating_windows(Rectangle viewport);
    void update_floating_window(FloatingWindow& window, int id, Rectangle viewport, Rectangle default_rect, Vector2 min_size);
    bool floating_window_open(int id) const { return id == GRAPH_WINDOW ? graph_open : uv_open; }
    FloatingWindow& floating_window(int id) { return id == GRAPH_WINDOW ? graph_window : uv_window; }
    const FloatingWindow& floating_window(int id) const { return id == GRAPH_WINDOW ? graph_window : uv_window; }
    // The mouse is over the other window, and that one is on top.
    bool mouse_covered_by_other(int id) const;
    void draw_floating_window(int id);

    // Animation graph window - every animation as a block, links as arrows.
    Rectangle graph_window_rect(Rectangle viewport) const; // where it first opens
    void draw_graph_window(Rectangle bounds);
    void rename_animation(EntityAnimation& animation, const std::string& new_name);

    // UV window - the skin with every cube's sides laid over it; drag a
    // side to move the cube's whole layout (Shift: just that side).
    Rectangle uv_window_rect(Rectangle viewport) const; // where it first opens
    void draw_uv_window(Rectangle bounds);
    bool over_floating_window(Rectangle viewport, Vector2 point) const;
    float snapped_time() const;

    EntityModel model;
    bool dirty = false;
    std::vector<HistoryState> undo_stack;
    std::vector<HistoryState> redo_stack;
    HistoryState committed_state;    // the model as of the last history step
    bool uncommitted_change = false; // edited since then (see mark_dirty())
    std::string status;
    double status_time = -10.0;

    Texture2D skin{};
    std::vector<std::string> skin_paths; // every PNG under assets/sprites/entities, relative to ASSETS_PATH
    int skin_list_scroll = 0;

    int selected_part = -1;
    int selected_cube = 0;
    int part_list_scroll = 0;
    Vector2 panel_scroll = {0.0f, 0.0f};

    int animation_index = -1; // into model.animations, -1 = none (rest pose)
    float time = 0.0f;
    bool playing = false;
    ModelPose preview_pose; // rotations tried out with no animation selected - not saved
    // Look preview (degrees) - tries out the look node like the game drives
    // it; not saved.
    float look_yaw = 0.0f;
    float look_pitch = 0.0f;

    bool graph_open = false;
    int graph_drag_node = -1;          // animation whose block is being moved
    Vector2 graph_drag_grab = {0, 0};  // where on the block it was grabbed
    int graph_link_from = -1;          // animation a new link is being dragged out of

    FloatingWindow graph_window;
    FloatingWindow uv_window;
    int top_window = UV_WINDOW;
    bool mouse_blocked = false; // the window being drawn is under the other one here

    // Graph view: zoom (1 = blocks at their natural size) and where the
    // graph's origin sits in the window; dragging empty space pans it.
    float graph_zoom = 1.0f;
    Vector2 graph_pan = {0, 0};
    bool graph_panning = false;
    Vector2 graph_pan_grab = {0, 0};

    // UV view: the skin at a whole number of screen pixels per skin pixel
    // (0 = fit it to the window next time it's drawn) - never stretched to
    // the window - and where its corner sits in the window.
    float uv_zoom = 0.0f;
    Vector2 uv_pan = {0, 0};
    bool uv_panning = false;
    Vector2 uv_pan_grab = {0, 0};

    bool uv_open = false;
    int uv_drag_face = -1;             // side being dragged (MODEL_FACE_IDS order), -1 = none
    bool uv_drag_single = false;       // that side alone, not the whole layout
    // A second click on the same layout picks one side: from then on only
    // that side moves (-1 = the whole layout). Remembers which layout was
    // clicked last to tell a first click from a second one.
    int uv_side = -1;
    int uv_clicked_part = -1;
    int uv_clicked_cube = -1;
    Vector2 uv_drag_mouse = {0, 0};    // where the drag started, screen
    Vector2 uv_drag_value = {0, 0};    // the uv/face_uv it started from
    bool scrubbing = false;

    // Orbit camera
    Vector3 orbit_target = {0.0f, 1.0f, 0.0f};
    float orbit_yaw = 0.8f;
    float orbit_pitch = 0.35f;
    float orbit_distance = 5.0f;
    bool orthographic = false;
    Vector2 press_position = {0.0f, 0.0f};
    RenderTexture2D viewport_texture{};

    int widget_counter = 0;
    int editing_widget = -1;
    char model_name[64] = "player";

    std::vector<std::string> model_names; // assets/models/*.json, sorted
    std::string current_model_file;       // what's open was loaded from/saved as - "" while unsaved
    bool models_dropdown_open = false;
    std::string pending_open;             // picked once with unsaved changes - a second pick opens it
    char part_name[64] = "";
    // Layer models (EntityModel::layers - a sheep's wool) drawn over this
    // one in the viewport, loaded from their own files; reloaded whenever
    // the list changes. Not editable here - open the layer model itself.
    struct LayerPreview {
        EntityModel model;
        Texture2D skin{};
    };
    std::vector<LayerPreview> layer_previews;
    std::vector<std::string> layer_previews_for; // model.layers they were loaded for
    bool show_layers = true;
    char layers_text[256] = "";                  // model.layers, comma-separated, as typed
    void refresh_layer_previews();
    void unload_layer_previews();
    char animation_name[64] = "";
};
