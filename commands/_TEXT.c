#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

                size_t arg_len = strlen(argv[i]);
                size_t new_len = text_len + arg_len + (text_len > 0 ? 1 : 0);
                char *new_text = realloc(text, new_len + 1);
                if (new_text == NULL) {
                    fprintf(stderr, "_TEXT: memory allocation failed for -text\n");
                    free(text);
                    goto cleanup;
                }

                text = new_text;
                if (text_len > 0) {
                    text[text_len] = ' ';
                    ++text_len;
                }
                memcpy(text + text_len, argv[i], arg_len);
                text_len += arg_len;
                text[text_len] = '\0';
            }

            if (text == NULL) {
                fprintf(stderr, "_TEXT: missing value for -text\n");
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
    fputs(text, stdout);
    printf("\033[0m");
    fflush(stdout);

    exit_code = EXIT_SUCCESS;

cleanup:
    free(text);
    return exit_code;
}

