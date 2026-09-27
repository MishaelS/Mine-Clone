#pragma once

#include "entities/ai/Pathfinder.hpp"

#include "raylib.h"

class Mob;
class World;

namespace ai {

// Walks the mob toward a point for this one tick, Minecraft's MoveControl -
// but it only ever walks forward, along where its body faces: the body
// turns toward the point at the mob's own turn speed, and it slows down
// (to a turn on the spot) while the point is off to the side or behind.
// Jumps when the point is a block up.
class MoveControl {
public:
    // Asked again every tick it should keep going (Navigation does).
    void move_to(Vector3 point, float speed_modifier);
    void tick(Mob& mob);

    bool is_moving() const { return moving; }
    float travel_yaw() const { return travel_yaw_degrees; } // where it's heading, while is_moving()

private:
    Vector3 wanted = {0.0f, 0.0f, 0.0f};
    float speed = 1.0f;
    bool requested = false;
    bool moving    = false;
    float travel_yaw_degrees = 0.0f;
};

// Turns the head, a limited number of degrees per tick, toward a point a
// goal asks for (Minecraft's LookControl). With nothing asked it looks
// where the mob walks, or back straight ahead. The head never turns further
// from the body than the model's look part allows: past that the body turns
// along while standing, and the head stops at the limit while walking.
class LookControl {
public:
    // Asked again every tick it should keep looking there.
    void look_at(Vector3 point, float max_yaw_step = 10.0f, float max_pitch_step = 40.0f);
    void tick(Mob& mob, const MoveControl& move);

private:
    Vector3 target = {0.0f, 0.0f, 0.0f};
    float yaw_step   = 10.0f;
    float pitch_step = 40.0f;
    int ticks_left = 0;
};

// Plans a path (find_path()) and walks it node by node through the
// MoveControl - Minecraft's PathNavigation. Gives up when the mob gets
// stuck for too long.
class Navigation {
public:
    // False when there's no way to get any closer.
    bool move_to(Mob& mob, const World& world, Vector3 target, float speed_modifier);
    void stop();
    bool done() const { return !active; }
    void tick(Mob& mob);

    // The path being walked and how far along it is (for F3+B).
    const Path& path() const { return current; }
    size_t next_node() const { return index; }

private:
    Path current;
    size_t index = 0;
    float speed = 1.0f;
    bool active = false;
    int ticks_on_node = 0;
    int ticks_walking = 0;
    Vector3 stuck_check_position = {0.0f, 0.0f, 0.0f};
};

} // namespace ai
