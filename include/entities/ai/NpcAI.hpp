#pragma once

#include "entities/ai/Goal.hpp"

// AI for NPCs - people living in one place (villager-like), unlike the
// animals of MobAI.hpp: they aren't tempted by items, stop and face a
// player who walks up to them, keep close to their home and head back
// there when they end up too far away.
namespace ai {

// A player walks up close: stops whatever walk it was on and faces them
// until they step back.
class WatchPlayerCloseGoal : public Goal {
public:
    explicit WatchPlayerCloseGoal(float range) : Goal(MOVE_FLAG | LOOK_FLAG), range(range) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void tick(Mob& mob, const AiContext& context) override;

private:
    float range;
};

// Too far from home (pushed, fell, followed a path out): walks back.
class ReturnHomeGoal : public Goal {
public:
    ReturnHomeGoal(Vector3 home, float radius, float speed) : Goal(MOVE_FLAG), home(home), radius(radius), speed(speed) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void stop(Mob& mob) override;

private:
    Vector3 home;
    float radius;
    float speed;
};

struct NpcAiSettings {
    Vector3 home = {0.0f, 0.0f, 0.0f};
    float home_radius = 12.0f;  // blocks it wanders within
    float stroll_speed = 0.8f;  // speed modifiers - see Mob::walk_speed()
    float talk_range = 3.0f;    // a player this close gets its full attention
    float look_range = 8.0f;
};

void add_npc_goals(GoalSelector& goals, const NpcAiSettings& settings);

} // namespace ai
