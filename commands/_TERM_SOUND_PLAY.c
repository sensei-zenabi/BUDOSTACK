#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TERMINAL_SOUND_MIN_CHANNEL 1L
#define TERMINAL_SOUND_MAX_CHANNEL 32L
#define TERMINAL_SOUND_MIN_VOLUME 0L
#define TERMINAL_SOUND_MAX_VOLUME 100L

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SOUND_PLAY <channel> <audiofile> <volume>\n");
    fprintf(stderr, "  channel must be between %ld and %ld inclusive.\n", TERMINAL_SOUND_MIN_CHANNEL, TERMINAL_SOUND_MAX_CHANNEL);
    fprintf(stderr, "  volume must be between %ld and %ld inclusive.\n", TERMINAL_SOUND_MIN_VOLUME, TERMINAL_SOUND_MAX_VOLUME);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        print_usage();
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long channel = strtol(argv[1], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_SOUND_PLAY: invalid channel '%s'\n", argv[1]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (channel < TERMINAL_SOUND_MIN_CHANNEL || channel > TERMINAL_SOUND_MAX_CHANNEL) {
        fprintf(stderr, "_TERM_SOUND_PLAY: channel must be between %ld and %ld.\n", TERMINAL_SOUND_MIN_CHANNEL, TERMINAL_SOUND_MAX_CHANNEL);
        return EXIT_FAILURE;
    }

    const char *path = argv[2];
    if (!path || path[0] == '\0') {
        fprintf(stderr, "_TERM_SOUND_PLAY: audio file path cannot be empty.\n");
        return EXIT_FAILURE;
    }

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        perror("_TERM_SOUND_PLAY: realpath");
        return EXIT_FAILURE;
    }

    if (access(resolved, R_OK) != 0) {
        perror("_TERM_SOUND_PLAY: access");
        return EXIT_FAILURE;
    }

    endptr = NULL;
    errno = 0;
    long volume = strtol(argv[3], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "_TERM_SOUND_PLAY: invalid volume '%s'\n", argv[3]);
        print_usage();
        return EXIT_FAILURE;
    }

    if (volume < TERMINAL_SOUND_MIN_VOLUME || volume > TERMINAL_SOUND_MAX_VOLUME) {
        fprintf(stderr, "_TERM_SOUND_PLAY: volume must be between %ld and %ld.\n", TERMINAL_SOUND_MIN_VOLUME, TERMINAL_SOUND_MAX_VOLUME);
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;sound=play;channel=%ld;path=%s;volume=%ld\a", channel, resolved, volume) < 0) {
        perror("_TERM_SOUND_PLAY: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SOUND_PLAY: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
