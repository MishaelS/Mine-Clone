#pragma once

#include "entities/Entity.hpp"
#include "entities/ai/Controls.hpp"
#include "entities/ai/Goal.hpp"
#include "core/TickMotion.hpp"
#include "model/EntityModel.hpp"

#include <cstdint>
#include <random>

class World;

// Every AI-driven creature - animals (see Animal) and NPCs (see Npc). Owns
// what they all share: tick physics (gravity, swimming, collisions, being
// pushed), Minecraft's AI plumbing (a GoalSelector of goals steering a
// Navigation, MoveControl and LookControl - see entities/ai/) and drawing
// its model with the walk animation and head look. Subclasses pick the
// model, size, speeds and goals.
class Mob : public Entity {
public:
    struct Dimensions {
        float width;      // blocks
        float height;
        float eye_height;
    };

    Mob(Vector3 feet_position, float yaw_degrees, uint32_t seed, Dimensions dimensions);
    ~Mob() override = default;

    // Its name in /summon and mobs.json.
    virtual const char* type_id() const = 0;
    virtual const EntityModel& model() const = 0;

    // One game tick: AI goals, then movement and physics.
    void tick(const World& world, const ai::PlayerView& player);
    void render(float tick_alpha, const World& world) const;

    // The simulated position (not GameObject's own field), like DroppedItem.
    Vector3 get_position() const { return motion.current; }
    float get_yaw() const { return body_yaw_degrees; }
    float width() const { return dimensions.width; }
    float height() const { return dimensions.height; }

    // Where it looks: its eyes and the unit direction (for F3+B).
    Vector3 eye_position() const;
    Vector3 look_direction() const;

    // Another hitbox shoved into this one (blocks/tick, horizontal) -
    // carried over into its next ticks' movement, fading out.
    void push(float dx, float dz);

    // Whether its hitbox overlaps block cell (x, y, z) - placing a block
    // there is refused.
    bool intersects_block(int x, int y, int z) const;

    // --- For the AI (entities/ai/) ---

    ai::Navigation& navigation() { return path_navigation; }
    const ai::Navigation& navigation() const { return path_navigation; }
    ai::MoveControl& move_control() { return movement; }
    ai::LookControl& look_control() { return looking; }
    virtual const ai::PathSettings& path_settings() const = 0;

    // Blocks/tick at speed modifier 1, and how fast its body turns while
    // walking (degrees/tick).
    virtual float walk_speed() const = 0;
    virtual float turn_speed() const { return 12.0f; }

    float body_yaw() const { return body_yaw_degrees; }
    void set_body_yaw(float yaw) { body_yaw_degrees = yaw; }
    void turn_body_toward(float yaw, float max_step);
    float head_yaw() const { return head_yaw_degrees; }
    float head_pitch() const { return head_pitch_degrees; } // positive = down
    void set_head(float yaw, float pitch);
    // How far the head turns from the body (the model's look part).
    float head_turn_limit() const;

    // This tick's movement input, reset every tick: forward speed along the
    // body (blocks/tick) and whether to jump / swim up.
    void set_forward(float blocks_per_tick) { forward_speed = blocks_per_tick; }
    void set_jumping(bool jump) { jumping = jump; }

    bool on_ground() const { return grounded; }
    // Water at `height` blocks above its feet.
    bool water_at(const World& world, float height) const;

    float random_float() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    int random_int(int min, int max) { return std::uniform_int_distribution<int>(min, max)(rng); }

protected:
    // World units per model pixel.
    virtual float model_scale() const { return 1.0f / 16.0f; }

    ai::GoalSelector goals;

private:
    bool blocked(const World& world, Vector3 feet) const;
    void move(const World& world);

    Dimensions dimensions;
    TickMotion motion;
    float body_yaw_degrees    = 0.0f;
    float previous_body_yaw   = 0.0f;
    float head_yaw_degrees    = 0.0f;
    float previous_head_yaw   = 0.0f;
    float head_pitch_degrees  = 0.0f;
    float previous_head_pitch = 0.0f;
    bool grounded = false;

    float forward_speed = 0.0f;
    bool jumping = false;
    Vector3 push_velocity = {0.0f, 0.0f, 0.0f}; // blocks/tick, from push()

    ai::Navigation path_navigation;
    ai::MoveControl movement;
    ai::LookControl looking;
    std::mt19937 rng;

    // Render-side animation state (visual only).
    mutable EntityAnimator animator;
    mutable Vector3 last_render_position = {0.0f, 0.0f, 0.0f};
    mutable bool rendered_before = false;
};
