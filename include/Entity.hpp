#pragma once

#include "core/GameObject.hpp"

// Dynamic actors: players, mobs, items, etc.
class Entity : public GameObject {
public:
    explicit Entity(Vector3 position = {0.0f, 0.0f, 0.0f});

    // Applies get_velocity() to position, same as before, except now
    // scaled by WATER_DRAG whenever `world` says this entity's position is
    // inside a Water block — Minecraft's own underwater movement slowdown.
    // Deliberately not applied to GameEngine's own free-look camera, which
    // isn't an Entity at all (it's a spectator-style camera with no
    // collision of its own) — only to whatever Entities the game actually
    // simulates through this same update().
    void update(float delta_time, const World* world) override;

    Vector3 get_velocity() const { return velocity; }
    void set_velocity(Vector3 new_velocity) { velocity = new_velocity; }

protected:
    Vector3 velocity{0.0f, 0.0f, 0.0f};

private:
    // Multiplies delta_time*velocity while submerged — 1.0 means no slowdown
    // at all; Minecraft's own underwater movement is roughly half speed.
    static constexpr float WATER_DRAG = 0.5f;
};
