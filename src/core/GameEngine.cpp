#include "core/GameEngine.hpp"
#include "Block.hpp"
#include "BlockTextures.hpp"
#include "Skybox.hpp"

namespace {
    constexpr float CAMERA_MOVE_SPEED = 10.0f; // world units per second
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;
}

GameEngine::GameEngine(int screenWidth, int screenHeight, const char* title) {
    InitWindow(screenWidth, screenHeight, title);
    SetTargetFPS(60);
    LoadBlockDefinitions(); // needs a GL context, so only after InitWindow

    camera.position = {0.0f, 10.0f, 25.0f};
    camera.target = {0.0f, 8.0f, 0.0f}; // roughly the starting chunk's center
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    DisableCursor(); // mouse-look needs the cursor captured
}

GameEngine::~GameEngine() {
    EnableCursor();
    UnloadBlockTextures();
    CloseWindow();
}

void GameEngine::AddObject(std::unique_ptr<GameObject> object) {
    objects.push_back(std::move(object));
}

void GameEngine::Update(float deltaTime) {
    // Free-look test camera: WASD to move, mouse to look, Space/Shift to fly up/down.
    Vector3 movement = {0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) movement.x += CAMERA_MOVE_SPEED * deltaTime;
    if (IsKeyDown(KEY_S)) movement.x -= CAMERA_MOVE_SPEED * deltaTime;
    if (IsKeyDown(KEY_D)) movement.y += CAMERA_MOVE_SPEED * deltaTime;
    if (IsKeyDown(KEY_A)) movement.y -= CAMERA_MOVE_SPEED * deltaTime;
    if (IsKeyDown(KEY_SPACE)) movement.z += CAMERA_MOVE_SPEED * deltaTime;
    if (IsKeyDown(KEY_LEFT_SHIFT)) movement.z -= CAMERA_MOVE_SPEED * deltaTime;

    Vector2 mouseDelta = GetMouseDelta();
    Vector3 rotation = {mouseDelta.x * CAMERA_MOUSE_SENSITIVITY, mouseDelta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};

    UpdateCameraPro(&camera, movement, rotation, 0.0f);

    for (auto& object : objects) {
        if (object->IsActive()) {
            object->Update(deltaTime);
        }
    }
}

void GameEngine::Draw() {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    BeginMode3D(camera);
    DrawSkybox(camera.position);
    DrawGrid(20, 1.0f); // temporary ground reference until World handles multiple chunks
    for (auto& object : objects) {
        if (object->IsActive()) {
            object->Draw();
        }
    }
    EndMode3D();

    EndDrawing();
}

void GameEngine::Run() {
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        Update(deltaTime);
        Draw();
    }
}
