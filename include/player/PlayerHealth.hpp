#pragma once

#include <cstdint>

// What actually hurt the player - GameEngine uses this only to decide which
// situational check dealt the damage (there's no per-source visual/audio
// difference yet, but keeping the source around leaves room for one, and
// for a future death-message HUD line like real Minecraft's own "Player
// fell from a high place").
enum class DamageSource : uint8_t {
    Fall,
    Drown,
    Lava,
    Fire,
    Cactus,
    Suffocation,
    Void,
};

// Survival-only hit points, in the same half-heart units real Minecraft's
// own damage numbers already use (a full heart is 2 points - see
// assets/sprites/gui/hearts/heart{0,1,2}.png for the full/half/empty frames
// GameEngine's HUD draws from this). GameEngine never touches Creative
// players with this at all (matching vanilla's own Creative invulnerability)
// - see its own current_game_mode checks.
//
// No passive regeneration and no hunger system: eating food (see
// ItemProperties::heal_amount and GameEngine's own right-click-to-eat
// handling) is the only way to recover lost health now - it isn't
// restored just by standing around unhurt for a while any more.
class PlayerHealth {
public:
    static constexpr int MAX_HEALTH = 20; // 10 hearts

    // Full health, no invulnerability - called on a brand new world/spawn
    // and every respawn after death.
    void reset();

    // Applies `amount` half-hearts of damage from `source`, unless still
    // within post-hit invulnerability or already dead. Returns true if it
    // actually landed - GameEngine uses that to drive the hurt-flash
    // overlay, so a blocked hit (still invulnerable) doesn't flash twice.
    // amount <= 0 is always a no-op.
    bool damage(int amount, DamageSource source);

    // Restores `amount` half-hearts (from eating food), clamped to
    // MAX_HEALTH; a no-op once dead, same as damage() refusing to touch a
    // dead player from the other direction. Doesn't touch invulnerability
    // - eating isn't blocked by the post-hit window, and doesn't grant one
    // either.
    void heal(int amount);

    // Per-frame upkeep: counts the post-hit invulnerability window down -
    // call exactly once per frame while playing in Survival.
    void update(float delta_time);

    // Sets health directly, clamped to [0, MAX_HEALTH], without touching
    // invulnerability the way damage()/reset() do - only for restoring a
    // saved value on load (WorldSave::PlayerSaveState::health), where none
    // of that per-hit bookkeeping is meaningful.
    void set_health(int value);

    // Instantly reduces health to 0, ignoring invulnerability - only for
    // the "/kill" chat command. No situational damage source should ever
    // bypass invulnerability this way; damage() is what those all go
    // through instead.
    void kill();

    int health() const { return current_health; }
    bool is_dead() const { return current_health <= 0; }
    bool is_invulnerable() const { return invulnerable_seconds > 0.0f; }
    DamageSource last_damage_source() const { return last_source; }

private:
    int current_health = MAX_HEALTH;
    float invulnerable_seconds = 0.0f;
    DamageSource last_source = DamageSource::Fall;
};
