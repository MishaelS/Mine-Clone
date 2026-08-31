cmake_minimum_required(VERSION 3.16)
project(Mine-Clone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/raylib)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/GameObject.cpp
    src/WorldObject.cpp
    src/Entity.cpp
    src/Player.cpp
    src/Block.cpp
    src/BlockTextures.cpp
    src/Chunk.cpp
    src/Json.cpp
    src/IsoCamera.cpp
    src/Skybox.cpp
    src/core/GameEngine.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
target_compile_definitions(${PROJECT_NAME} PRIVATE ASSETS_PATH="${CMAKE_SOURCE_DIR}/assets/")

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
