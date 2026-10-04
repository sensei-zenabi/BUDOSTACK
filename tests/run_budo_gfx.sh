#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT_DIR=$PWD
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/budogfx-tests-XXXXXX")
trap 'rm -rf "$TEST_DIR"' EXIT
COMPILER=${CC:-cc}
FLAGS='-std=c11 -Wall -Wextra -Werror -Wpedantic'
$COMPILER $FLAGS tests/budo_gfx.c -o "$TEST_DIR/transport"
"$TEST_DIR/transport"

SDL_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null || true)
SDL_LIBS=$(pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || true)
GL_LIBS=${GL_LIBS:-$(pkg-config --libs gl 2>/dev/null || echo '-lGL')}
if [[ -z "$SDL_LIBS" ]]; then
    echo 'Skipping OpenGL/demo checks: SDL2 development files not found.'
    exit 0
fi
make all
LIB_OBJECTS=()
for object in lib/*.o; do
    if [[ "$object" != 'lib/budo_gfx.o' ]]; then
        LIB_OBJECTS+=("$object")
    fi
done
$COMPILER $FLAGS -shared -fPIC tests/gfx_socket_preload.c -o "$TEST_DIR/socket.so"
$COMPILER $FLAGS -DBUDO_WIDTH="${BUDO_WIDTH:-320}" $SDL_CFLAGS tests/terminal_gfx.c budo/lib/budo_screen.c \
    "${LIB_OBJECTS[@]}" $SDL_LIBS $GL_LIBS -lm -pthread -o "$TEST_DIR/display"
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-offscreen} SDL_AUDIODRIVER=dummy \
    "$TEST_DIR/display" "$TEST_DIR/socket.so" "$ROOT_DIR"
