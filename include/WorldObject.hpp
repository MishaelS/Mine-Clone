#pragma once

#include "GameObject.hpp"

// Static level geometry: walls, floor, doors, windows, etc.
class WorldObject : public GameObject {
public:
    explicit WorldObject(Vector3 position = {0.0f, 0.0f, 0.0f}, bool solid = true);

    bool IsSolid() const { return solid; }

protected:
    bool solid;
};
