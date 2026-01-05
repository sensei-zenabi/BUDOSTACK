#define _POSIX_C_SOURCE 200809L

#include "budo_video.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *budo_pixels = NULL;
static int budo_width = 0;
static int budo_height = 0;

static void budo_video_set_mode(budo_video_mode_t mode) {
    if (mode == BUDO_VIDEO_HIGH) {
        budo_width = 320;
        budo_height = 200;
    } else {
        budo_width = 160;
        budo_height = 100;
    }
}

static void budo_video_reset_colors(void) {
    printf("\033[0m");
}

static void budo_video_set_color(uint32_t fg, uint32_t bg) {
    unsigned int fr = (fg >> 16) & 0xffu;
    unsigned int fg_g = (fg >> 8) & 0xffu;
    unsigned int fb = fg & 0xffu;
    unsigned int br = (bg >> 16) & 0xffu;
    unsigned int bg_g = (bg >> 8) & 0xffu;
    unsigned int bb = bg & 0xffu;

    printf("\033[38;2;%u;%u;%u;48;2;%u;%u;%um", fr, fg_g, fb, br, bg_g, bb);
}

int budo_video_init(budo_video_mode_t mode, const char *title, int scale) {
    size_t pixel_count;

    (void)title;
    (void)scale;

    budo_video_set_mode(mode);
    pixel_count = (size_t)budo_width * (size_t)budo_height;
    budo_pixels = calloc(pixel_count, sizeof(uint32_t));
    if (!budo_pixels) {
        fprintf(stderr, "budo_video_init: out of memory.\n");
        return -1;
    }

    printf("\033[?25l\033[2J\033[H");
    fflush(stdout);
    return 0;
}

void budo_video_shutdown(void) {
    free(budo_pixels);
    budo_pixels = NULL;
    budo_width = 0;
    budo_height = 0;
    budo_video_reset_colors();
    printf("\033[?25h");
    fflush(stdout);
}

void budo_video_clear(uint32_t color) {
    if (!budo_pixels) {
        return;
    }
    for (int i = 0; i < budo_width * budo_height; i++) {
        budo_pixels[i] = color;
    }
}

void budo_video_put_pixel(int x, int y, uint32_t color) {
    if (!budo_pixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= budo_width || y >= budo_height) {
        return;
    }
    budo_pixels[y * budo_width + x] = color;
}

void budo_video_draw_pixels(int x, int y, const uint32_t *pixels, int width, int height, int pitch) {
    if (!budo_pixels || !pixels || width <= 0 || height <= 0) {
        return;
    }
    if (pitch <= 0) {
        pitch = width;
    }

    for (int row = 0; row < height; row++) {
        int dest_y = y + row;
        if (dest_y < 0 || dest_y >= budo_height) {
            continue;
        }
        for (int col = 0; col < width; col++) {
            int dest_x = x + col;
            if (dest_x < 0 || dest_x >= budo_width) {
                continue;
            }
            budo_pixels[dest_y * budo_width + dest_x] = pixels[row * pitch + col];
        }
    }
}

void budo_video_present(void) {
    uint32_t last_fg = 0;
    uint32_t last_bg = 0;
    int has_color = 0;

    if (!budo_pixels) {
        return;
    }

    printf("\033[H");

    for (int y = 0; y < budo_height; y += 2) {
        for (int x = 0; x < budo_width; x++) {
            uint32_t fg = budo_pixels[y * budo_width + x];
            uint32_t bg = 0x000000u;
            if (y + 1 < budo_height) {
                bg = budo_pixels[(y + 1) * budo_width + x];
            }
            if (!has_color || fg != last_fg || bg != last_bg) {
                budo_video_set_color(fg, bg);
                last_fg = fg;
                last_bg = bg;
                has_color = 1;
            }
            fputs("▀", stdout);
        }
        budo_video_reset_colors();
        has_color = 0;
        if (y + 2 < budo_height) {
            putchar('\n');
        }
    }

    fflush(stdout);
}

int budo_video_get_size(int *width, int *height) {
    if (!budo_pixels) {
        return -1;
    }
    if (width) {
        *width = budo_width;
    }
    if (height) {
        *height = budo_height;
    }
    return 0;
}
