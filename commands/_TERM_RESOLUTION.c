#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_RESOLUTION <width> <height>\n");
    fprintf(stderr, "  Changes the resolution to <width>x<height> defined as pixels.\n");
    fprintf(stderr, "  Use 0 0 to restore the default resolution.\n");
    fprintf(stderr, "Usage: _TERM_RESOLUTION LOW\n");
    fprintf(stderr, "  Changes the resolution to 640x360.\n");
    fprintf(stderr, "Usage: _TERM_RESOLUTION HIGH\n");
    fprintf(stderr, "  Changes the resolution to 800x450.\n");
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

static int parse_preset(const char *arg, long *width, long *height) {
    if (!arg || !width || !height) {
        return 0;
    }

    if (strcasecmp(arg, "LOW") == 0) {
        *width = 640;
        *height = 360;
        return 1;
    }

    if (strcasecmp(arg, "HIGH") == 0) {
        *width = 800;
        *height = 450;
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    long width = 0;
    long height = 0;

    if (argc == 2) {
        if (!parse_preset(argv[1], &width, &height)) {
            print_usage();
            return EXIT_FAILURE;
        }
    } else if (argc == 3) {
        if (parse_dimension(argv[1], "width", &width) != 0) {
            return EXIT_FAILURE;
        }

        if (parse_dimension(argv[2], "height", &height) != 0) {
            return EXIT_FAILURE;
        }
    } else {
        print_usage();
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
