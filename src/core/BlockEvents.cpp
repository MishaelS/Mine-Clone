// The engine's side of the block API (world/BlockBehavior.hpp): the
// BlockApi behaviors act through, and GameEngine handing them their events.
#include "core/EngineBlockApi.hpp"
#include "core/GameEngine.hpp"
#include "core/DayNightCycle.hpp"
#include "items/DropTable.hpp"
#include "worldgen/Structure.hpp"

#include <algorithm>
#include <cmath>

namespace {
    // How many neighbor updates one change may set off, all its chained
    // consequences included - a runaway chain stops here instead of
    // freezing the game.
    constexpr int MAX_NEIGHBOR_UPDATES = 4096;
    // How many scheduled ticks run in one game tick at most - the rest wait
    // for the next one.
    constexpr int MAX_SCHEDULED_TICKS_PER_TICK = 1024;

    Vector3 center_of(BlockPos pos)
    {
        return {pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    }
}

// ------------------------------------------------------------ Queries --

BlockType EngineBlockApi::get_block(BlockPos pos) const
{
    return engine.world ? engine.world->get_block(pos.x, pos.y, pos.z) : BlockType::Air;
}

std::optional<int> EngineBlockApi::get_property(BlockPos pos, const std::string& name) const
{
    if (!engine.world) return std::nullopt;
    const BlockStateProperty* property = find_state_property(get_block(pos), name);
    if (!property) return std::nullopt;
    return read_state_property(*property, engine.world->get_state_values(pos.x, pos.y, pos.z));
}

int EngineBlockApi::get_light(BlockPos pos) const
{
    if (!engine.world) return 0;
    return engine.world->get_effective_light(pos.x, pos.y, pos.z, DayNightCycle::sky_light_factor(engine.game_tick));
}

int EngineBlockApi::get_sky_light(BlockPos pos) const
{
    return engine.world ? engine.world->get_sky_light(pos.x, pos.y, pos.z) : 0;
}

int EngineBlockApi::get_block_light(BlockPos pos) const
{
    return engine.world ? engine.world->get_block_light(pos.x, pos.y, pos.z) : 0;
}

bool EngineBlockApi::is_block_near(BlockPos center, BlockType type, int radius, int height) const
{
    if (!engine.world) return false;
    radius = std::clamp(radius, 0, 16);
    height = std::clamp(height, 0, 16);
    for (int dy = -height; dy <= height; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                if (engine.world->get_block(center.x + dx, center.y + dy, center.z + dz) == type) return true;
            }
        }
    }
    return false;
}

float EngineBlockApi::random()
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return static_cast<float>(random_state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

uint64_t EngineBlockApi::game_tick() const
{
    return engine.game_tick;
}

// ------------------------------------------------------------ Actions --

void EngineBlockApi::set_block(BlockPos pos, BlockType type)
{
    if (!engine.world || !engine.world->replace_block(pos.x, pos.y, pos.z, type, player_action)) return;
    for (const auto& behavior : block_behaviors::of(type)) behavior->on_placed(*this, pos);
    engine.notify_block_neighbors(pos.x, pos.y, pos.z);
}

void EngineBlockApi::set_property(BlockPos pos, const std::string& name, int value)
{
    if (!engine.world) return;
    const BlockStateProperty* property = find_state_property(get_block(pos), name);
    if (!property) return;
    const uint32_t before = engine.world->get_state_values(pos.x, pos.y, pos.z);
    const uint32_t after = write_state_property(*property, before, value);
    if (after == before) return;
    engine.world->set_state_values(pos.x, pos.y, pos.z, after);
    engine.notify_block_neighbors(pos.x, pos.y, pos.z);
}

void EngineBlockApi::break_block(BlockPos pos, bool drops)
{
    if (!engine.world) return;
    const std::optional<BlockType> broken = engine.world->break_block(pos.x, pos.y, pos.z);
    if (!broken) return;
    const Vector3 center = center_of(pos);
    engine.particles.spawn_destroy(*broken, center);
    engine.audio.play_break(*broken, center, engine.camera.position);
    if (drops) {
        for (const DropRoll& drop : resolve_block_drops(*broken, ItemStack{})) {
            ItemStack stack;
            if (drop.is_item) stack.tool = drop.item;
            else stack.block = drop.block;
            stack.count = drop.count;
            drop_item(pos, stack);
        }
    }
    for (const auto& behavior : block_behaviors::of(*broken)) behavior->on_broken(*this, pos);
    engine.check_plant_support_above(pos.x, pos.y, pos.z);
    engine.check_attachment_support_near(pos.x, pos.y, pos.z);
    engine.notify_block_neighbors(pos.x, pos.y, pos.z);
}

void EngineBlockApi::drop_item(BlockPos pos, const ItemStack& stack)
{
    if (stack.empty()) return;
    engine.dropped_items.push_back(std::make_unique<DroppedItem>(center_of(pos), stack, Vector3{0.0f, 0.02f, 0.0f},
                                                                 DroppedItemOrigin::Natural));
}

void EngineBlockApi::schedule_tick(BlockPos pos, int delay_ticks)
{
    engine.scheduled_block_ticks.push_back({engine.game_tick + static_cast<uint64_t>(std::max(1, delay_ticks)), pos.x, pos.y, pos.z,
                                            get_block(pos)});
    std::push_heap(engine.scheduled_block_ticks.begin(), engine.scheduled_block_ticks.end(),
                   [](const auto& a, const auto& b) { return a.due > b.due; });
}

bool EngineBlockApi::place_structure(BlockPos origin, const StructureDefinition& structure)
{
    if (!engine.world || structure.variants.empty()) return false;
    const Structure& variant = structure.variants[std::min(structure.variants.size() - 1,
                                                           static_cast<size_t>(random() * static_cast<float>(structure.variants.size())))];
    for (const StructureBlock& block : variant.get_blocks()) {
        if (!block.required) continue;
        const BlockPos at = origin.offset(block.x, block.y, block.z);
        // The origin's own block (a sapling) is turning into the structure.
        const BlockType existing = at == origin ? BlockType::Air : get_block(at);
        if (!structure_can_replace(block.replace_rule, existing)) return false;
    }
    // Cleared first (no drop, no particles - it's turning into the
    // structure, not being destroyed), so the block placed there finds air.
    engine.world->replace_block(origin.x, origin.y, origin.z, BlockType::Air, player_action);
    for (const StructureBlock& block : variant.get_blocks()) {
        engine.world->place_structure_block(origin.x + block.x, origin.y + block.y, origin.z + block.z, block.type, block.replace_rule);
    }
    return true;
}

void EngineBlockApi::take_held(BlockUse& use, int count)
{
    if (use.creative || count <= 0) return;
    use.held.count -= count;
    if (use.held.count <= 0) use.held.clear();
}

// ------------------------------------------------------------- Events --

void GameEngine::random_tick_block(int x, int y, int z, BlockType type)
{
    const auto& behaviors = block_behaviors::of(type);
    if (behaviors.empty()) return;
    for (const auto& behavior : behaviors) {
        behavior->on_random_tick(*block_api, {x, y, z});
        // One of them changed it - the rest were for the block that was there.
        if (world->get_block(x, y, z) != type) break;
    }
}

bool GameEngine::behaviors_allow_placement(BlockType type, int x, int y, int z)
{
    for (const auto& behavior : block_behaviors::of(type)) {
        if (!behavior->can_place(*block_api, {x, y, z})) return false;
    }
    return true;
}

void GameEngine::block_placed_by_player(int x, int y, int z)
{
    block_api->player_action = true;
    for (const auto& behavior : block_behaviors::of(world->get_block(x, y, z))) behavior->on_placed(*block_api, {x, y, z});
    notify_block_neighbors(x, y, z);
    block_api->player_action = false;
}

void GameEngine::block_broken_by_player(int x, int y, int z, BlockType broken)
{
    block_api->player_action = true;
    for (const auto& behavior : block_behaviors::of(broken)) behavior->on_broken(*block_api, {x, y, z});
    notify_block_neighbors(x, y, z);
    block_api->player_action = false;
}

bool GameEngine::use_block(const World::RaycastHit& hit, ItemStack& held)
{
    const BlockType type = world->get_block(hit.x, hit.y, hit.z);
    const auto& behaviors = block_behaviors::of(type);
    if (behaviors.empty()) return false;
    // The face clicked, from the hit's outward normal.
    BlockFace face = BlockFace::Top;
    if (hit.normal.y < -0.5f) face = BlockFace::Bottom;
    else if (hit.normal.z < -0.5f) face = BlockFace::North;
    else if (hit.normal.z > 0.5f) face = BlockFace::South;
    else if (hit.normal.x > 0.5f) face = BlockFace::East;
    else if (hit.normal.x < -0.5f) face = BlockFace::West;
    BlockUse use{held, face, current_game_mode == GameMode::Creative};
    block_api->player_action = true;
    bool used = false;
    for (const auto& behavior : behaviors) {
        if (behavior->on_use(*block_api, {hit.x, hit.y, hit.z}, use)) {
            used = true;
            break;
        }
    }
    block_api->player_action = false;
    return used;
}

void GameEngine::notify_block_neighbors(int x, int y, int z)
{
    if (!world) return;
    pending_neighbor_updates.push_back({x, y, z});
    if (updating_neighbors) return; // the loop below already running gets to it
    updating_neighbors = true;
    constexpr BlockFace FACES[6] = {BlockFace::Top, BlockFace::Bottom, BlockFace::North, BlockFace::South, BlockFace::East, BlockFace::West};
    int budget = MAX_NEIGHBOR_UPDATES;
    for (size_t next = 0; next < pending_neighbor_updates.size() && budget > 0; ++next) {
        const auto [cx, cy, cz] = pending_neighbor_updates[next];
        const BlockPos from{cx, cy, cz};
        for (BlockFace face : FACES) {
            if (--budget <= 0) break;
            const BlockPos pos = from.neighbor(face);
            const BlockType type = world->get_block(pos.x, pos.y, pos.z);
            const auto& behaviors = block_behaviors::of(type);
            if (behaviors.empty()) continue;
            const bool stays = std::all_of(behaviors.begin(), behaviors.end(),
                                           [&](const auto& behavior) { return behavior->can_stay(*block_api, pos); });
            if (!stays) {
                block_api->break_block(pos, true); // queues its own neighbors
                continue;
            }
            for (const auto& behavior : behaviors) {
                behavior->on_neighbor_changed(*block_api, pos, from);
                if (world->get_block(pos.x, pos.y, pos.z) != type) break;
            }
        }
    }
    pending_neighbor_updates.clear();
    updating_neighbors = false;
}

void GameEngine::run_scheduled_block_ticks()
{
    if (!world) {
        scheduled_block_ticks.clear();
        return;
    }
    for (int n = 0; n < MAX_SCHEDULED_TICKS_PER_TICK && !scheduled_block_ticks.empty(); ++n) {
        if (scheduled_block_ticks.front().due > game_tick) break;
        std::pop_heap(scheduled_block_ticks.begin(), scheduled_block_ticks.end(), [](const auto& a, const auto& b) { return a.due > b.due; });
        const ScheduledBlockTick due = scheduled_block_ticks.back();
        scheduled_block_ticks.pop_back();
        if (world->get_block(due.x, due.y, due.z) != due.type) continue;
        for (const auto& behavior : block_behaviors::of(due.type)) {
            behavior->on_scheduled_tick(*block_api, {due.x, due.y, due.z});
            if (world->get_block(due.x, due.y, due.z) != due.type) break;
        }
    }
}
