#include "Entity.hpp"
#include "World.hpp"

Entity::Entity(Vector3 position)
    : GameObject(position) {}

void Entity::update(float delta_time, const World* world)
{
    float drag = (world && world->water_depth_at(position).has_value()) ? WATER_DRAG : 1.0f;
    position.x += velocity.x * delta_time * drag;
    position.y += velocity.y * delta_time * drag;
    position.z += velocity.z * delta_time * drag;
}
