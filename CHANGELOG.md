# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `assets/` folder (`sprites`, `sounds`, `fonts`) for game resources, and `CREDITS.md` with attribution for third-party art assets.
- `GameObject` base class with `WorldObject` (walls, floor, doors, windows) and `Entity` (players, mobs, items) subclasses.
- `core/` folders (`src/core`, `include/core`) for engine-level system code; `GameEngine` (`core/`) owns the window, the main loop, and the `GameObject` list, so `main.cpp` just creates and runs it.
- Voxel engine core: `Block`/`Chunk` data model with a JSON-driven block registry (`assets/blocks.json`), per-face chunk rendering with Minecraft-style vertex AO, two-channel (sky + block) BFS light propagation, a free-look debug camera, and a gradient skybox.
- `TextureManager`, a shared texture cache used by any system that loads textures (replaces the block-only `BlockTextures`).
- Perlin-noise terrain generation (`core/PerlinNoise`) covering a 512x512-block world (32x32 chunks), and a `core/TextureAtlas` so a whole chunk mesh draws with one bound texture.
- Each chunk now builds into a single cached GPU mesh (`Chunk::build_mesh`) instead of per-face immediate-mode drawing, with faces hidden by an opaque neighbor culled out — including across chunk borders.
- `World` class (`World.hpp`/`.cpp`): owns the chunk grid and everything that needs to reach across chunk borders — generation order, camera raycasting, and block edits.
- Block breaking: left-click removes the block the camera is aimed at, up to 10 blocks away (voxel DDA raycast in `World::raycast`), relighting and remeshing the edited chunk and its neighbors.
- Minecraft-style crosshair: a "+" at screen center, rendered with an inverted-color blend so it stays visible over any background.
- Debug overlay (F3, Minecraft-style): FPS, position, block/chunk coordinates, facing, light level, fly speed, and the block currently targeted.
- `get_block_name()` (`core/Block`): a block's `blocks.json` name, for the debug overlay's "Looking at" line.
- Mouse wheel adjusts the free-look camera's fly speed (2-100 blocks/second), Minecraft creative/spectator-style.
- Block placing: right-click places a block against the targeted face, up to 15 blocks away (`World::place_block`); no inventory yet, so it always places Cobblestone.
- Block-selection outline: a black wireframe box around whatever block the crosshair is aimed at, out to the block-placing distance (15 blocks).

### Changed
- Renamed class methods project-wide to snake_case; moved `Json`, `GameObject`, `IsoCamera`, and `Block` into `core/`.

### Fixed
- Startup crash on block names in `blocks.json` with no matching `BlockType` entry yet (new ore/wood types added without updating the enum).
- AO and smooth lighting at chunk borders: `Chunk::build_mesh` now samples real neighbor-chunk block/light data (including diagonal neighbors, for corner vertices) instead of chunk-edge defaults, removing the seam that was visible along chunk boundaries.
- Skybox not rendering: flipping depth-test/backface-culling state right after drawing it didn't flush the pending render batch first, so its quads were rasterized later with the wrong GL state and culled away.

## [0.0.1] - 2026-08-30

### Added
- Project scaffolding with a CMake build system.
- raylib added as a git submodule (`external/raylib`).
- Initial empty window (`src/main.cpp`).
- `build.sh` helper script (`build`, `clean`, `rebuild`, `run`).
