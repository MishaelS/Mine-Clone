#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/TextureManager.hpp"
#include "Skybox.hpp"
#include "DebugOverlay.hpp"

#include "raymath.h"
#include "rlgl.h"

namespace {
    constexpr float CAMERA_MOVE_SPEED_DEFAULT = 10.0f; // world units per second
    constexpr float CAMERA_MOVE_SPEED_MIN = 2.0f;
    constexpr float CAMERA_MOVE_SPEED_MAX = 100.0f;
    constexpr float CAMERA_MOVE_SPEED_SCROLL_STEP = 2.0f; // per wheel notch
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;

    constexpr float BREAK_REACH = 10.0f; // max block-breaking distance, in blocks
    constexpr float PLACE_REACH = 15.0f; // max block-placing distance, in blocks

    // No inventory/block-selection system yet, so placing always uses this
    // one block type — Cobblestone rather than a natural block so a
    // player-placed block is visually obvious against the terrain.
    constexpr BlockType PLACE_BLOCK_TYPE = BlockType::Cobblestone;

    // Minecraft-style block-selection outline: very slightly larger than
    // the block itself so its wireframe doesn't z-fight with the block's
    // own faces.
    constexpr float TARGET_OUTLINE_SIZE = 1.002f;
    constexpr Color TARGET_OUTLINE_COLOR = {0, 0, 0, 200};

    constexpr float CROSSHAIR_ARM_LENGTH = 10.0f; // pixels, from center to tip
    constexpr float CROSSHAIR_THICKNESS = 2.0f;   // pixels
    // Slightly off white: with the invert blend below, pure white would
    // fully negate the background; backing off a little keeps the classic
    // Minecraft "soft" look instead of a stark negative.
    constexpr unsigned char CROSSHAIR_INTENSITY = 235;

    // Minecraft's crosshair trick: instead of drawing an opaque or
    // alpha-blended "+", render it with the framebuffer's own color fed
    // back into the blend so each pixel becomes (roughly) its own inverse —
    // result = src*(1-dst) + dst*(1-src). That's what makes it read as
    // legible (and faintly "see-through") over both light and dark terrain,
    // rather than a flat-colored icon that disappears against a similar
    // background.
    void draw_crosshair(int screen_width, int screen_height)
    {
        Color color = {CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, 255};
        float center_x = screen_width / 2.0f;
        float center_y = screen_height / 2.0f;

        rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);

        DrawRectangle(static_cast<int>(center_x - CROSSHAIR_ARM_LENGTH),
                      static_cast<int>(center_y - CROSSHAIR_THICKNESS / 2.0f),
                      static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f),
                      static_cast<int>(CROSSHAIR_THICKNESS), color);
        DrawRectangle(static_cast<int>(center_x - CROSSHAIR_THICKNESS / 2.0f),
                      static_cast<int>(center_y - CROSSHAIR_ARM_LENGTH),
                      static_cast<int>(CROSSHAIR_THICKNESS),
                      static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f), color);

        EndBlendMode();
    }

    // Outlines the block a raycast hit, in world space — the block's own
    // vertices are chunk-mesh-local (0..CHUNK_SIZE within that chunk), but
    // World::raycast already reports hit coordinates in world space, and a
    // wireframe cube doesn't care which chunk (if any) it's logically
    // "in".
    void draw_target_outline(const World::RaycastHit& hit)
    {
        Vector3 center = {hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f};
        DrawCubeWires(center, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_COLOR);
    }
}

GameEngine::GameEngine(int screen_width, int screen_height, const char* title)
    : camera_move_speed(CAMERA_MOVE_SPEED_DEFAULT)
{
    InitWindow(screen_width, screen_height, title);
    SetTargetFPS(60);
    Load_block_definitions(); // needs a GL context, so only after InitWindow

    // World Generation covers world X/Z [0, 512); start roughly above its
    // center, looking down at it.
    camera.position = {256.0f, 30.0f, 281.0f};
    camera.target = {256.0f, 10.0f, 256.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    DisableCursor(); // mouse-look needs the cursor captured
}

GameEngine::~GameEngine()
{
    EnableCursor();
    TextureManager::unload_all();
    CloseWindow();
}

void GameEngine::add_object(std::unique_ptr<GameObject> object)
{
    objects.push_back(std::move(object));
}

void GameEngine::set_world(std::unique_ptr<World> new_world)
{
    world = std::move(new_world);
}

void GameEngine::update(float delta_time)
{
    // Mouse wheel adjusts fly speed (Minecraft creative/spectator-style):
    // one notch = one CAMERA_MOVE_SPEED_SCROLL_STEP, clamped so it can
    // never scroll down to a standstill or up to an uncontrollable blur.
    float wheel_move = GetMouseWheelMove();
    if (wheel_move != 0.0f) {
        camera_move_speed = Clamp(camera_move_speed + wheel_move * CAMERA_MOVE_SPEED_SCROLL_STEP,
                                   CAMERA_MOVE_SPEED_MIN, CAMERA_MOVE_SPEED_MAX);
    }

    // Free-look test camera: WASD to move, mouse to look, Space/Shift to fly up/down.
    Vector3 movement = {0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) movement.x += camera_move_speed * delta_time;
    if (IsKeyDown(KEY_S)) movement.x -= camera_move_speed * delta_time;
    if (IsKeyDown(KEY_D)) movement.y += camera_move_speed * delta_time;
    if (IsKeyDown(KEY_A)) movement.y -= camera_move_speed * delta_time;

    if (IsKeyDown(KEY_SPACE))      movement.z += camera_move_speed * delta_time;
    if (IsKeyDown(KEY_LEFT_SHIFT)) movement.z -= camera_move_speed * delta_time;

    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};

    UpdateCameraPro(&camera, movement, rotation, 0.0f);

    // camera.target isn't a unit vector (it's an arbitrary point ahead of
    // the camera), so the aim direction needs normalizing before it's used
    // as a ray direction.
    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    // Recomputed every frame (not just on click) so draw() can outline
    // whatever's targeted, out to the longer of the two reaches (place's)
    // so the outline still shows a block that's placeable but too far to
    // break.
    targeted_block = world ? world->raycast(camera.position, aim, PLACE_REACH) : std::nullopt;

    // Left click breaks whatever solid block the crosshair is aimed at,
    // within BREAK_REACH blocks. Right click places one block against the
    // face the crosshair is aimed at (the cell just outside the targeted
    // block, in the direction of the hit face's own outward normal),
    // within the longer PLACE_REACH.
    if (world && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (auto hit = world->raycast(camera.position, aim, BREAK_REACH)) {
            world->break_block(hit->x, hit->y, hit->z);
        }
    } else if (world && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (targeted_block) {
            int place_x = targeted_block->x + static_cast<int>(targeted_block->normal.x);
            int place_y = targeted_block->y + static_cast<int>(targeted_block->normal.y);
            int place_z = targeted_block->z + static_cast<int>(targeted_block->normal.z);
            world->place_block(place_x, place_y, place_z, PLACE_BLOCK_TYPE);
        }
    }

    if (IsKeyPressed(KEY_F3)) {
        show_debug_overlay = !show_debug_overlay;
    }

    for (auto& object : objects) {
        if (object->is_active()) {
            object->update(delta_time);
        }
    }
}

void GameEngine::draw()
{
    BeginDrawing();
    ClearBackground(RAYWHITE);

    BeginMode3D(camera);
    draw_skybox(camera.position);
    if (world) {
        world->draw();
    }
    for (auto& object : objects) {
        if (object->is_active()) {
            object->draw();
        }
    }
    if (targeted_block) {
        draw_target_outline(*targeted_block);
    }
    EndMode3D();

    draw_crosshair(GetScreenWidth(), GetScreenHeight());

    if (show_debug_overlay && world) {
        draw_debug_overlay(camera, *world, BREAK_REACH, camera_move_speed);
    }

    EndDrawing();
}

void GameEngine::run()
{
    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();
        update(delta_time);
        draw();
    }
}
