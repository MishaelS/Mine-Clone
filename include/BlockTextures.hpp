#pragma once

#include "raylib.h"

#include <string>

// Loads and caches block textures by sprite file name, so block definitions
// in blocks.json can reference any file in assets/sprites/ without needing a
// new C++ enum value.
void UnloadBlockTextures();

// Loads (if not already cached) and returns the texture for a sprite file
// name such as "grass_block_top.png". Needs a GL context (call after InitWindow).
const Texture2D& GetBlockTexture(const std::string& fileName);
