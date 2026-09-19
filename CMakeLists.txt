cmake_minimum_required(VERSION 3.16)
project(MineToo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lets editor tooling (VS Code's C/C++ extension, clangd, ...) pick up the
# real compiler flags - including target_compile_definitions below - instead
# of guessing and flagging things like ASSETS_PATH as undefined.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/raylib)

# Terrain noise generation (core/PerlinNoise). Only the library itself is
# needed - not its "Node Editor" GUI tool or its own test suite.
set(FASTNOISE2_TOOLS OFF CACHE BOOL "" FORCE)
set(FASTNOISE2_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/FastNoise2)

# raygui: single-header immediate-mode GUI widgets for raylib. Drives the
# menu screens' interactive controls (buttons, text boxes, sliders) - see
# src/ui/Widgets.cpp - instead of this project hand-rolling its own
# hit-testing/focus/drag state machine for them. Header-only: its
# implementation is compiled once, in src/ui/RayGuiImpl.cpp.
add_library(raygui INTERFACE)
target_include_directories(raygui INTERFACE external/raygui/src)

add_executable(${PROJECT_NAME}
    # Application and reusable core services
    src/main.cpp
    src/core/GameEngine.cpp
    src/core/Json.cpp
    src/core/Block.cpp
    src/core/BlockShape.cpp
    src/core/TextureManager.cpp
    src/core/PerlinNoise.cpp
    src/core/TerrainNoise.cpp
    src/core/Biome.cpp
    src/core/DayNightCycle.cpp
    src/core/Keybindings.cpp
    src/core/Settings.cpp
    src/core/WorldSave.cpp
    src/audio/AudioSystem.cpp

    # Player-domain state and world-visible effects
    src/player/Entity.cpp
    src/player/Inventory.cpp
    src/player/PlayerController.cpp
    src/player/PlayerHealth.cpp
    src/player/Item.cpp
    src/player/DroppedItem.cpp
    src/player/DropTable.cpp
    src/player/Recipe.cpp
    src/player/Smelting.cpp
    src/effects/ParticleSystem.cpp

    # World, generation, and rendering
    src/world/Chunk.cpp
    src/world/ChunkWorkerPool.cpp
    src/world/World.cpp
    src/rendering/Skybox.cpp
    src/rendering/BlockMesh.cpp
    src/rendering/PlayerRenderer.cpp
    src/worldgen/Structure.cpp
    src/worldgen/StructureGenerator.cpp

    # UI: reusable widgets and independent screens
    src/ui/RayGuiImpl.cpp
    src/ui/Widgets.cpp
    src/ui/Localization.cpp
    src/ui/FontManager.cpp
    src/ui/DebugOverlay.cpp
    src/ui/MainMenuScreen.cpp
    src/ui/WorldListScreen.cpp
    src/ui/WorldCreateScreen.cpp
    src/ui/SettingsScreen.cpp
    src/ui/PauseMenuScreen.cpp
    src/ui/InventoryHud.cpp
    src/ui/ChatHud.cpp
    src/ui/LoadingScreen.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE include)

# First use of <thread>/<mutex>/<shared_mutex>/<condition_variable> in this
# project (ChunkWorkerPool, background chunk generation/meshing) - needs an
# actual thread library linked in, unlike raylib/FastNoise2 which don't
# require this on their own.
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib FastNoise Threads::Threads raygui)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SAVE_DATA_PATH="${CMAKE_SOURCE_DIR}/"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    # raymath.h intentionally uses C-style `{ 0 }` aggregate initialization;
    # suppress that third-party-only noise while keeping project warnings on.
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
