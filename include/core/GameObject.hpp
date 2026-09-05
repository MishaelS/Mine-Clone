#pragma once

#include "raylib.h"

class World;

// Base class for anything that exists in the game world:
// static level geometry (WorldObject) and dynamic actors (Entity).
class GameObject {
public:
    explicit GameObject(Vector3 position = {0.0f, 0.0f, 0.0f});
    virtual ~GameObject() = default;

    // `world` is whatever GameEngine currently holds (see its own update()),
    // nullable since nothing guarantees one exists at every call site — a
    // subclass caring about world state (Entity's water drag) checks it
    // itself rather than every caller having to know that's needed.
    virtual void update(float delta_time, const World* world);
    virtual void draw() const;

    Vector3 get_position() const { return position; }
    void set_position(Vector3 new_position) { position = new_position; }

    bool is_active() const { return active; }
    void set_active(bool value) { active = value; }

protected:
    Vector3 position;
    bool active = true;
};
