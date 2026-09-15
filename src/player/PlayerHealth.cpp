#include "player/PlayerHealth.hpp"

#include <algorithm>

namespace {
    // Same ~10-tick post-hit window real Minecraft grants after any
    // damage - without it, a source that gets re-checked every frame
    // (lava, a cactus you're standing flush against) would apply its "per
    // tick" damage dozens of times a second instead of at the intended
    // once-per-interval rate.
    constexpr float INVULNERABILITY_SECONDS = 0.5f;
}

void PlayerHealth::reset()
{
    current_health = MAX_HEALTH;
    invulnerable_seconds = 0.0f;
}

void PlayerHealth::set_health(int value)
{
    current_health = std::clamp(value, 0, MAX_HEALTH);
}

bool PlayerHealth::damage(int amount, DamageSource source)
{
    if (amount <= 0 || invulnerable_seconds > 0.0f || is_dead()) return false;

    current_health = std::max(0, current_health - amount);
    invulnerable_seconds = INVULNERABILITY_SECONDS;
    last_source = source;
    return true;
}

void PlayerHealth::heal(int amount)
{
    if (amount <= 0 || is_dead()) return;
    current_health = std::min(MAX_HEALTH, current_health + amount);
}

void PlayerHealth::update(float delta_time)
{
    if (invulnerable_seconds > 0.0f) {
        invulnerable_seconds = std::max(0.0f, invulnerable_seconds - delta_time);
    }
}
