#include "core/GameObject.hpp"

GameObject::GameObject(Vector3 position)
    : position(position) {}

void GameObject::update(float, const World*) {}

void GameObject::draw() const {}
