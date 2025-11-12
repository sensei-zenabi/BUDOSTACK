#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/retroprofile.h"
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

static int default_color_index(void) {
    int index = retroprofile_active_default_foreground_index();
    if (index >= 0)
        return index;
    return 15;
}

static int clamp_color_value(int value) {
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

static int resolve_color(int color_index) {
    int clamped = clamp_color_value(color_index);
    if (clamped >= 0 && clamped < 16) {
        RetroColor palette_color;
        if (retroprofile_color_from_active(clamped, &palette_color) == 0)
            return termbg_encode_truecolor(palette_color.r, palette_color.g, palette_color.b);
    }
    return clamped;
}

static void apply_foreground(int resolved_color, int fallback_index) {
    if (termbg_is_truecolor(resolved_color)) {
        int r, g, b;
        termbg_decode_truecolor(resolved_color, &r, &g, &b);
        printf("\033[38;2;%d;%d;%dm", r, g, b);
    } else {
        printf("\033[38;5;%dm", fallback_index);
    }
}

static void reset_background(int *last_bg) {
    if (last_bg != NULL && *last_bg != -1) {
        printf("\033[49m");
        *last_bg = -1;
    }
}

static void apply_background(int encoded_color, int *last_bg) {
    if (last_bg == NULL)
        return;
    if (*last_bg == encoded_color)
        return;
    if (termbg_is_truecolor(encoded_color)) {
        int r, g, b;
        termbg_decode_truecolor(encoded_color, &r, &g, &b);
        printf("\033[48;2;%d;%d;%dm", r, g, b);
    } else {
        printf("\033[48;5;%dm", encoded_color);
    }
    *last_bg = encoded_color;
}

static void print_with_background(const char *text, int start_x, int row) {
    int col = start_x;
    int last_bg = -1;

    for (const unsigned char *ptr = (const unsigned char *)text; *ptr != '\0'; ++ptr) {
        if (start_x >= 0) {
            int bg_color;
            if (termbg_get(col, row, &bg_color)) {
                apply_background(bg_color, &last_bg);
            } else {
                reset_background(&last_bg);
            }
        }

        fputc(*ptr, stdout);

        if (start_x >= 0)
            ++col;
    }

    if (start_x >= 0)
        reset_background(&last_bg);
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int color = default_color_index();
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

    color = clamp_color_value(color);
    int resolved_color = resolve_color(color);

    int row = y + 1;
    int col = x + 1;

    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;

    printf("\033[%d;%dH", row, col);
    apply_foreground(resolved_color, color);
    print_with_background(text, x, y);
    printf("\033[39m");
    fflush(stdout);

    exit_code = EXIT_SUCCESS;

cleanup:
    free(text);
    termbg_shutdown();
    return exit_code;
}

