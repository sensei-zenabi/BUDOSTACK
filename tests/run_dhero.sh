#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT_DIR=$PWD
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/dhero-tests-XXXXXX")
trap 'rm -rf "$TEST_DIR"' EXIT
./budo/build.sh
cc -std=c11 -Wall -Wextra -Werror -Wpedantic tests/dhero.c budo/DHERO/data.c budo/DHERO/pcx.c -o "$TEST_DIR/dhero"
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -shared -fPIC \
    tests/gfx_socket_preload.c -o "$TEST_DIR/socket.so"
BUDO_GFX_TEST_SOCKETPAIR=1 "$TEST_DIR/dhero" "$ROOT_DIR/budo/dhero" "$TEST_DIR/socket.so"
