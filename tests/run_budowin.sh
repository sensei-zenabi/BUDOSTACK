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
"$COMPILER" "${FLAGS[@]}" tests/budowin_background.c lib/budo_gfx.c -ldl -lm -o "$TEST_DIR/background"
"$TEST_DIR/background" "$TEST_DIR/background.pcx"
"$COMPILER" "${FLAGS[@]}" tests/budowin_native.c budo/BUDOWIN/src/platform.c lib/budo_gfx.c -ldl -lm -o "$TEST_EXE"
"$TEST_EXE" "$ROOT_DIR/budo/BUDOWIN"
"$COMPILER" "${FLAGS[@]}" -shared -fPIC tests/gfx_socket_preload.c -o "$TEST_DIR/socket.so"
"$COMPILER" "${FLAGS[@]}" tests/budowin_graphics.c -o "$TEST_DIR/graphics"
"$TEST_DIR/graphics" "$ROOT_DIR/budo/budowin" "$TEST_DIR/socket.so" "$TEST_DIR/desktop.ppm"

if [[ -x apps/runtask ]]; then python3 tests/budowin_boot.py; fi

mkdir -p "$TEST_DIR/launch" "$HOME/.budowin"
"$COMPILER" "${FLAGS[@]}" tests/budowin_launch_app.c lib/budo_gfx.c -o "$TEST_DIR/launch/example"
cp "$TEST_DIR/launch/example" "$TEST_DIR/launch/rocket"
"$COMPILER" "${FLAGS[@]}" -shared -fPIC tests/budowin_launch_preload.c -o "$TEST_DIR/launch-socket.so"
"$COMPILER" "${FLAGS[@]}" tests/budowin_launch.c -o "$TEST_DIR/launch-test"
printf '%s\n0 0 1 0 0 60 60 520 340 60 60 520 340\n' "$TEST_DIR/launch" > "$HOME/.budowin/session.state"
export BUDOWIN_LAUNCH_MARKER="$TEST_DIR/launched.txt"
"$TEST_DIR/launch-test" "$ROOT_DIR/budo/budowin" "$TEST_DIR/launch-socket.so"
python3 - "$BUDOWIN_LAUNCH_MARKER" <<'PY'
import pathlib, sys
assert [pathlib.Path(p).name for p in pathlib.Path(sys.argv[1]).read_text().splitlines()] == ['example', 'rocket']
PY
