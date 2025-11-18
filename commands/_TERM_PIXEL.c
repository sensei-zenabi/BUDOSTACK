#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "  Draws raw SDL pixels on the terminal window.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_PIXEL: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_PIXEL: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-r") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -r.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-r", 0, 255, &r) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-g") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -g.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-g", 0, 255, &g) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-b") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -b.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-b", 0, 255, &b) != 0) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_PIXEL: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
        fprintf(stderr, "_TERM_PIXEL: missing required draw arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }
    if (printf("\x1b]777;pixel=draw;pixel_x=%ld;pixel_y=%ld;pixel_r=%ld;pixel_g=%ld;pixel_b=%ld\a",
               x,
               y,
               r,
               g,
               b) < 0) {
        perror("_TERM_PIXEL: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
