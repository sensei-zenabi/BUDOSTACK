#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="$SCRIPT_DIR"

SDL_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null || true)
SDL_LIBS=$(pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || true)
GL_LIBS=$(pkg-config --libs gl 2>/dev/null || echo "-lGL")

if [[ -z "$SDL_LIBS" ]]; then
    echo "SDL2 development files not found." >&2
    exit 1
fi

cd "$BUILD_DIR"

build_demo() {
    local source="$1"
    local output="$2"

    cc -std=c11 -Wall -Wextra -Werror -Wpedantic -DGL_GLEXT_PROTOTYPES \
        $SDL_CFLAGS \
        -I"$SCRIPT_DIR" \
        budo_graphics.c \
        budo_shader_stack.c \
        "$source" \
        -o "$output" \
        $SDL_LIBS $GL_LIBS -lm

    echo "Built $BUILD_DIR/$output"
}

build_demo example.c example
build_demo rocket.c rocket
