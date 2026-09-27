#include "entities/ai/MobAI.hpp"
#include "entities/ai/CommonGoals.hpp"
#include "entities/Mob.hpp"

#include "raymath.h"

#include <algorithm>

namespace ai {

    namespace {
        constexpr float TEMPT_RANGE         = 10.0f; // blocks
        constexpr float TEMPT_STOP_DISTANCE = 2.5f;  // close enough - stands and watches
        constexpr int TEMPT_CALM_DOWN_TICKS = 100;
        constexpr int TEMPT_REPLAN_TICKS    = 10;
        constexpr float TEMPT_REPLAN_DISTANCE = 1.0f; // the player moved this far since the last plan
    }

    bool TemptGoal::tempted(Mob& mob, const AiContext& context) const {
        const PlayerView& player = context.player;
        if (!player.present || player.held.empty() || !player.held.holds_item()) return false;
        if (std::find(items.begin(), items.end(), player.held.tool) == items.end()) return false;
        return Vector3Distance(mob.get_position(), player.feet) <= TEMPT_RANGE;
    }

    bool TemptGoal::can_use(Mob& mob, const AiContext& context) {
        if (calm_down_ticks > 0) {
            --calm_down_ticks;
            return false;
        }
        return tempted(mob, context);
    }

    bool TemptGoal::can_continue(Mob& mob, const AiContext& context) {
        return tempted(mob, context);
    }

    void TemptGoal::start(Mob&, const AiContext&) {
        replan_ticks = 0;
    }

    void TemptGoal::stop(Mob& mob) {
        mob.navigation().stop();
        calm_down_ticks = TEMPT_CALM_DOWN_TICKS;
    }

    void TemptGoal::tick(Mob& mob, const AiContext& context) {
        const PlayerView& player = context.player;
        mob.look_control().look_at(player.eyes, 30.0f, 40.0f);

        if (Vector3Distance(mob.get_position(), player.feet) < TEMPT_STOP_DISTANCE) {
            mob.navigation().stop();
            return;
        }
        // Re-planned as the player moves - and now and then anyway, in case a
        // block placed or broken opened or closed the way.
        if (--replan_ticks <= 0 || Vector3Distance(planned_for, player.feet) > TEMPT_REPLAN_DISTANCE) {
            mob.navigation().move_to(mob, context.world, player.feet, speed);
            planned_for = player.feet;
            replan_ticks = TEMPT_REPLAN_TICKS;
        }
    }

    void add_animal_goals(GoalSelector& goals, const AnimalAiSettings& settings) {
        StrollSettings stroll;
        stroll.speed = settings.stroll_speed;
        stroll.avoid_water = true;
        stroll.prefer_grass = true;

        goals.add(0, std::make_unique<FloatGoal>());
        if (!settings.tempt_items.empty()) goals.add(3, std::make_unique<TemptGoal>(settings.tempt_speed, settings.tempt_items));
        goals.add(5, std::make_unique<RandomStrollGoal>(stroll));
        goals.add(6, std::make_unique<LookAtPlayerGoal>(settings.look_range, 0.02f));
        goals.add(7, std::make_unique<RandomLookAroundGoal>());
    }

} // namespace ai
