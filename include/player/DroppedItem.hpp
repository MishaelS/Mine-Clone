#pragma once

#include "player/Entity.hpp"
#include "player/Inventory.hpp" // ItemStack
#include "core/TickMotion.hpp"

#include <optional>

class World;

// Player-thrown stacks need a longer owner-safe window than natural block
// drops. A named origin keeps that gameplay distinction explicit at spawn
// sites instead of hiding it behind a boolean constructor argument.
enum class DroppedItemOrigin {
    Natural,
    PlayerThrown,
};

// A block or tool, popped out into the world as a physical item - after
// being broken, or thrown out with Q (GameEngine.cpp's drop handling).
// Physics (gravity/drag/water buoyancy) runs once per game tick - see
// tick_physics() - the same fixed-rate simulation real Minecraft runs
// entities on; render() interpolates between the last two ticks' positions
// so it still reads as smooth motion at whatever frame rate the game is
// rendering at, instead of visibly stepping at 20Hz.
class DroppedItem : public Entity {
public:
    // `launch_velocity` (blocks/tick) is added to the initial upward pop -
    // both a just-broken block (a small kick in whichever direction it was
    // struck from) and a Q-dropped item (thrown out in front of the
    // player) give this a real direction instead of every drop popping
    // straight up in place. `stack` is copied, not consumed - the caller
    // still owns clearing/decrementing whatever slot it came from.
    DroppedItem(Vector3 position, ItemStack stack,
                Vector3 launch_velocity = {0.0f, 0.0f, 0.0f},
                DroppedItemOrigin origin = DroppedItemOrigin::Natural,
                std::optional<Color> block_tint = std::nullopt);

    // One tick's worth of gravity/drag/water-buoyancy and ground collision
    // (see the .cpp), plus aging toward MAX_AGE. Called from
    // GameEngine::tick(), never per-frame.
    void tick_physics(const World* world);

    // A continuous (not tick-stepped) pull toward `target` while within
    // MAGNET_RADIUS of it - separate from tick_physics() so the visible
    // pull tracks a smoothly-moving player at full frame rate instead of
    // updating in visible 50ms steps. Called from GameEngine::update()
    // every frame while the item hasn't been picked up yet.
    void update_magnet_pull(float delta_time, Vector3 target);

    // Named render(), not draw() - GameObject already declares a 0-arg
    // virtual draw(), and DroppedItem is only ever used through a concrete
    // pointer (GameEngine's own dropped_items list), never polymorphically
    // through a GameObject*, so this is deliberately a distinct method
    // rather than an override with a mismatched signature. Blocks render as
    // small 3D cubes using terrain.png. Non-block items - and blocks with an
    // items.json "block_items" sprite (torches, sapling, doors, bed) - use
    // one flat icon from the item atlas; that billboard faces
    // `viewer_position` only around the vertical axis, so looking up/down
    // never tilts its top or bottom edge.
    void render(float tick_alpha, Vector3 viewer_position, const World& world) const;

    const ItemStack& get_stack() const { return stack; }
    bool can_pick_up() const { return age >= pickup_delay; }
    bool is_active() const { return active; }
    void set_active(bool value) { active = value; }
    Vector3 get_position() const { return motion.current; }
    float get_age() const { return age; }
    std::optional<Color> get_block_tint() const { return block_tint; }

    // Restores a saved despawn countdown (WorldSave::load_dropped_items())
    // instead of starting fresh at 0 - a reloaded item keeps counting down
    // from wherever it was when the world was last saved, rather than
    // getting another full MAX_AGE on every relog. Safe to call any time;
    // pickup_delay is a separate, much smaller window that's already long
    // past for any item old enough to have been worth persisting at all.
    void set_age(float value) { age = value; }

    // Same kind of stack (both blocks of the same BlockType, never
    // tools - a tool carries its own durability, so two tools never merge
    // even if they're the same kind), both old enough to no longer be
    // freshly launched (avoids merging two items still popping apart from
    // the same break), within MERGE_RADIUS of each other, and the
    // combined count still fits in one stack - folds `other` into this
    // stack and deactivates it. Returns whether a merge happened. See
    // GameEngine.cpp for the O(n^2) pass that calls this once per tick -
    // the actual "stacking reduces entity count" optimization the merge
    // exists for.
    bool try_merge(DroppedItem& other);

private:
    static constexpr float NATURAL_PICKUP_DELAY = 0.25f;
    static constexpr float PLAYER_THROWN_PICKUP_DELAY = 2.0f;
    static constexpr float MAX_AGE = 300.0f; // 6000 ticks - the same despawn time real Minecraft uses

    ItemStack stack;
    float age = 0.0f;
    float pickup_delay = NATURAL_PICKUP_DELAY;
    std::optional<Color> block_tint;
    TickMotion motion;
};
