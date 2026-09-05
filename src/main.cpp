#include <cstdint>
#include <ctime>
#include <memory>

#include "core/GameEngine.hpp"
#include "World.hpp"

int main()
{
    GameEngine engine(1280, 720, "Mine-Clone");

    // Built after the engine so InitWindow()/Load_block_definitions() (both
    // done by GameEngine's constructor) have already run — chunk meshing
    // needs a GL context and the block texture atlas. No world size to pass
    // any more — World has none, only its (very large but finite, see
    // World.cpp's WORLD_BORDER_BLOCKS) border, and chunks stream in/out
    // around wherever the camera actually goes.
    engine.set_world(std::make_unique<World>(static_cast<uint32_t>(time(nullptr))));

    engine.run();
    return 0;
}
