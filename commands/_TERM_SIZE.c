#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SIZE <x_percent> <y_percent>\n");
    fprintf(stderr, "  Sets the centered apps/terminal display size as percentages of the active layout.\n");
    fprintf(stderr, "  Use 0 0 for automatic layout size.\n");
}

static int parse_dimension(const char *arg, const char *name, double *out_value) {
    char *endptr = NULL;
    double value = 0;

    if (!arg || !out_value) {
        return -1;
    }

    errno = 0;
    value = strtod(arg, &endptr);
    if (errno != 0 || !endptr || endptr == arg || *endptr != '\0' || !isfinite(value)) {
        fprintf(stderr, "_TERM_SIZE: invalid %s value '%s'\n", name, arg ? arg : "");
        return -1;
    }
    if (value < 0 || value > 100) {
        fprintf(stderr, "_TERM_SIZE: %s must be between 0 and 100 percent.\n", name);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    double width = 0;
    double height = 0;

    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }
    if (parse_dimension(argv[1], "width", &width) != 0 ||
        parse_dimension(argv[2], "height", &height) != 0) {
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;term_size=%.17gx%.17g\a", width, height) < 0) {
        perror("_TERM_SIZE: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_SIZE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
