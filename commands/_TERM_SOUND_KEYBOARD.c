#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SOUND_KEYBOARD <enable|disable>\n");
    fprintf(stderr, "  Enables or disables keyboard typing sound effects in terminal apps.\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv || !argv[1]) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *action = argv[1];
    if (strcmp(action, "enable") != 0 && strcmp(action, "disable") != 0) {
        fprintf(stderr, "_TERM_SOUND_KEYBOARD: action must be 'enable' or 'disable'.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;keyboard_sound=%s\a", action) < 0) {
        perror("_TERM_SOUND_KEYBOARD: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SOUND_KEYBOARD: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
