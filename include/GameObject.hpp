#pragma once

#include "raylib.h"

// Base class for anything that exists in the game world:
// static level geometry (WorldObject) and dynamic actors (Entity).
class GameObject {
public:
    explicit GameObject(Vector3 position = {0.0f, 0.0f, 0.0f});
    virtual ~GameObject() = default;

    virtual void Update(float deltaTime);
    virtual void Draw() const;

    Vector3 GetPosition() const { return position; }
    void SetPosition(Vector3 newPosition) { position = newPosition; }

    bool IsActive() const { return active; }
    void SetActive(bool value) { active = value; }

protected:
    Vector3 position;
    bool active = true;
};
