#pragma once

#include "entities/ai/Goal.hpp"
#include "items/Item.hpp"

#include <vector>

// AI for mobs - animals roaming the world (cows today). Minecraft's animal
// goal list: stay afloat, follow a player holding food they like, wander
// about preferring grass, glance at a player nearby, look around.
// Compare NpcAI.hpp.
namespace ai {

// Follows a player within range holding one of `items`: walks up to them
// (a planned path, re-planned as they move), stops a couple of blocks away
// and keeps watching them. After the player puts the item away it ignores
// them for a moment before it can be tempted again.
class TemptGoal : public Goal {
public:
    TemptGoal(float speed, std::vector<ItemType> items) : Goal(MOVE_FLAG | LOOK_FLAG), speed(speed), items(std::move(items)) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void stop(Mob& mob) override;
    void tick(Mob& mob, const AiContext& context) override;

private:
    bool tempted(Mob& mob, const AiContext& context) const;

    float speed;
    std::vector<ItemType> items;
    int calm_down_ticks = 0;
    int replan_ticks = 0;
    Vector3 planned_for = {0.0f, 0.0f, 0.0f}; // where the player stood when the path was planned
};

// Now and then lowers its head and eats the grass it stands on: tall grass
// at its feet goes, or else the grass block under it turns to dirt
// (Mob::request_block_change()). Then Mob::on_ate_grass() - a sheared
// sheep grows its wool back. Its model's Manual "eat" animation (head down
// to the ground, chewing) plays meanwhile.
class EatGrassGoal : public Goal {
public:
    EatGrassGoal() : Goal(MOVE_FLAG | LOOK_FLAG | JUMP_FLAG) {}
    bool can_use(Mob& mob, const AiContext& context) override;
    bool can_continue(Mob& mob, const AiContext& context) override;
    void start(Mob& mob, const AiContext& context) override;
    void stop(Mob& mob) override;
    void tick(Mob& mob, const AiContext& context) override;

private:
    int ticks = 0;
};

struct AnimalAiSettings {
    std::vector<ItemType> tempt_items; // what makes it follow a player
    float tempt_speed = 1.25f;         // speed modifiers - see Mob::walk_speed()
    float stroll_speed = 1.0f;
    float look_range = 6.0f;           // blocks
    float panic_speed = 2.0f;          // fleeing after being hit: twice its walk
    bool eats_grass = false;           // see EatGrassGoal
};

void add_animal_goals(GoalSelector& goals, const AnimalAiSettings& settings);

} // namespace ai
