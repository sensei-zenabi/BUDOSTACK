#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: _TERM_LAYOUT_ASPECT <width:height|screen>\n");
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "screen") != 0) {
        char *end_x = NULL;
        char *end_y = NULL;
        errno = 0;
        double x = strtod(argv[1], &end_x);
        int valid_x = errno == 0 && end_x != argv[1] && *end_x == ':' &&
                      isfinite(x) && x > 0;
        if (!valid_x) {
            fprintf(stderr, "_TERM_LAYOUT_ASPECT: expected a positive width:height ratio.\n");
            return EXIT_FAILURE;
        }
        errno = 0;
        double y = strtod(end_x + 1, &end_y);
        double aspect = x / y;
        if (errno != 0 || end_y == end_x + 1 || *end_y != '\0' || !isfinite(y) ||
            y <= 0 || !isfinite(aspect) || aspect < 0.1 || aspect > 10) {
            fprintf(stderr, "_TERM_LAYOUT_ASPECT: ratio must be between 1:10 and 10:1.\n");
            return EXIT_FAILURE;
        }
    }
    if (printf("\x1b]777;layout_aspect=%s\a", argv[1]) < 0 || fflush(stdout) != 0) {
        perror("_TERM_LAYOUT_ASPECT: output");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
