#include <cstdlib>
#include <ctime>
#include <memory>

#include "core/GameEngine.hpp"
#include "Chunk.hpp"

int main() {
    srand(static_cast<unsigned>(time(nullptr)));

    GameEngine engine(1280, 720, "Mine-Clone");

    // 3x3 grid of chunks (the original one plus its 8 neighbors), centered on
    // the origin so the starting camera view frames the middle one. Each
    // chunk is generated and lit independently — no World yet to give one
    // chunk's edge blocks visibility into its neighbor's, so expect a visible
    // AO/lighting seam where two chunks meet.
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            Vector3 position = {
                cx * static_cast<float>(CHUNK_SIZE) - CHUNK_SIZE / 2.0f,
                0.0f,
                cz * static_cast<float>(CHUNK_SIZE) - CHUNK_SIZE / 2.0f,
            };
            auto chunk = std::make_unique<Chunk>(position);
            chunk->Randomize();
            chunk->ComputeLighting();
            engine.AddObject(std::move(chunk));
        }
    }

    engine.Run();
    return 0;
}
