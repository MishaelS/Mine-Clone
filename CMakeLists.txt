cmake_minimum_required(VERSION 3.16)
project(Mine-Clone CXX)

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

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/WorldObject.cpp
    src/Entity.cpp
    src/Player.cpp
    src/Chunk.cpp
    src/World.cpp
    src/DebugOverlay.cpp
    src/Skybox.cpp
    src/core/GameObject.cpp
    src/core/GameEngine.cpp
    src/core/Json.cpp
    src/core/IsoCamera.cpp
    src/core/Block.cpp
    src/core/TextureManager.cpp
    src/core/FontManager.cpp
    src/core/PerlinNoise.cpp
    src/core/TerrainNoise.cpp
    src/core/Biome.cpp
    src/core/Keybindings.cpp
    src/core/Settings.cpp
    src/core/WorldSave.cpp
    src/core/Inventory.cpp
    src/ui/Widgets.cpp
    src/ui/MainMenuScreen.cpp
    src/ui/WorldListScreen.cpp
    src/ui/WorldCreateScreen.cpp
    src/ui/SettingsScreen.cpp
    src/ui/PauseMenuScreen.cpp
    src/ui/InventoryHud.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib FastNoise)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SAVE_DATA_PATH="${CMAKE_SOURCE_DIR}/"
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
