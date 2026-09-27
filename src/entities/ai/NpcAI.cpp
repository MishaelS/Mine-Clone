#include "entities/ai/NpcAI.hpp"
#include "entities/ai/CommonGoals.hpp"
#include "entities/Mob.hpp"

#include "raymath.h"

namespace ai {

    namespace {
        constexpr float WATCH_RELEASE_MARGIN = 1.0f; // blocks past talk range before it lets go
    }

    bool WatchPlayerCloseGoal::can_use(Mob& mob, const AiContext& context) {
        return context.player.present && Vector3Distance(mob.eye_position(), context.player.eyes) <= range;
    }

    bool WatchPlayerCloseGoal::can_continue(Mob& mob, const AiContext& context) {
        return context.player.present &&
            Vector3Distance(mob.eye_position(), context.player.eyes) <= range + WATCH_RELEASE_MARGIN;
    }

    void WatchPlayerCloseGoal::start(Mob& mob, const AiContext&) {
        mob.navigation().stop();
    }

    void WatchPlayerCloseGoal::tick(Mob& mob, const AiContext& context) {
        mob.look_control().look_at(context.player.eyes, 10.0f, 40.0f);
    }

    bool ReturnHomeGoal::can_use(Mob& mob, const AiContext&) {
        // Checked about once a second, not every tick - a way home that's
        // blocked shouldn't be re-planned 20 times a second.
        return mob.random_int(0, 19) == 0 && Vector3Distance(mob.get_position(), home) > radius;
    }

    bool ReturnHomeGoal::can_continue(Mob& mob, const AiContext&) {
        return !mob.navigation().done();
    }

    void ReturnHomeGoal::start(Mob& mob, const AiContext& context) {
        mob.navigation().move_to(mob, context.world, home, speed);
    }

    void ReturnHomeGoal::stop(Mob& mob) {
        mob.navigation().stop();
    }

    void add_npc_goals(GoalSelector& goals, const NpcAiSettings& settings) {
        StrollSettings stroll;
        stroll.speed = settings.stroll_speed;
        stroll.avoid_water = true;
        stroll.home = settings.home;
        stroll.home_radius = settings.home_radius;
        stroll.horizontal_range = 8;

        goals.add(0, std::make_unique<FloatGoal>());
        goals.add(2, std::make_unique<WatchPlayerCloseGoal>(settings.talk_range));
        goals.add(4, std::make_unique<ReturnHomeGoal>(settings.home, settings.home_radius, settings.stroll_speed));
        goals.add(5, std::make_unique<RandomStrollGoal>(stroll));
        goals.add(6, std::make_unique<LookAtPlayerGoal>(settings.look_range, 0.025f));
        goals.add(7, std::make_unique<RandomLookAroundGoal>());
    }

} // namespace ai
