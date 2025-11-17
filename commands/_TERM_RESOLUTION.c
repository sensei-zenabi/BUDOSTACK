#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_RESOLUTION <width> <height>\n");
    fprintf(stderr, "  Sets the terminal logical resolution in pixels.\n");
    fprintf(stderr, "  Use 0 0 to restore the default resolution.\n");
}

static int parse_dimension(const char *arg, const char *name, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_RESOLUTION: invalid %s value '%s'\n", name, arg ? arg : "");
        return -1;
    }

    if (value < 0 || value > INT_MAX) {
        fprintf(stderr, "_TERM_RESOLUTION: %s must be between 0 and %d.\n", name, INT_MAX);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    long width = 0;
    long height = 0;

    if (parse_dimension(argv[1], "width", &width) != 0) {
        return EXIT_FAILURE;
    }

    if (parse_dimension(argv[2], "height", &height) != 0) {
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;resolution=%ldx%ld\a", width, height) < 0) {
        perror("_TERM_RESOLUTION: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_RESOLUTION: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
