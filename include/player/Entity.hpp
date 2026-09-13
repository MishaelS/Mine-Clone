#pragma once

#include "core/GameObject.hpp"

// Dynamic actors: dropped items today, more later. Just a velocity holder
// on top of GameObject - each subclass runs its own physics however suits
// it (DroppedItem's is tick-based; see its own tick_physics()/TickMotion).
class Entity : public GameObject {
public:
    explicit Entity(Vector3 position = {0.0f, 0.0f, 0.0f});

    Vector3 get_velocity() const { return velocity; }
    void set_velocity(Vector3 new_velocity) { velocity = new_velocity; }

protected:
    Vector3 velocity{0.0f, 0.0f, 0.0f};
};
