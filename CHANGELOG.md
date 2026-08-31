# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `assets/` folder (`sprites`, `sounds`, `fonts`) for game resources.
- `CREDITS.md` with attribution for third-party art assets.
- `GameObject` base class with `WorldObject` (walls, floor, doors, windows) and `Entity` (players, mobs, items) subclasses.
- Moved `.hpp` headers into `include/`, kept `.cpp` files in `src/`.
- `core/` folders (`src/core`, `include/core`) reserved for engine-level system code (parsing, resource loading, etc.).
- `GameEngine` class (`core/`): owns the window, the main loop, and the list of `GameObject`s (update + draw each frame). `main.cpp` now just creates and runs it.

## [0.0.1] - 2026-08-30

### Added
- Project scaffolding with a CMake build system.
- raylib added as a git submodule (`external/raylib`).
- Initial empty window (`src/main.cpp`).
- `build.sh` helper script (`build`, `clean`, `rebuild`, `run`).
