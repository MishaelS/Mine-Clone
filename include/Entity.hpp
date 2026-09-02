#pragma once

#include "core/GameObject.hpp"

// Dynamic actors: players, mobs, items, etc.
class Entity : public GameObject {
public:
    explicit Entity(Vector3 position = {0.0f, 0.0f, 0.0f});

    void update(float delta_time) override;

    Vector3 get_velocity() const { return velocity; }
    void set_velocity(Vector3 new_velocity) { velocity = new_velocity; }

protected:
    Vector3 velocity{0.0f, 0.0f, 0.0f};
};
