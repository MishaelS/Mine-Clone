#include "entities/ai/Goal.hpp"

#include <algorithm>

namespace ai {

    void GoalSelector::add(int priority, std::unique_ptr<Goal> goal) {
        auto at = std::upper_bound(entries.begin(), entries.end(), priority,
                                [](int p, const Entry& entry) { return p < entry.priority; });
        entries.insert(at, Entry{priority, std::move(goal)});
    }

    void GoalSelector::tick(Mob& mob, const AiContext& context) {
        // Running goals that no longer apply let go of their flags first.
        for (Entry& entry : entries) {
            if (entry.running && !entry.goal->can_continue(mob, context)) {
                entry.goal->stop(mob);
                entry.running = false;
            }
        }

        // Then, most important first, every idle goal that could run takes its
        // flags - from nobody, or from a less important goal that allows it.
        for (Entry& entry : entries) {
            if (entry.running) continue;
            bool available = true;
            for (const Entry& other : entries) {
                if (!other.running || (other.goal->flags() & entry.goal->flags()) == 0) continue;
                if (other.priority <= entry.priority || !other.goal->interruptible()) {
                    available = false;
                    break;
                }
            }
            if (!available || !entry.goal->can_use(mob, context)) continue;

            for (Entry& other : entries) {
                if (other.running && (other.goal->flags() & entry.goal->flags()) != 0) {
                    other.goal->stop(mob);
                    other.running = false;
                }
            }
            entry.goal->start(mob, context);
            entry.running = true;
        }

        for (Entry& entry : entries) {
            if (entry.running) entry.goal->tick(mob, context);
        }
    }

    void GoalSelector::stop_all(Mob& mob) {
        for (Entry& entry : entries) {
            if (!entry.running) continue;
            entry.goal->stop(mob);
            entry.running = false;
        }
    }

} // namespace ai
