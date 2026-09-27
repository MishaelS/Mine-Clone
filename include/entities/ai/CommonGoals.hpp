#pragma once

#include "entities/ai/Goal.hpp"

#include <optional>

// Goals every kind of mob uses - animals (MobAI.hpp) and NPCs (NpcAI.hpp)
// alike. Named after, and behaving like, Minecraft's own.
namespace ai {

// Keeps its head above water: swims up whenever it's deep enough in.
class FloatGoal : public Goal {
public:
    FloatGoal() : Goal(JUMP_FLAG) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    void tick(Mob& mob, const AiContext& context) override;
};

struct StrollSettings {
    float speed = 1.0f;
    int interval         = 120; // on average one walk every this many idle ticks
    int horizontal_range = 10;  // blocks from where it stands
    int vertical_range   = 7;
    bool avoid_water  = true;
    bool prefer_grass = false;   // animals pick grass over anything else
    std::optional<Vector3> home; // never wanders off further than home_radius from here
    float home_radius = 16.0f;
};

// Now and then walks to a random spot nearby, along a planned path.
class RandomStrollGoal : public Goal {
public:
    explicit RandomStrollGoal(StrollSettings settings) : Goal(MOVE_FLAG), settings(settings) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void stop(Mob& mob) override;

private:
    std::optional<Vector3> pick_target(Mob& mob, const World& world) const;

    StrollSettings settings;
    Vector3 target = {0.0f, 0.0f, 0.0f};
};

// A player close by catches its eye now and then: it turns its head to
// them, watches for a few seconds, then - loses interest and turns away.
class LookAtPlayerGoal : public Goal {
public:
    LookAtPlayerGoal(float range, float chance_per_tick)
        : Goal(LOOK_FLAG), range(range), chance(chance_per_tick) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void tick(Mob& mob, const AiContext& context) override;

private:
    bool player_in_range(Mob& mob, const AiContext& context) const;

    float range;
    float chance;
    int look_ticks = 0;
    int look_away_ticks = 0;
    float look_away_yaw = 0.0f; // degrees
};

// With nothing else to do, looks off in a random direction for a moment.
class RandomLookAroundGoal : public Goal {
public:
    RandomLookAroundGoal() : Goal(MOVE_FLAG | LOOK_FLAG) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void tick(Mob& mob, const AiContext& context) override;

private:
    float yaw = 0.0f; // degrees
    int ticks = 0;
};

} // namespace ai
