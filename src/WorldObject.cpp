#include "WorldObject.hpp"

WorldObject::WorldObject(Vector3 position, bool solid)
    : GameObject(position), solid(solid) {}
