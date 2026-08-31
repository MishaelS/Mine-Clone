#pragma once

#include "GameObject.hpp"

// Dynamic actors: players, mobs, items, etc.
class Entity : public GameObject {
public:
    explicit Entity(Vector3 position = {0.0f, 0.0f, 0.0f});

    void Update(float deltaTime) override;

    Vector3 GetVelocity() const { return velocity; }
    void SetVelocity(Vector3 newVelocity) { velocity = newVelocity; }

protected:
    Vector3 velocity{0.0f, 0.0f, 0.0f};
};
