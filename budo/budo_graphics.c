#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"

#include <stdio.h>

int budo_gfx_init(void) {
    budo_gfx_hide_cursor();
    budo_gfx_clear();
    return 0;
}

void budo_gfx_shutdown(void) {
    budo_gfx_reset_color();
    budo_gfx_show_cursor();
    printf("\033[0m");
    fflush(stdout);
}

void budo_gfx_clear(void) {
    printf("\033[2J\033[H");
}

void budo_gfx_present(void) {
    fflush(stdout);
}

void budo_gfx_hide_cursor(void) {
    printf("\033[?25l");
}

void budo_gfx_show_cursor(void) {
    printf("\033[?25h");
}

void budo_gfx_move_cursor(int x, int y) {
    if (x < 1) {
        x = 1;
    }
    if (y < 1) {
        y = 1;
    }
    printf("\033[%d;%dH", y, x);
}

void budo_gfx_draw_text(int x, int y, const char *text) {
    if (!text) {
        return;
    }
    budo_gfx_move_cursor(x, y);
    fputs(text, stdout);
}

void budo_gfx_set_color(budo_color_t fg, budo_color_t bg) {
    if (fg == BUDO_COLOR_DEFAULT && bg == BUDO_COLOR_DEFAULT) {
        printf("\033[0m");
        return;
    }
    if (fg == BUDO_COLOR_DEFAULT) {
        printf("\033[%dm", 40 + bg);
        return;
    }
    if (bg == BUDO_COLOR_DEFAULT) {
        printf("\033[%dm", 30 + fg);
        return;
    }
    printf("\033[%d;%dm", 30 + fg, 40 + bg);
}

void budo_gfx_reset_color(void) {
    printf("\033[0m");
}
