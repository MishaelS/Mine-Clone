#include "Player.hpp"

#include <cmath>

namespace {
    constexpr float PLAYER_SPEED = 4.5f; // world units (blocks) per second
    constexpr float PLAYER_WIDTH = 0.6f;
    constexpr float PLAYER_HEIGHT = 1.8f;
}

Player::Player(Vector3 position) : Entity(position) {}

void Player::Update(float deltaTime) {
    // World axes: X/Z is the ground plane, Y is up. W/S move along Z, A/D along X.
    Vector3 direction = {0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) direction.z -= 1.0f;
    if (IsKeyDown(KEY_S)) direction.z += 1.0f;
    if (IsKeyDown(KEY_A)) direction.x -= 1.0f;
    if (IsKeyDown(KEY_D)) direction.x += 1.0f;

    float length = sqrtf(direction.x * direction.x + direction.z * direction.z);
    if (length > 0.0f) {
        direction.x /= length;
        direction.z /= length;
    }

    SetVelocity({direction.x * PLAYER_SPEED, 0.0f, direction.z * PLAYER_SPEED});
    Entity::Update(deltaTime);
}

void Player::Draw() const {
    DrawCube(GetPosition(), PLAYER_WIDTH, PLAYER_HEIGHT, PLAYER_WIDTH, WHITE);
    DrawCubeWires(GetPosition(), PLAYER_WIDTH, PLAYER_HEIGHT, PLAYER_WIDTH, BLACK);
}
