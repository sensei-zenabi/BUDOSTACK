#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_FPS -enable <0|1>\n");
    fprintf(stderr, "  -enable 1 shows the FPS in apps/terminal bottom right corner.\n");
    fprintf(stderr, "  -enable 0 hides the FPS display.\n");
}

int main(int argc, char **argv) {
    if (argc != 3 || !argv || !argv[1] || !argv[2]) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-enable") != 0) {
        fprintf(stderr, "_TERM_FPS: expected -enable argument.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(argv[2], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_FPS: invalid enable value '%s'\n", argv[2]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (value != 0 && value != 1) {
        fprintf(stderr, "_TERM_FPS: enable must be 0 or 1.\n");
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;fps=%ld\a", value) < 0) {
        perror("_TERM_FPS: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_FPS: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
