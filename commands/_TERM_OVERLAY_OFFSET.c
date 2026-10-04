#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_OVERLAY_OFFSET <x_percent> <y_percent>\n");
    fprintf(stderr, "  Offsets the centered overlay image as percentages of the active layout. Values may be positive or negative.\n");
}

static int parse_offset(const char *arg, const char *name, double *out_value) {
    char *endptr = NULL;
    double value = 0;

    if (!arg || !out_value) {
        return -1;
    }

    errno = 0;
    value = strtod(arg, &endptr);
    if (errno != 0 || !endptr || endptr == arg || *endptr != '\0' || !isfinite(value)) {
        fprintf(stderr, "_TERM_OVERLAY_OFFSET: invalid %s value '%s'\n", name, arg ? arg : "");
        return -1;
    }
    if (value < -100 || value > 100) {
        fprintf(stderr, "_TERM_OVERLAY_OFFSET: %s must be between -100 and 100 percent.\n", name);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    double x = 0;
    double y = 0;

    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }
    if (parse_offset(argv[1], "x offset", &x) != 0 ||
        parse_offset(argv[2], "y offset", &y) != 0) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;overlay_offset=%.17g,%.17g\a", x, y) < 0) {
        perror("_TERM_OVERLAY_OFFSET: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_OVERLAY_OFFSET: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
