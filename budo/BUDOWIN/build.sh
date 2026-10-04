#!/usr/bin/env bash
set -euo pipefail
BUDOWIN_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$BUDOWIN_DIR"
COMPILER=${CC:-cc}
FLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic)
mkdir -p APPS
for application in explorer editor terminal settings paint; do
    "$COMPILER" "${FLAGS[@]}" -fPIC -shared "appsrc/$application.c" -o "APPS/${application^^}.BWA"
done
"$COMPILER" "${FLAGS[@]}" src/main.c src/platform.c ../../lib/budo_gfx.c -ldl -lm -o ../budowin
printf 'Built BUDOWIN and five native applications.\n'
