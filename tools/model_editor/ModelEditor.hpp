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
    void open_model();
    void save_model();
    void load_skin(const std::string& path);
    void select_part(int index);
    void add_part();
    void delete_selected_part();
    void rename_selected_part(const std::string& new_name);
    bool is_descendant(int part, int ancestor) const;
    std::vector<int> part_display_order(std::vector<int>* depths) const;
    void set_status(const std::string& text);
    void mark_dirty() { dirty = true; }

    // Animation
    EntityAnimation* current_animation();
    ModelPose current_pose();         // animation (or rotation preview) only - what the sliders edit
    ModelPose displayed_pose();       // current_pose() plus the look preview - what the viewport shows
    float displayed_body_yaw() const; // the body turned by the look preview, degrees
    // Sets the selected part's pose - a keyframe at the current time with
    // an animation selected (auto-key), else the unsaved preview.
    void set_selected_pose(PartPose pose);

    // Animation graph window - every animation as a block, links as arrows.
    Rectangle graph_window_rect(Rectangle viewport) const;
    void draw_graph_window(Rectangle bounds);
    void rename_animation(EntityAnimation& animation, const std::string& new_name);
    float snapped_time() const;

    EntityModel model;
    bool dirty = false;
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
    char part_name[64] = "";
    char animation_name[64] = "";
};
