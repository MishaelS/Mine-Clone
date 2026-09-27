#include "entities/ai/Controls.hpp"
#include "entities/Mob.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace ai {

    namespace {
        constexpr float IDLE_HEAD_STEP    = 10.0f;  // degrees/tick the head eases back without a target
        constexpr float WALKING_HEAD_STEP = 20.0f;  // ...or turns toward where it walks - ahead of the body
        constexpr float MAX_HEAD_PITCH    = 40.0f;  // Minecraft's getMaxHeadXRot()
        constexpr float STEP_UP_HEIGHT    = 0.25f;  // a waypoint this much higher needs a jump

        // Navigation: a node isn't getting any closer after this long -
        // something got in the way; and every STUCK_CHECK_TICKS it must have
        // covered at least STUCK_PROGRESS of the distance it could have.
        constexpr int NODE_TIMEOUT_TICKS = 80;
        constexpr int STUCK_CHECK_TICKS  = 100;
        constexpr float STUCK_PROGRESS = 0.25f;

        float rotate_toward(float from, float to, float max_step) {
            return from + std::clamp(wrap_degrees(to - from), -max_step, max_step);
        }

        float horizontal_distance(Vector3 a, Vector3 b) {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
        }
    }

    void MoveControl::move_to(Vector3 point, float speed_modifier) {
        wanted = point;
        speed  = speed_modifier;
        requested = true;
    }

    void MoveControl::tick(Mob& mob) {
        moving = false;
        if (!requested) {
            mob.set_forward(0.0f);
            return;
        }
        requested = false;

        const Vector3 feet = mob.get_position();
        const float dx       = wanted.x - feet.x, dz = wanted.z - feet.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < 0.01f) {
            mob.set_forward(0.0f);
            return;
        }

        moving = true;
        travel_yaw_degrees = std::atan2(dx, dz) * RAD2DEG;
        mob.turn_body_toward(travel_yaw_degrees, mob.turn_speed());

        // Only ever forward, along the body: full speed once it faces the point,
        // slower while still turning, standing and turning once it's sideways
        // or behind.
        const float facing = std::cos(wrap_degrees(travel_yaw_degrees - mob.body_yaw()) * DEG2RAD);
        const float forward = facing > 0.0f ? mob.walk_speed() * speed * facing : 0.0f;
        mob.set_forward(std::min(forward, distance));

        if (wanted.y - feet.y > STEP_UP_HEIGHT && distance < std::max(1.0f, mob.width()) + 0.5f) mob.set_jumping(true);
    }

    void LookControl::look_at(Vector3 point, float max_yaw_step, float max_pitch_step) {
        target     = point;
        yaw_step   = max_yaw_step;
        pitch_step = max_pitch_step;
        ticks_left = 2; // held through the next tick, like Minecraft's lookAtCooldown
    }

    void LookControl::tick(Mob& mob, const MoveControl& move) {
        float yaw = mob.head_yaw();
        float pitch = mob.head_pitch();
        if (ticks_left > 0) {
            --ticks_left;
            const Vector3 to = Vector3Subtract(target, mob.eye_position());
            const float horizontal = std::sqrt(to.x * to.x + to.z * to.z);
            if (horizontal > 0.001f || std::fabs(to.y) > 0.001f) {
                yaw = rotate_toward(yaw, std::atan2(to.x, to.z) * RAD2DEG, yaw_step);
                pitch = rotate_toward(pitch, -std::atan2(to.y, horizontal) * RAD2DEG, pitch_step);
            }
        } else {
            // Nothing to look at: ahead where it's walking - turning its head
            // there first, the body following - or back in line with the body.
            if (move.is_moving()) yaw = rotate_toward(yaw, move.travel_yaw(), WALKING_HEAD_STEP);
            else yaw = rotate_toward(yaw, mob.body_yaw(), IDLE_HEAD_STEP);
            pitch = rotate_toward(pitch, 0.0f, IDLE_HEAD_STEP);
        }
        pitch = std::clamp(pitch, -MAX_HEAD_PITCH, MAX_HEAD_PITCH);

        // The head only turns so far from the body: walking, it stops there;
        // standing, the body turns along with it.
        const float limit = mob.head_turn_limit();
        if (move.is_moving()) {
            yaw = mob.body_yaw() + std::clamp(wrap_degrees(yaw - mob.body_yaw()), -limit, limit);
        } else {
            mob.set_body_yaw(body_yaw_following_look(mob.model(), mob.body_yaw(), yaw));
        }
        mob.set_head(yaw, pitch);
    }

    bool Navigation::move_to(Mob& mob, const World& world, Vector3 target, float speed_modifier) {
        std::optional<Path> found = find_path(world, mob.get_position(), target, mob.path_settings());
        if (!found || found->nodes.empty()) {
            stop();
            return false;
        }
        // Keep the stuck check running across re-plans toward a moving target.
        if (!active) {
            ticks_walking = 0;
            stuck_check_position = mob.get_position();
        }
        current = std::move(*found);
        index = 0;
        speed = speed_modifier;
        active = true;
        ticks_on_node = 0;
        return true;
    }

    void Navigation::stop() {
        active = false;
        current.nodes.clear();
        index = 0;
    }

    void Navigation::tick(Mob& mob) {
        if (!active) return;
        const Vector3 feet = mob.get_position();

        // Minecraft's waypoint radius: close enough to count as there.
        const float reach = mob.width() > 0.75f ? mob.width() * 0.5f : 0.75f - mob.width() * 0.5f;
        while (index < current.nodes.size()) {
            const PathNode& node = current.nodes[index];
            const Vector3 center = node.center();
            bool passed = horizontal_distance(feet, center) < reach && std::fabs(feet.y - center.y) < 1.0f;
            // Already past this node toward the next one on the same level (it
            // cut the corner): no turning back for it.
            if (!passed && index + 1 < current.nodes.size() && current.nodes[index + 1].y == node.y &&
                horizontal_distance(feet, center) < 1.0f) {
                const Vector3 next = current.nodes[index + 1].center();
                passed = (feet.x - center.x) * (next.x - center.x) + (feet.z - center.z) * (next.z - center.z) > 0.0f;
            }
            if (!passed) break;
            ++index;
            ticks_on_node = 0;
        }

        if (index >= current.nodes.size()) {
            stop();
            return;
        }

        if (++ticks_on_node > NODE_TIMEOUT_TICKS) {
            stop();
            return;
        }

        if (++ticks_walking % STUCK_CHECK_TICKS == 0) {
            const float expected = mob.walk_speed() * speed * STUCK_CHECK_TICKS;
            if (horizontal_distance(feet, stuck_check_position) < expected * STUCK_PROGRESS) {
                stop();
                return;
            }
            stuck_check_position = feet;
        }

        mob.move_control().move_to(current.nodes[index].center(), speed);
    }

} // namespace ai
