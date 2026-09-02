#pragma once

#include "core/GameObject.hpp"

// Static level geometry: walls, floor, doors, windows, etc.
class WorldObject : public GameObject {
public:
    explicit WorldObject(Vector3 position = {0.0f, 0.0f, 0.0f}, bool solid = true);

    bool is_solid() const { return solid; }

protected:
    bool solid;
};
