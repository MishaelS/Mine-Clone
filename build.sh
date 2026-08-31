#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BUILD_TYPE="Release"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

usage() {
    echo "Usage: $0 [build|clean|rebuild|run] [Debug|Release]"
    echo "  build    - configure (if needed) and compile"
    echo "  clean    - remove the build directory"
    echo "  rebuild  - clean, then build"
    echo "  run      - build, then launch the game"
}

do_build() {
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BUILD_DIR" -j"$JOBS"
}

do_clean() {
    rm -rf "$BUILD_DIR"
}

CMD="${1:-build}"
[ -n "$2" ] && BUILD_TYPE="$2"

case "$CMD" in
    build)
        do_build
        ;;
    clean)
        do_clean
        ;;
    rebuild)
        do_clean
        do_build
        ;;
    run)
        do_build
        "$BUILD_DIR/Mine-Clone"
        ;;
    *)
        usage
        exit 1
        ;;
esac
