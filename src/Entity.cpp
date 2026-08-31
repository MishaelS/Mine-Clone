#include "Entity.hpp"

Entity::Entity(Vector3 position)
    : GameObject(position) {}

void Entity::Update(float deltaTime) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
    position.z += velocity.z * deltaTime;
}
