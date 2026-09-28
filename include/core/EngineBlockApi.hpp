#pragma once

#include "world/BlockBehavior.hpp"

class GameEngine;

// The game's BlockApi: what block behaviors do goes to GameEngine's world,
// dropped items, sounds and particles (core/BlockEvents.cpp).
class EngineBlockApi : public BlockApi {
public:
    explicit EngineBlockApi(GameEngine& engine) : engine(engine) {}

    BlockType get_block(BlockPos pos) const override;
    std::optional<int> get_property(BlockPos pos, const std::string& name) const override;
    int get_light(BlockPos pos) const override;
    int get_sky_light(BlockPos pos) const override;
    int get_block_light(BlockPos pos) const override;
    bool is_block_near(BlockPos center, BlockType type, int radius, int height) const override;
    float random() override;
    uint64_t game_tick() const override;

    void set_block(BlockPos pos, BlockType type) override;
    void set_property(BlockPos pos, const std::string& name, int value) override;
    void break_block(BlockPos pos, bool drops) override;
    void drop_item(BlockPos pos, const ItemStack& stack) override;
    void schedule_tick(BlockPos pos, int delay_ticks) override;
    bool place_structure(BlockPos origin, const StructureDefinition& structure) override;
    void take_held(BlockUse& use, int count) override;

    // While a player's own click is being handled: changes show this very
    // frame instead of with the next background remesh.
    bool player_action = false;

private:
    GameEngine& engine;
    uint32_t random_state = 0x6D2B79F5u;
};
