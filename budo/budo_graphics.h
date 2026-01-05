#ifndef BUDO_GRAPHICS_H
#define BUDO_GRAPHICS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDO_COLOR_DEFAULT = -1,
    BUDO_COLOR_BLACK = 0,
    BUDO_COLOR_RED = 1,
    BUDO_COLOR_GREEN = 2,
    BUDO_COLOR_YELLOW = 3,
    BUDO_COLOR_BLUE = 4,
    BUDO_COLOR_MAGENTA = 5,
    BUDO_COLOR_CYAN = 6,
    BUDO_COLOR_WHITE = 7
} budo_color_t;

int budo_gfx_init(void);
void budo_gfx_shutdown(void);
void budo_gfx_clear(void);
void budo_gfx_present(void);
void budo_gfx_hide_cursor(void);
void budo_gfx_show_cursor(void);
void budo_gfx_move_cursor(int x, int y);
void budo_gfx_draw_text(int x, int y, const char *text);
void budo_gfx_set_color(budo_color_t fg, budo_color_t bg);
void budo_gfx_reset_color(void);

#ifdef __cplusplus
}
#endif

#endif
