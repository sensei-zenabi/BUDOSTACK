#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_MARGIN <pixels>\n");
    fprintf(stderr, "  Sets the terminal render margin in pixels.\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(argv[1], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_MARGIN: invalid pixel value '%s'\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (value < 0 || value > INT_MAX) {
        fprintf(stderr, "_TERM_MARGIN: pixel value must be between 0 and %d.\n", INT_MAX);
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;margin=%ld\a", value) < 0) {
        perror("_TERM_MARGIN: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_MARGIN: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
