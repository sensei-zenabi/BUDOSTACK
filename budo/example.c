#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

int main(void) {
    int width = EXAMPLE_WIDTH;
    int height = EXAMPLE_HEIGHT;

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
