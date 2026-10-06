#!/usr/bin/env python3
"""Compile the production SDL input handler against a small event fixture.

This checks text forwarding even on hosts without SDL development headers.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "apps/terminal.c").read_text()


def function(signature):
    start = source.index(signature + " {")
    end = source.index("\n}\n", start) + 3
    return source[start:end]


fixture = r'''
#include "lib/budo_gfx.h"
#include <assert.h>
#include <string.h>
#define SDL_KEYDOWN 1
#define SDL_KEYUP 2
#define SDL_TEXTINPUT 3
#define SDL_MOUSEMOTION 4
#define SDL_MOUSEBUTTONDOWN 5
#define SDL_MOUSEBUTTONUP 6
#define SDL_MOUSEWHEEL 7
#define SDL_WINDOWEVENT 8
#define SDL_QUIT 9
#define SDL_WINDOWEVENT_FOCUS_LOST 1
typedef union SDL_Event {
    unsigned int type;
    struct { unsigned int type; struct { int sym, scancode; } keysym; int repeat; } key;
    struct { unsigned int type; char text[32]; } text;
    struct { unsigned int type; int x, y; } motion;
    struct { unsigned int type; int button, x, y; } button;
    struct { unsigned int type; int x, y; } wheel;
    struct { unsigned int type; int event; } window;
} SDL_Event;
static void *terminal_window_handle;
static struct budo_gfx_event received[32];
static int count;
void budo_gfx_host_event(struct budo_gfx_host *host, const struct budo_gfx_event *event)
{
    (void)host;
    assert(count < 32);
    received[count++] = *event;
}
const uint8_t *budo_gfx_host_pixels(struct budo_gfx_host *host, int *w, int *h, int *dirty)
{
    (void)host;
    (void)dirty;
    *w = 640;
    *h = 480;
    return NULL;
}
static void SDL_GetWindowSize(void *window, int *w, int *h)
{
    (void)window;
    *w = 640;
    *h = 480;
}
static void SDL_GL_GetDrawableSize(void *window, int *w, int *h)
{
    SDL_GetWindowSize(window, w, h);
}
static void terminal_gfx_display_rect(int dw, int dh, int *x, int *y, int *w, int *h)
{
    *x = *y = 0;
    *w = dw;
    *h = dh;
}
'''
fixture += function("static int terminal_utf8_next(const uint8_t *data, size_t length, size_t *offset, uint32_t *out_codepoint)")
fixture += "\n" + function("static void terminal_gfx_input(struct budo_gfx_host *host, const SDL_Event *event)")
fixture += r'''
int main(void)
{
    SDL_Event event = {0};
    event.type = SDL_TEXTINPUT;
    strcpy(event.text.text, "\303\244\303\266\303\245\303\204\303\226\303\205");
    terminal_gfx_input(NULL, &event);
    const int expected[] = {0xe4, 0xf6, 0xe5, 0xc4, 0xd6, 0xc5};
    assert(count == 6);
    for (int i = 0; i < 6; ++i) {
        assert(received[i].type == BUDO_GFX_TEXT_INPUT);
        assert(received[i].key == expected[i]);
    }
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = 0xe4;
    event.key.keysym.scancode = 52;
    event.key.repeat = 1;
    terminal_gfx_input(NULL, &event);
    assert(count == 7 && received[6].type == BUDO_GFX_KEY_DOWN);
    assert(received[6].key == 0xe4 && received[6].scancode == 52 && received[6].repeat == 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / "input.c").write_text(fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-Wpedantic", "-I", str(root), str(path / "input.c"),
                    "-o", str(path / "input")], check=True)
    subprocess.run([str(path / "input")], check=True)
print("PASS: production SDL Unicode text and physical key forwarding")
