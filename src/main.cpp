#include "core/GameEngine.hpp"

int main()
{
    // World construction now happens from the startup menu flow (see
    // GameEngine::start_singleplayer_world()) instead of here - main() just
    // creates and runs the engine.
    GameEngine engine(1280, 720, "MineToo");
    engine.run();
    return 0;
}
