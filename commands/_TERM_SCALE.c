#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SCALE <scale>\n");
    fprintf(stderr, "  1: original resolution, 2: double resolution.\n");
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
        fprintf(stderr, "_TERM_SCALE: invalid scale value '%s'\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (value < 1 || value > 2) {
        fprintf(stderr, "_TERM_SCALE: scale must be either 1 or 2!\n");
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;scale=%ld\a", value) < 0) {
        perror("_TERM_SCALE: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SCALE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
