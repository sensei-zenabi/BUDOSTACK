#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="$SCRIPT_DIR"
BUDO_WIDTH=${BUDO_WIDTH:-320}
case "$BUDO_WIDTH" in
    320|640) ;;
    *) echo "BUDO_WIDTH must be 320 or 640." >&2; exit 1 ;;
esac

SDL_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null || true)
SDL_LIBS=$(pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || true)
SDL_IMAGE_CFLAGS=$(pkg-config --cflags SDL2_image 2>/dev/null || true)
SDL_IMAGE_LIBS=$(pkg-config --libs SDL2_image 2>/dev/null || true)
SDL_MIXER_CFLAGS=$(pkg-config --cflags SDL2_mixer 2>/dev/null || true)
SDL_MIXER_LIBS=$(pkg-config --libs SDL2_mixer 2>/dev/null || true)

if [[ -n "$SDL_IMAGE_LIBS" ]]; then
    SDL_IMAGE_DEFINE="-DBUDO_USE_SDL_IMAGE=1"
else
    SDL_IMAGE_DEFINE="-DBUDO_USE_SDL_IMAGE=0"
fi

if [[ -n "$SDL_MIXER_LIBS" ]]; then
    SDL_MIXER_DEFINE="-DBUDO_USE_SDL_MIXER=1"
else
    SDL_MIXER_DEFINE="-DBUDO_USE_SDL_MIXER=0"
fi

"$SCRIPT_DIR/budowin/build.sh"

if [[ -z "$SDL_LIBS" ]]; then
    echo "Skipping BUDO SDL demos: SDL2 development files not found."
    exit 0
fi

cd "$BUILD_DIR"

build_demo() {
    local source="$1"
    local output="$2"

    cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -DBUDO_WIDTH="$BUDO_WIDTH" \
        $SDL_IMAGE_DEFINE \
        $SDL_MIXER_DEFINE \
        $SDL_CFLAGS $SDL_IMAGE_CFLAGS $SDL_MIXER_CFLAGS \
        -I"$SCRIPT_DIR" -I"$SCRIPT_DIR/lib" \
        lib/budo_graphics.c \
        lib/budo_audio.c \
        lib/budo_screen.c \
        ../lib/budo_gfx.c \
        "$source" \
        -o "$output" \
        $SDL_LIBS $SDL_IMAGE_LIBS $SDL_MIXER_LIBS -lm

    echo "Built $BUILD_DIR/$output"
}

build_demo example.c example
build_demo rocket.c rocket
