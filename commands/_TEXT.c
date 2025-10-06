#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/termbg.h"

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_TEXT: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_TEXT: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void clamp_color(int *color) {
    if (*color < 0)
        *color = 0;
    else if (*color > 255)
        *color = 255;
}

static void print_with_background(const char *text, int start_x, int row) {
    int col = start_x;
    int last_bg = -2;

    for (const unsigned char *ptr = (const unsigned char *)text; *ptr != '\0'; ++ptr) {
        if (start_x >= 0) {
            int bg_color;
            if (termbg_get(col, row, &bg_color)) {
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

        if (start_x >= 0)
            ++col;
    }

    if (start_x >= 0 && last_bg != -1)
        printf("\033[49m");
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int color = 15; /* bright white by default */
    char *text = NULL;
    int exit_code = EXIT_FAILURE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -x\n");
                goto cleanup;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                goto cleanup;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -y\n");
                goto cleanup;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                goto cleanup;
        } else if (strcmp(argv[i], "-text") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -text\n");
                goto cleanup;
            }

            free(text);
            text = NULL;
            size_t text_len = 0;

            int suppress_space = 0;

            for (; i < argc; ++i) {
                int is_option = 0;
                if (argv[i][0] == '-') {
                    if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "-y") == 0 ||
                        strcmp(argv[i], "-color") == 0 || strcmp(argv[i], "-text") == 0) {
                        is_option = 1;
                    }
                }

                if (is_option && text_len > 0) {
                    --i;
                    break;
                } else if (is_option) {
                    is_option = 0;
                }

                if (strcmp(argv[i], "+") == 0) {
                    if (suppress_space) {
                        fprintf(stderr, "_TEXT: consecutive '+' tokens in -text\n");
                        goto cleanup;
                    }
                    suppress_space = 1;
                    continue;
                }

                size_t arg_len = strlen(argv[i]);
                int need_space = text_len > 0 && suppress_space == 0;
                size_t new_len = text_len + arg_len + (need_space ? 1 : 0);
                char *new_text = realloc(text, new_len + 1);
                if (new_text == NULL) {
                    fprintf(stderr, "_TEXT: memory allocation failed for -text\n");
                    free(text);
                    text = NULL;
                    goto cleanup;
                }

                text = new_text;
                if (need_space) {
                    text[text_len] = ' ';
                    ++text_len;
                }
                memcpy(text + text_len, argv[i], arg_len);
                text_len += arg_len;
                text[text_len] = '\0';
                suppress_space = 0;
            }

            if (text == NULL) {
                fprintf(stderr, "_TEXT: missing value for -text\n");
                goto cleanup;
            }

            if (suppress_space) {
                fprintf(stderr, "_TEXT: dangling '+' in -text value\n");
                goto cleanup;
            }
        } else if (strcmp(argv[i], "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TEXT: missing value for -color\n");
                goto cleanup;
            }
            if (parse_int(argv[i], "-color", &color) != 0)
                goto cleanup;
        } else {
            fprintf(stderr, "_TEXT: unknown argument '%s'\n", argv[i]);
            goto cleanup;
        }
    }

    if (x < 0 || y < 0 || text == NULL) {
        fprintf(stderr, "Usage: _TEXT -x <col> -y <row> -text <string> [-color <0-255>]\n");
        goto cleanup;
    }

    clamp_color(&color);

    int row = y + 1;
    int col = x + 1;

    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;

    printf("\033[%d;%dH", row, col);
    printf("\033[38;5;%dm", color);
    print_with_background(text, x, y);
    printf("\033[39m");
    fflush(stdout);

    exit_code = EXIT_SUCCESS;

cleanup:
    free(text);
    termbg_shutdown();
    return exit_code;
}

