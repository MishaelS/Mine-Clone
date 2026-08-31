#pragma once

#include "raylib.h"
#include "Entity.hpp"

class Player : public Entity {
public:
    explicit Player(Vector3 position);

    void Update(float deltaTime) override;
    void Draw() const override;
};
