#pragma once

#include "raylib.h"
#include "Entity.hpp"

class Player : public Entity {
public:
    explicit Player(Vector3 position);

    void update(float delta_time) override;
    void draw() const override;
};
