#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/TextureManager.hpp"
#include "Skybox.hpp"

namespace {
    constexpr float CAMERA_MOVE_SPEED = 10.0f; // world units per second
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;
}

GameEngine::GameEngine(int screen_width, int screen_height, const char* title)
{
    InitWindow(screen_width, screen_height, title);
    SetTargetFPS(60);
    Load_block_definitions(); // needs a GL context, so only after InitWindow

    camera.position = {0.0f, 10.0f, 25.0f};
    camera.target = {0.0f, 8.0f, 0.0f}; // roughly the starting chunk's center
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

void GameEngine::update(float delta_time)
{
    // Free-look test camera: WASD to move, mouse to look, Space/Shift to fly up/down.
    Vector3 movement = {0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) movement.x += CAMERA_MOVE_SPEED * delta_time;
    if (IsKeyDown(KEY_S)) movement.x -= CAMERA_MOVE_SPEED * delta_time;
    if (IsKeyDown(KEY_D)) movement.y += CAMERA_MOVE_SPEED * delta_time;
    if (IsKeyDown(KEY_A)) movement.y -= CAMERA_MOVE_SPEED * delta_time;

    if (IsKeyDown(KEY_SPACE))      movement.z += CAMERA_MOVE_SPEED * delta_time;
    if (IsKeyDown(KEY_LEFT_SHIFT)) movement.z -= CAMERA_MOVE_SPEED * delta_time;

    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};

    UpdateCameraPro(&camera, movement, rotation, 0.0f);

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
    DrawGrid(20, 1.0f); // temporary ground reference until World handles multiple chunks
    for (auto& object : objects) {
        if (object->is_active()) {
            object->draw();
        }
    }
    EndMode3D();

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
