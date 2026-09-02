#include "Entity.hpp"

Entity::Entity(Vector3 position)
    : GameObject(position) {}

void Entity::update(float delta_time)
{
    position.x += velocity.x * delta_time;
    position.y += velocity.y * delta_time;
    position.z += velocity.z * delta_time;
}
