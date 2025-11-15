#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define TERMINAL_SOUND_MIN_CHANNEL 1L
#define TERMINAL_SOUND_MAX_CHANNEL 32L

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SOUND_STOP <channel>\n");
    fprintf(stderr, "  channel must be between %ld and %ld inclusive.\n", TERMINAL_SOUND_MIN_CHANNEL, TERMINAL_SOUND_MAX_CHANNEL);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long channel = strtol(argv[1], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_SOUND_STOP: invalid channel '%s'\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (channel < TERMINAL_SOUND_MIN_CHANNEL || channel > TERMINAL_SOUND_MAX_CHANNEL) {
        fprintf(stderr, "_TERM_SOUND_STOP: channel must be between %ld and %ld.\n", TERMINAL_SOUND_MIN_CHANNEL, TERMINAL_SOUND_MAX_CHANNEL);
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;sound=stop;channel=%ld\a", channel) < 0) {
        perror("_TERM_SOUND_STOP: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SOUND_STOP: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
