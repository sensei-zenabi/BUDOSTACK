#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SIZE <x_pixels> <y_pixels>\n");
    fprintf(stderr, "  Sets the centered apps/terminal display size in pixels.\n");
    fprintf(stderr, "  Use 0 0 to fill the current display.\n");
}

static int parse_dimension(const char *arg, const char *name, long *out_value) {
    char *endptr = NULL;
    long value = 0;

    if (!arg || !out_value) {
        return -1;
    }

    errno = 0;
    value = strtol(arg, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_SIZE: invalid %s value '%s'\n", name, arg ? arg : "");
        return -1;
    }
    if (value < 0 || value > INT_MAX) {
        fprintf(stderr, "_TERM_SIZE: %s must be between 0 and %d.\n", name, INT_MAX);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    long width = 0;
    long height = 0;

    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }
    if (parse_dimension(argv[1], "width", &width) != 0 ||
        parse_dimension(argv[2], "height", &height) != 0) {
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;term_size=%ldx%ld\a", width, height) < 0) {
        perror("_TERM_SIZE: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_SIZE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
