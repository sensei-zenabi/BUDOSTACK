#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_BACKGROUND_VOLUME <0-100>\n");
    fprintf(stderr, "  Sets the continuous terminal background hum volume.\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv || !argv[1]) {
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long volume = strtol(argv[1], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0' || volume < 0 || volume > 100) {
        fprintf(stderr, "_TERM_BACKGROUND_VOLUME: volume must be an integer from 0 to 100.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;background_volume=%ld\a", volume) < 0) {
        perror("_TERM_BACKGROUND_VOLUME: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_BACKGROUND_VOLUME: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
