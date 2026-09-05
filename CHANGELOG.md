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
- Perlin-noise terrain generation (`core/PerlinNoise`) covering a 512x512-block world (32x32 chunks).
- `FontManager`, a shared font cache (mirrors `TextureManager`) — every in-game text now draws with `assets/fonts/Minecraft Rus/minecraft.ttf` (loaded with Cyrillic glyphs, for Russian text) instead of raylib's built-in default font.
- Per-face tint color in `blocks.json` (`BlockProperties::texture_tints`): a face can multiply a color into its tile, for art that's deliberately colorless and meant to be recolored in code (e.g. grass top).
- Each chunk now builds into a single cached GPU mesh (`Chunk::build_mesh`) instead of per-face immediate-mode drawing, with faces hidden by an opaque neighbor culled out — including across chunk borders.
- `World` class (`World.hpp`/`.cpp`): owns the chunk grid and everything that needs to reach across chunk borders — generation order, camera raycasting, and block edits.
- Block breaking: left-click removes the block the camera is aimed at, up to 10 blocks away (voxel DDA raycast in `World::raycast`), relighting and remeshing the edited chunk and its neighbors.
- Minecraft-style crosshair: a "+" at screen center, rendered with an inverted-color blend so it stays visible over any background.
- Debug overlay (F3, Minecraft-style): FPS, position, block/chunk coordinates, facing, light level, fly speed, and the block currently targeted.
- `get_block_name()` (`core/Block`): a block's `blocks.json` name, for the debug overlay's "Looking at" line.
- Mouse wheel adjusts the free-look camera's fly speed (2-100 blocks/second), Minecraft creative/spectator-style.
- Block placing: right-click places a block against the targeted face, up to 15 blocks away (`World::place_block`); no inventory yet, so it always places Cobblestone.
- Block-selection outline: a black wireframe box around whatever block the crosshair is aimed at, out to the block-placing distance (15 blocks).
- Fixed-rate game tick (`GameEngine::tick`, 20/second, Minecraft's own tick rate) via an accumulator in `GameEngine::run`, decoupled from the render frame rate and capped against a lag-spike "spiral of death"; the running tick count shows on the debug overlay. Nothing simulates on it yet — it's the clock future world systems (day/night, scheduled block updates, random ticks) will run on.
- Dynamic chunk loading: `ChunkState` (`Unloaded`/`Loaded`/`Active`, `Chunk.hpp`) plus `World::update_chunk_states`, called every tick with the camera's position — chunks within a configurable `LOADED_RADIUS` are generated/meshed on demand and drawn, chunks within the smaller `ACTIVE_RADIUS` inside that are additionally flagged as ticking (no simulation reads the flag yet — it's the extension point for future per-chunk gameplay), and anything that falls out of `LOADED_RADIUS` is unloaded (mesh and block data freed; a modified chunk's edits aren't persisted yet — `World::unload_chunk` has a TODO). `World` no longer generates its whole grid up front, so a chunk's state now genuinely starts at `Unloaded`. Transition logic (`desired_state_for`, `load_chunk`, `unload_chunk`) is kept separate from the pure world/chunk data it acts on, as a seam for a future client/server split.
- `Water` block (`assets/blocks.json`, non-solid, transparent), filling every column from its terrain height up to `WATER_LEVEL` (block 64, `Chunk.cpp`) — a sea level, same as Minecraft's own.
- Debug overlay's "Chunk:" line now also shows position local to the chunk (not just which chunk), matching Minecraft's own F3 "local x/y/z in chunk x/z" format.

### Changed
- Renamed class methods project-wide to snake_case; moved `Json`, `GameObject`, `IsoCamera`, and `Block` into `core/`.
- Block textures now index a fixed 16x16 grid of tiles in `assets/sprites/terrain.png` by `{x, y}` coordinates per face (`blocks.json`'s `top`/`bottom`/`side` are now `{x, y}` objects, with an optional `color` tint, instead of sprite filenames), rather than dynamically packing individual sprite files into an atlas at startup; removed the now-unused `core/TextureAtlas`.
- `core/PerlinNoise` is now backed by FastNoise2 (`external/FastNoise2`, a new git submodule) instead of a hand-rolled implementation — same public API (`PerlinNoise(seed)`, `.noise()`/`.fractal()`), SIMD-accelerated noise underneath, with native Apple Silicon (NEON) support.
- Chunks are now `CHUNK_SIZE` (16) wide/deep but `CHUNK_HEIGHT` (256) tall — a single chunk per (x, z) column spans the whole world height (still no vertical chunk *stacking* — that's a different thing) instead of being a 16x16x16 cube. Every bound check and loop that used to reuse `CHUNK_SIZE` for the Y axis now uses `CHUNK_HEIGHT`. Terrain generation's `BASE_HEIGHT`/`HEIGHT_VARIATION` (`Chunk.cpp`) were raised to fit this taller world and sit mostly above the new water level, and the starting camera position/angle (`GameEngine.cpp`) were retuned to clear the now much taller (and steeper) hills.

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
