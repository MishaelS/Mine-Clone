#include "core/GameObject.hpp"

GameObject::GameObject(Vector3 position)
    : position(position) {}

void GameObject::update(float delta_time) {}

void GameObject::draw() const {}
