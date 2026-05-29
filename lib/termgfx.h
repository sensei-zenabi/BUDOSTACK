#ifndef BUDOSTACK_TERMGFX_H
#define BUDOSTACK_TERMGFX_H

#include <stdint.h>

int termgfx_color_from_index(int index, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out);
int termgfx_pixel(long x, long y, uint8_t r, uint8_t g, uint8_t b, long layer);
int termgfx_rect(long x, long y, long width, long height, uint8_t r, uint8_t g, uint8_t b, long layer);
int termgfx_clear(long x, long y, long width, long height, long layer);
int termgfx_render(long layer);
int termgfx_sprite_data(long x, long y, long width, long height, const char *encoded_rgba, long layer);
int termgfx_sprite_literal(long x, long y, const char *literal, long layer);
int termgfx_sprite_file(long x, long y, const char *path, long layer);
int termgfx_sprite_load_literal(const char *path, char **literal_out);

#endif
