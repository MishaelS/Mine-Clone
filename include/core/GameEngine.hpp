#pragma once

#include <memory>
#include <vector>

#include "raylib.h"
#include "GameObject.hpp"

// Owns the window, the main loop, and every GameObject in the game.
class GameEngine {
public:
    GameEngine(int screenWidth, int screenHeight, const char* title);
    ~GameEngine();

    void Run();

    void AddObject(std::unique_ptr<GameObject> object);

private:
    void Update(float deltaTime);
    void Draw();

    // Free-look test camera (WASD + mouse). Swap back to IsoCamera once
    // testing doesn't need to fly around and inspect the world freely.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
};
