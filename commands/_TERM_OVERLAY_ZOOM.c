#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_OVERLAY_ZOOM <percent>\n");
    fprintf(stderr, "  Sets overlay width and height as percentages of the current screen.\n");
    fprintf(stderr, "  100 fills the screen; smaller values zoom out, larger values zoom in.\n");
    fprintf(stderr, "  Range: 1 to 1000 percent; decimals allowed.\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    double value = strtod(argv[1], &endptr);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' ||
        !isfinite(value) || value < 1 || value > 1000) {
        fprintf(stderr, "_TERM_OVERLAY_ZOOM: zoom must be between 1 and 1000 percent.\n");
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;overlay_zoom=%.17g\a", value) < 0) {
        perror("_TERM_OVERLAY_ZOOM: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_OVERLAY_ZOOM: fflush");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
