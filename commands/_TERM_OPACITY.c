#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_OPACITY <value>\n");
    fprintf(stderr, "  Sets terminal opacity from 0 (opaque) to 100 (transparent).\n");
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
        fprintf(stderr, "_TERM_OPACITY: invalid opacity value '%s'\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (value < 0 || value > 100) {
        fprintf(stderr, "_TERM_OPACITY: value must be between 0 and 100.\n");
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;opacity=%ld\a", value) < 0) {
        perror("_TERM_OPACITY: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_OPACITY: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
