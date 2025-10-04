#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "../lib/termbg.h"

// Code

#define BAR_WIDTH 10

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_BAR: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_BAR: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void print_with_background(const char *text, int row, int *col) {
    if (text == NULL)
        return;

    int last_bg = -2;
    for (const unsigned char *ptr = (const unsigned char *)text; *ptr != '\0'; ++ptr) {
        if (col && *col >= 0) {
            int bg_color;
            if (termbg_get(*col, row, &bg_color)) {
                if (bg_color != last_bg) {
                    printf("\033[48;5;%dm", bg_color);
                    last_bg = bg_color;
                }
            } else if (last_bg != -1) {
                printf("\033[49m");
                last_bg = -1;
            }
        }

        fputc(*ptr, stdout);

        if (col && *col >= 0)
            ++(*col);
    }

    if (col && *col >= 0 && last_bg != -1)
        printf("\033[49m");
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int progress = -1;
    int color = 15; /* default bright white */
    const char *title = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -x\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -y\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -title\n");
                return EXIT_FAILURE;
            }
            title = argv[i];
        } else if (strcmp(argv[i], "-progress") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -progress\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-progress", &progress) != 0)
                return EXIT_FAILURE;
        } else if (strcmp(argv[i], "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_BAR: missing value for -color\n");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-color", &color) != 0)
                return EXIT_FAILURE;
        } else {
            fprintf(stderr, "_BAR: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (progress < 0 || title == NULL) {
        fprintf(stderr, "Usage: _BAR [-x <col> -y <row>] -title <text> -progress <0-100> [-color <0-255>]\n");
        return EXIT_FAILURE;
    }

    if ((x >= 0) != (y >= 0)) {
        fprintf(stderr, "_BAR: both -x and -y must be provided together\n");
        return EXIT_FAILURE;
    }

    if (progress < 0)
        progress = 0;
    if (progress > 100)
        progress = 100;

    if (color < 0)
        color = 0;
    if (color > 255)
        color = 255;

    const char *filled_block = "\u2588";
    const char *empty_block = "\u2591";
    char bar[(BAR_WIDTH * 4) + 1];
    char *ptr = bar;
    int filled = (progress * BAR_WIDTH) / 100;
    if (filled < 0)
        filled = 0;
    if (filled > BAR_WIDTH)
        filled = BAR_WIDTH;

    for (int i = 0; i < BAR_WIDTH; ++i) {
        const char *glyph = (i < filled) ? filled_block : empty_block;
        size_t len = strlen(glyph);
        memcpy(ptr, glyph, len);
        ptr += len;
    }
    *ptr = '\0';

    int tracked_col = -1;
    if (x >= 0 && y >= 0) {
        int row = y + 1;
        int col = x + 1;

        if (row < 1)
            row = 1;
        if (col < 1)
            col = 1;

        printf("\033[%d;%dH", row, col);
        tracked_col = col - 1;
    }
    printf("\033[38;5;%dm", color);

    int row_index = (y >= 0) ? y : -1;
    print_with_background(title, row_index, &tracked_col);
    print_with_background(" ", row_index, &tracked_col);
    print_with_background(bar, row_index, &tracked_col);

    char percent_buffer[16];
    snprintf(percent_buffer, sizeof(percent_buffer), " %d%%", progress);
    print_with_background(percent_buffer, row_index, &tracked_col);

    printf("\033[49m\033[39m\n");
    fflush(stdout);

    termbg_shutdown();

    return EXIT_SUCCESS;
}

