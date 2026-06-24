#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_OFFSET <x_pixels> <y_pixels>\n");
    fprintf(stderr, "  Offsets the centered apps/terminal display in pixels. Values may be positive or negative.\n");
}

static int parse_offset(const char *arg, const char *name, long *out_value) {
    char *endptr = NULL;
    long value = 0;

    if (!arg || !out_value) {
        return -1;
    }

    errno = 0;
    value = strtol(arg, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_OFFSET: invalid %s value '%s'\n", name, arg ? arg : "");
        return -1;
    }
    if (value < INT_MIN || value > INT_MAX) {
        fprintf(stderr, "_TERM_OFFSET: %s must be between %d and %d.\n", name, INT_MIN, INT_MAX);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    long x = 0;
    long y = 0;

    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }
    if (parse_offset(argv[1], "x offset", &x) != 0 ||
        parse_offset(argv[2], "y offset", &y) != 0) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;term_offset=%ld,%ld\a", x, y) < 0) {
        perror("_TERM_OFFSET: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_OFFSET: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
