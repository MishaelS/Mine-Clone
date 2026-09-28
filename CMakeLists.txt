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

# Lua 5.4: block behaviors written as scripts (assets/scripts - see
# include/scripting/LuaScripting.hpp). Built from its sources as C++, not C,
# so a script error unwinds as a C++ exception - destructors of the game's
# code it passes through still run (Lua's C build uses longjmp). Only the
# library: not its standalone interpreter/compiler (lua.c, luac.c), its
# single-file build (onelua.c) or its internal tests (ltests.c).
set(LUA_SOURCES
    lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldblib.c ldebug.c
    ldo.c ldump.c lfunc.c lgc.c linit.c liolib.c llex.c lmathlib.c lmem.c
    loadlib.c lobject.c lopcodes.c loslib.c lparser.c lstate.c lstring.c
    lstrlib.c ltable.c ltablib.c ltm.c lundump.c lutf8lib.c lvm.c lzio.c)
list(TRANSFORM LUA_SOURCES PREPEND external/lua/)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC external/lua)
if(NOT MSVC)
    # The .c files compiled as C++ on purpose (see above).
    target_compile_options(lua PRIVATE -x c++)
endif()
if(NOT WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()

add_executable(${PROJECT_NAME}
    # Application and reusable core services
    src/main.cpp
    src/app/Application.cpp
    src/core/GameEngine.cpp
    src/core/BlockEvents.cpp
    src/core/Json.cpp
    src/core/Block.cpp
    src/core/BlockShape.cpp
    src/core/BlockShapeKinds.cpp
    src/effects/BlockParticles.cpp
    src/core/TextureManager.cpp
    src/core/PerlinNoise.cpp
    src/core/TerrainNoise.cpp
    src/core/Biome.cpp
    src/core/DayNightCycle.cpp
    src/core/Keybindings.cpp
    src/core/Settings.cpp
    src/core/WorldSave.cpp
    src/audio/AudioSystem.cpp

    # Entities: dynamic actors in the world (the player, dropped items)
    src/entities/Entity.cpp
    src/entities/Player.cpp
    src/entities/PlayerHealth.cpp
    src/entities/DroppedItem.cpp
    src/entities/Cow.cpp
    src/entities/Mob.cpp
    src/entities/Npc.cpp
    src/entities/Sheep.cpp
    src/entities/Interaction.cpp
    src/entities/ai/Goal.cpp
    src/entities/ai/Pathfinder.cpp
    src/entities/ai/Controls.cpp
    src/entities/ai/CommonGoals.cpp
    src/entities/ai/MobAI.cpp
    src/entities/ai/NpcAI.cpp

    # Items: item/tool stats, inventory, drops, crafting and smelting
    src/items/Item.cpp
    src/items/Inventory.cpp
    src/items/DropTable.cpp
    src/items/Recipe.cpp
    src/items/Smelting.cpp

    # World-visible effects
    src/effects/ParticleSystem.cpp

    # Game content: every block, item, drop, recipe and smelting rule
    # (see include/content/Content.hpp)
    src/content/Blocks.cpp
    src/content/BlockFile.cpp
    src/content/StructureFile.cpp
    src/content/Items.cpp
    src/content/Drops.cpp
    src/content/Recipes.cpp
    src/content/Smelting.cpp
    src/content/Behaviors.cpp
    src/scripting/LuaScripting.cpp

    # World, generation, and rendering
    src/world/Chunk.cpp
    src/world/ChunkWorkerPool.cpp
    src/world/World.cpp
    src/world/BlockBehavior.cpp
    src/rendering/Skybox.cpp
    src/rendering/BlockMesh.cpp
    src/rendering/PlayerRenderer.cpp
    src/rendering/FirstPersonHand.cpp
    src/rendering/HeldItem.cpp
    src/rendering/BlockIcon.cpp
    src/model/EntityModel.cpp
    src/model/EntityModelRenderer.cpp
    src/model/ModelLibrary.cpp
    src/worldgen/Structure.cpp
    src/worldgen/StructureGenerator.cpp
    src/worldgen/CustomWorld.cpp

    # UI: reusable widgets and independent screens
    src/ui/RayGuiImpl.cpp
    src/ui/Widgets.cpp
    src/ui/Localization.cpp
    src/ui/FontManager.cpp
    src/ui/DebugOverlay.cpp
    src/ui/MainMenuScreen.cpp
    src/ui/WorldListScreen.cpp
    src/ui/WorldCreateScreen.cpp
    src/ui/CustomWorldScreen.cpp
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
target_link_libraries(${PROJECT_NAME} PRIVATE raylib FastNoise Threads::Threads raygui lua)
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

# Developer tool: the entity model & animation editor (tools/model_editor -
# see ModelEditor.hpp). Its own executable, never shipped inside the game;
# shares the model format/renderer (src/model) the game will draw with.
add_executable(MineTooModelEditor
    tools/model_editor/main.cpp
    tools/model_editor/ModelEditor.cpp
    tools/model_editor/EditorText.cpp
    tools/model_editor/RayGuiImpl.cpp
    tools/model_editor/BlockTab.cpp
    tools/model_editor/StructureTab.cpp
    tools/model_editor/EditorWidgets.cpp
    tools/model_editor/BlockTabBehavior.cpp
    src/rendering/BlockIcon.cpp
    src/core/BlockShapeKinds.cpp
    src/effects/BlockParticles.cpp
    src/content/BlockFile.cpp
    src/content/StructureFile.cpp
    src/model/EntityModel.cpp
    src/model/EntityModelRenderer.cpp
    src/core/Json.cpp
)
target_include_directories(MineTooModelEditor PRIVATE include tools/model_editor external/raygui/styles)
target_link_libraries(MineTooModelEditor PRIVATE raylib raygui)
target_compile_definitions(MineTooModelEditor PRIVATE
    ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/"
    SAVE_DATA_PATH="${CMAKE_SOURCE_DIR}/"
)
if(MSVC)
    target_compile_options(MineTooModelEditor PRIVATE /W4)
else()
    target_compile_options(MineTooModelEditor PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
if(APPLE)
    target_link_libraries(MineTooModelEditor PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
