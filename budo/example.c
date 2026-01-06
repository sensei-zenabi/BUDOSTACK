#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define EXAMPLE_WIDTH 640
#define EXAMPLE_HEIGHT 360
#define EXAMPLE_LAYER 1
#define EXAMPLE_DURATION_SECONDS 5

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

static void resolve_terminal_size(int *out_width, int *out_height) {
    if (!out_width || !out_height) {
        return;
    }

    int width = EXAMPLE_WIDTH;
    int height = EXAMPLE_HEIGHT;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            width = ws.ws_xpixel;
            height = ws.ws_ypixel;
        } else if (ws.ws_col > 0 && ws.ws_row > 0) {
            width = (int)ws.ws_col * 8;
            height = (int)ws.ws_row * 8;
        }
    }

    if (width <= 0) {
        width = EXAMPLE_WIDTH;
    }
    if (height <= 0) {
        height = EXAMPLE_HEIGHT;
    }

    *out_width = width;
    *out_height = height;
}

int main(void) {
    int width = EXAMPLE_WIDTH;
    int height = EXAMPLE_HEIGHT;

    resolve_terminal_size(&width, &height);
    setvbuf(stdout, NULL, _IONBF, 0);

    budo_graphics_init(width, height, 0, 0, 0);

    uint64_t start_ms = monotonic_ms();
    uint64_t now_ms = start_ms;
    uint32_t frame = 0u;

    while (now_ms - start_ms < (uint64_t)EXAMPLE_DURATION_SECONDS * 1000u) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int r = (x + (int)frame) & 0xFF;
                int g = (y + (int)(frame * 2u)) & 0xFF;
                int b = (x + y + (int)(frame * 3u)) & 0xFF;
                budo_graphics_pixel(x, y, r, g, b, EXAMPLE_LAYER);
            }
        }
        budo_graphics_render(EXAMPLE_LAYER);
        frame++;
        now_ms = monotonic_ms();
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            budo_graphics_clean(x, y, EXAMPLE_LAYER);
        }
    }
    budo_graphics_render(EXAMPLE_LAYER);

    return 0;
}
