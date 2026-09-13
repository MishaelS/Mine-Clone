#pragma once

// The whole game simulates at a fixed 20 ticks/second (Minecraft's own
// rate) - GameEngine::run()'s fixed-timestep accumulator loop, and every
// tick-driven system hanging off GameEngine::tick() (falling blocks,
// dropped-item physics) shares these same two constants instead of each
// re-deriving or separately hardcoding them.
constexpr int TICKS_PER_SECOND = 20;
constexpr float TICK_DURATION = 1.0f / TICKS_PER_SECOND; // seconds per tick (50ms)
