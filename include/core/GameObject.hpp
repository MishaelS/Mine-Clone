#pragma once

#include "raylib.h"

// Base class for anything that exists in the game world:
// static level geometry (WorldObject) and dynamic actors (Entity).
class GameObject {
public:
    explicit GameObject(Vector3 position = {0.0f, 0.0f, 0.0f});
    virtual ~GameObject() = default;

    virtual void update(float delta_time);
    virtual void draw() const;

    Vector3 get_position() const { return position; }
    void set_position(Vector3 new_position) { position = new_position; }

    bool is_active() const { return active; }
    void set_active(bool value) { active = value; }

protected:
    Vector3 position;
    bool active = true;
};
