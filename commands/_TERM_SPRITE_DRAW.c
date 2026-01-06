#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE_DRAW -id <number> -x <pixels> -y <pixels> [-layer <1-16>]\n");
    fprintf(stderr, "  Draws a cached sprite by id.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE_DRAW: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE_DRAW: %s must be between %ld and %ld.\n", name, min_value, max_value);
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

    long id = -1;
    long origin_x = -1;
    long origin_y = -1;
    long layer = 1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-id") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_DRAW: missing value for -id.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-id", 0, INT_MAX, &id) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_DRAW: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &origin_x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_DRAW: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &origin_y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_DRAW: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_SPRITE_DRAW: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (id < 0 || origin_x < 0 || origin_y < 0) {
        fprintf(stderr, "_TERM_SPRITE_DRAW: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;sprite_cache=draw;sprite_id=%ld;sprite_x=%ld;sprite_y=%ld;sprite_cache_layer=%ld\a",
               id, origin_x, origin_y, layer) < 0) {
        perror("_TERM_SPRITE_DRAW: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_DRAW: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
