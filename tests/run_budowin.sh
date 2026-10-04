#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT_DIR=$PWD
TEST_DIR=$(mktemp -d "$ROOT_DIR/.budowin-test-XXXXXX")
TEST_EXE="$ROOT_DIR/budo/budowin-test"
trap 'rm -rf "$TEST_DIR"; rm -f "$TEST_EXE"' EXIT
export TMPDIR="$TEST_DIR"
export HOME="$TEST_DIR"
export BUDOWIN_TEST_DIR="$TEST_DIR/work"
unset BUDOSTACK_BASE
mkdir -p "$BUDOWIN_TEST_DIR/folder"
printf 'input' > "$BUDOWIN_TEST_DIR/input.txt"
COMPILER=${CC:-cc}
FLAGS=(-std=c11 -Wall -Wextra -Werror -Wpedantic)
budo/BUDOWIN/build.sh
"$COMPILER" "${FLAGS[@]}" tests/budowin_native.c budo/BUDOWIN/src/platform.c lib/budo_gfx.c -ldl -lm -o "$TEST_EXE"
"$TEST_EXE" "$ROOT_DIR/budo/BUDOWIN"
"$COMPILER" "${FLAGS[@]}" -shared -fPIC tests/gfx_socket_preload.c -o "$TEST_DIR/socket.so"
"$COMPILER" "${FLAGS[@]}" tests/budowin_graphics.c -o "$TEST_DIR/graphics"
"$TEST_DIR/graphics" "$ROOT_DIR/budo/budowin" "$TEST_DIR/socket.so" "$TEST_DIR/desktop.ppm"

if [[ -x apps/runtask ]]; then python3 tests/budowin_boot.py; fi
