#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_FAST [--enable | --disable]\n");
    fprintf(stderr, "  Toggles terminal fast render mode.\n");
}

int main(int argc, char **argv) {
    const char *action = "enable";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--enable") == 0) {
            action = "enable";
        } else if (strcmp(argv[i], "--disable") == 0) {
            action = "disable";
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_FAST: unknown argument '%s'.\n", argv[i]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (printf("\x1b]777;fast=%s\a", action) < 0) {
        perror("_TERM_FAST: printf");
        return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_FAST: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
