#include <cstdint>
#include <ctime>
#include <memory>

#include "core/GameEngine.hpp"
#include "World.hpp"

namespace {
    constexpr int WORLD_SIZE = 512; // blocks along X and Z, covering [0, WORLD_SIZE)
}

int main()
{
    GameEngine engine(1280, 720, "Mine-Clone");

    // Built after the engine so InitWindow()/Load_block_definitions() (both
    // done by GameEngine's constructor) have already run — chunk meshing
    // needs a GL context and the block texture atlas.
    engine.set_world(std::make_unique<World>(WORLD_SIZE, static_cast<uint32_t>(time(nullptr))));

    engine.run();
    return 0;
}
