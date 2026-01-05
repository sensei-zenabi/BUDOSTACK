#define _POSIX_C_SOURCE 200809L

#include "budo_video.h"

#include "budo_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if BUDO_HAVE_SDL2
static SDL_Window *budo_window = NULL;
static SDL_Renderer *budo_renderer = NULL;
static SDL_Texture *budo_texture = NULL;
static uint32_t *budo_pixels = NULL;
static int budo_width = 0;
static int budo_height = 0;

static void budo_video_set_mode(budo_video_mode_t mode) {
    if (mode == BUDO_VIDEO_HIGH) {
        budo_width = 640;
        budo_height = 480;
    } else {
        budo_width = 320;
        budo_height = 200;
    }
}
#endif

int budo_video_init(budo_video_mode_t mode, const char *title, int scale) {
#if !BUDO_HAVE_SDL2
    (void)mode;
    (void)title;
    (void)scale;
    fprintf(stderr, "budo_video_init: SDL2 not available.\n");
    return -1;
#else
    int window_width;
    int window_height;

    if (scale <= 0) {
        scale = 2;
    }

    budo_video_set_mode(mode);
    window_width = budo_width * scale;
    window_height = budo_height * scale;

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "budo_video_init: SDL video init failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    budo_window = SDL_CreateWindow(title ? title : "BUDOSTACK BUDO",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   window_width,
                                   window_height,
                                   0);
    if (!budo_window) {
        fprintf(stderr, "budo_video_init: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    budo_renderer = SDL_CreateRenderer(budo_window, -1, SDL_RENDERER_ACCELERATED);
    if (!budo_renderer) {
        budo_renderer = SDL_CreateRenderer(budo_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!budo_renderer) {
        fprintf(stderr, "budo_video_init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        budo_video_shutdown();
        return -1;
    }

    budo_texture = SDL_CreateTexture(budo_renderer,
                                     SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     budo_width,
                                     budo_height);
    if (!budo_texture) {
        fprintf(stderr, "budo_video_init: SDL_CreateTexture failed: %s\n", SDL_GetError());
        budo_video_shutdown();
        return -1;
    }

    budo_pixels = calloc((size_t)budo_width * (size_t)budo_height, sizeof(uint32_t));
    if (!budo_pixels) {
        fprintf(stderr, "budo_video_init: out of memory.\n");
        budo_video_shutdown();
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(budo_renderer, budo_width, budo_height);
    return 0;
#endif
}

void budo_video_shutdown(void) {
#if BUDO_HAVE_SDL2
    free(budo_pixels);
    budo_pixels = NULL;

    if (budo_texture) {
        SDL_DestroyTexture(budo_texture);
        budo_texture = NULL;
    }
    if (budo_renderer) {
        SDL_DestroyRenderer(budo_renderer);
        budo_renderer = NULL;
    }
    if (budo_window) {
        SDL_DestroyWindow(budo_window);
        budo_window = NULL;
    }
#endif
}

void budo_video_clear(uint32_t color) {
#if BUDO_HAVE_SDL2
    if (!budo_pixels) {
        return;
    }
    for (int i = 0; i < budo_width * budo_height; i++) {
        budo_pixels[i] = color;
    }
#else
    (void)color;
#endif
}

void budo_video_put_pixel(int x, int y, uint32_t color) {
#if BUDO_HAVE_SDL2
    if (!budo_pixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= budo_width || y >= budo_height) {
        return;
    }
    budo_pixels[y * budo_width + x] = color;
#else
    (void)x;
    (void)y;
    (void)color;
#endif
}

void budo_video_draw_pixels(int x, int y, const uint32_t *pixels, int width, int height, int pitch) {
#if BUDO_HAVE_SDL2
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
#else
    (void)x;
    (void)y;
    (void)pixels;
    (void)width;
    (void)height;
    (void)pitch;
#endif
}

void budo_video_present(void) {
#if BUDO_HAVE_SDL2
    if (!budo_renderer || !budo_texture || !budo_pixels) {
        return;
    }

    SDL_UpdateTexture(budo_texture, NULL, budo_pixels, budo_width * (int)sizeof(uint32_t));
    SDL_RenderClear(budo_renderer);
    SDL_RenderCopy(budo_renderer, budo_texture, NULL, NULL);
    SDL_RenderPresent(budo_renderer);
#endif
}

int budo_video_get_size(int *width, int *height) {
#if BUDO_HAVE_SDL2
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
#else
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    return -1;
#endif
}
