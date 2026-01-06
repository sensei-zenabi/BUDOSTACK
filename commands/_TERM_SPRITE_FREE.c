#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE_FREE -id <number>\n");
    fprintf(stderr, "  Frees a cached sprite by id.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE_FREE: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE_FREE: %s must be between %ld and %ld.\n", name, min_value, max_value);
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

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-id") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_FREE: missing value for -id.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-id", 0, INT_MAX, &id) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_SPRITE_FREE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (id < 0) {
        fprintf(stderr, "_TERM_SPRITE_FREE: missing -id.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;sprite_cache=free;sprite_id=%ld\a", id) < 0) {
        perror("_TERM_SPRITE_FREE: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_FREE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
