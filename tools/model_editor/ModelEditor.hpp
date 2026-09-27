#pragma once

#include "content/BlockFile.hpp"
#include "effects/BlockParticles.hpp"
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
    // Tabs, top left: entity models (everything below up to "Blocks tab"),
    // and blocks (BlockTab.cpp).
    enum class Tab { Entities, Blocks };
    Tab tab = Tab::Entities;
    void draw_tabs();

    // --- Blocks tab (BlockTab.cpp): every full-cube block's file,
    // assets/blocks/<name>.json - see content/BlockFile.hpp. ---
    void run_blocks_frame();
    void load_blocks();
    void select_block(int index);
    void save_block(int index);
    void save_all_blocks();
    void draw_blocks_top_bar(Rectangle bounds);
    void draw_block_list(Rectangle bounds);
    void draw_block_properties(Rectangle bounds);
    void draw_block_hitbox_panel(Rectangle bounds);
    void draw_block_model_panel(Rectangle bounds);
    void update_block_camera(Rectangle view);
    void draw_block_preview(Rectangle view);
    // Two small panels over the block view: the block as the game draws it
    // in the world (a little scene), and its inventory icon.
    void draw_block_game_view(Rectangle bounds);
    void draw_block_inventory_icon(Rectangle bounds);
    const Texture2D& terrain_atlas();
    const Texture2D& items_atlas();
    const Texture2D& particle_sheet();
    void draw_block_particles_panel(Rectangle bounds);
    void update_block_particles(float dt);
    void mark_block_dirty();
    void commit_block_history(bool force = false);
    void block_undo();
    void block_redo();

    std::vector<block_file::BlockFile> blocks;
    std::vector<bool> block_dirty; // edited since last saved
    bool blocks_loaded = false;
    int selected_block = -1;
    int selected_face = 0;         // BlockFace order - the face the atlas and tint edit
    std::string block_search;
    Vector2 block_list_scroll = {0, 0};
    Vector2 block_panel_scroll = {0, 0};
    Vector2 block_hitbox_scroll = {0, 0};
    int block_panel_tab = 0;   // the right panel: 0 properties, 1 model (its parts), 2 hitbox, 3 particles
    Vector2 block_particles_scroll = {0, 0};
    int selected_emitter = 0;  // the particle emitter being edited
    // What the selected block gives off, live: around it in the preview and
    // round its two copies "in the game" - emitted on the game's 20/s clock.
    std::vector<block_particles::Particle> preview_particles;
    std::vector<block_particles::Particle> game_particles;
    float particle_clock = 0.0f;
    int particles_block = -1; // whose they are - cleared on picking another
    uint32_t particle_random = 0x6C8E9CF5u;
    Vector2 block_model_scroll = {0, 0};
    int selected_element = 0;  // the part of its model being edited
    bool uv_dragging = false;  // drawing a face's UV rectangle over its tile
    Vector2 uv_drag_from = {0, 0};
    int block_state_view = 0;  // which of its states (a torch: floor, wall) is shown and edited
    bool show_hitbox = true;   // its hitbox drawn over the preview and "in the game"
    bool block_large_view = false; // a block that joins sideways (a chest) shown joined to a second one
    RenderTexture2D block_view_texture{};
    RenderTexture2D block_game_texture{};
    int game_ui_scale = 2; // the game's settings.json ui_scale - its inventory's size
    float block_yaw = 0.8f, block_pitch = 0.45f, block_distance = 2.6f;
    bool block_view_dragging = false;
    Vector2 block_press_position = {0, 0};
    // Undo for the blocks tab: (block index, its state before) steps.
    std::vector<std::pair<int, block_file::BlockFile>> block_undo_stack;
    std::vector<std::pair<int, block_file::BlockFile>> block_redo_stack;
    block_file::BlockFile block_committed;
    int block_committed_index = -1;
    bool block_uncommitted = false;

    // Layout
    Rectangle top_bar_rect() const;
    Rectangle right_panel_rect() const;
    Rectangle left_panel_rect() const;
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
    // The entity's description (EntityModel::entity): health, where it
    // lives, drops, interactions, natural spawning - what the game runs.
    void draw_left_panel(Rectangle bounds);
    float draw_part_properties(float x, float y, float width);
    void draw_timeline(Rectangle bounds);

    // Widgets - raygui needs to know which box is being typed into; these
    // track that by call order within the frame.
    bool text_field(Rectangle bounds, char* buffer, int size); // true when editing just finished
    // A text field editing `value` itself - true once an edit changed it.
    bool string_field(Rectangle bounds, std::string& value);
    bool int_field(Rectangle bounds, int& value, int min_value, int max_value);
    bool float_as_int_field(Rectangle bounds, float& value, int min_value, int max_value);
    // A number with decimals: dragged across (0.1 a step, Shift 0.01), or
    // clicked and typed in.
    bool float_field(Rectangle bounds, float& value, float min_value, float max_value);
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
    Vector2 entity_panel_scroll = {0.0f, 0.0f};
    char string_edit_buffer[256] = ""; // what string_field() is editing
    char float_edit_buffer[32] = "";   // what float_field() is editing
    int float_drag_id = -1;            // the float_field() being dragged
    float float_drag_start = 0.0f;
    float float_drag_value = 0.0f;
    bool float_dragged = false;

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

    // "Player's view": the viewport sees through the player's own eyes -
    // the camera the game draws the first-person hand with (the eye at the
    // origin looking down -Z, 70 degree FOV) - for the first_person model.
    // `preview_item` puts an example in its item slot: 0 nothing (the
    // "empty" animation's pose), 1 a block, 2 a tool, 3 any other item.
    bool player_view = false;
    int preview_item = 0;
    Texture2D preview_items_atlas{};
    Texture2D preview_particle_sheet{};
    Texture2D preview_blocks_atlas{};
    Rectangle preview_combo_rect(Rectangle viewport) const;
    // The "in hand" preview picker shows for any model with item slots.
    bool has_item_slots() const;
    // The game window's width/height (the game's settings.json): the
    // player's view shows exactly that frame, letterboxed in the viewport.
    float game_aspect = 16.0f / 9.0f;
    // Where the 3D view is drawn: the whole viewport, or in the player's
    // view the game-shaped frame inside it.
    Rectangle scene_rect(Rectangle viewport) const;
    // Player's view: picking a pose animation ("empty", "hold_block"...)
    // picks the matching item to show, and picking an item while one is
    // open switches to that item's pose - so what's shown is what the game
    // shows.
    void sync_player_view_pose();
    int synced_animation = -2;
    int synced_preview = -1;
    void draw_item_slots(const ModelPose& pose);

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

    // Panels folded away to a thin strip (their header button, or the
    // strip's own, folds/unfolds them); the timeline's height is dragged by
    // its top edge.
    bool left_panel_open = true;
    bool right_panel_open = true;
    bool timeline_open = true;
    float timeline_height = 190.0f;
    bool timeline_resizing = false;
    float left_width() const;
    float right_width() const;
    float timeline_visible_height() const;
    // A side panel's header: its title and the fold button. Returns the
    // rest of `bounds` below it.
    Rectangle panel_header(Rectangle bounds, const std::string& title, bool& open, bool button_on_right);
    void draw_folded_panels();

    // Timeline view: zoom (pixels per second, 0 = the whole clip fits) and
    // the time at its left edge. Scrubbing snaps to the nearest keyframe,
    // else to a step that shrinks as it zooms in (time_snap_step).
    float timeline_zoom = 0.0f;
    float timeline_scroll = 0.0f;
    bool timeline_panning = false;
    bool timeline_thumb_drag = false;
    float timeline_pan_grab = 0.0f;
    float time_snap_step = 0.05f;
    void jump_to_keyframe(int direction); // -1 previous, +1 next

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
