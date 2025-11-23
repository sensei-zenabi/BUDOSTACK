#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_RENDER\\n");
    fprintf(stderr, "  Triggers rendering of pending terminal pixel buffer.\\n");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
            print_usage();
            return EXIT_SUCCESS;
        }

        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\\x1b]777;pixel=render\\a") < 0) {
        perror("_TERM_RENDER: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_RENDER: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
