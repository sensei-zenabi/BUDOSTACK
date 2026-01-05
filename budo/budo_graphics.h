#ifndef BUDO_GRAPHICS_H
#define BUDO_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

int budo_graphics_set_resolution(int width, int height);
int budo_graphics_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, int layer);
int budo_graphics_clear_pixels(int layer);
int budo_graphics_clear_rect(int x, int y, int width, int height, int layer);
int budo_graphics_clear_screen(int width, int height, int layer);
int budo_graphics_draw_sprite_rgba(int x,
                                   int y,
                                   int width,
                                   int height,
                                   const uint8_t *rgba,
                                   int layer);
int budo_graphics_draw_frame_rgba(int width, int height, const uint8_t *rgba);
int budo_graphics_draw_text(int x, int y, const char *text, int color, int layer);
int budo_graphics_render_layer(int layer);

#endif
