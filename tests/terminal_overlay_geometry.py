#!/usr/bin/env python3
"""Check production geometry without SDL/OpenGL dependencies."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "apps/terminal.c").read_text()
start = source.index("static void terminal_layout_size(")
end = source.index("static int terminal_window_point_to_framebuffer", start)
program = r"""
#include <assert.h>
#include <limits.h>
#include <math.h>
static int terminal_overlay_enabled = 1;
static int terminal_overlay_width = 1920, terminal_overlay_height = 1080;
static double terminal_layout_aspect = 1.25;
static double terminal_display_width = 65.2, terminal_display_height = 61.2;
static double terminal_offset_x = 0, terminal_offset_y = 0;
static double terminal_overlay_zoom = 100;
static double terminal_overlay_offset_x = 0, terminal_overlay_offset_y = 0;
""" + source[start:end] + r"""
int main(void) {
    int x, y, w, h;
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 0 && y == 0 && w == 1920 && h == 1080);
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == -270 && y == 0 && w == 1820 && h == 1024);
    terminal_display_rect(1920, 1080, &x, &y, &w, &h);
    assert(w == 880 && h == 661);
    terminal_display_rect(1280, 1024, &x, &y, &w, &h);
    assert(w == 835 && h == 627);
    terminal_overlay_zoom = 125;
    terminal_overlay_offset_x = -2.5;
    terminal_overlay_offset_y = 5;
    for (int mode = 0; mode < 3; mode++) {
        terminal_layout_aspect = mode == 0 ? 0 : mode == 1 ? 1.25 : 16.0 / 9;
        terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
        assert(x == -267 && y == -81 && w == 2400 && h == 1350);
        terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
        assert(x == -524 && y == -77 && w == 2276 && h == 1280);
    }
    terminal_overlay_width = terminal_overlay_height = 1000;
    terminal_overlay_zoom = 100;
    terminal_overlay_offset_x = terminal_overlay_offset_y = 0;
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 420 && y == 0 && w == 1080 && h == 1080);
    terminal_overlay_width = 600;
    terminal_overlay_height = 1200;
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 384 && y == 0 && w == 512 && h == 1024);
    terminal_overlay_enabled = 0;
    terminal_display_width = terminal_display_height = 0;
    terminal_display_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 0 && y == 0 && w == 1920 && h == 1080);
    return 0;
}
"""
with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR", root)) as temp:
    source_path = Path(temp) / "geometry.c"
    binary_path = Path(temp) / "geometry"
    source_path.write_text(program)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-Wpedantic", str(source_path), "-lm", "-o",
                    str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True)
print("Native overlay geometry and terminal layout checks passed.")
