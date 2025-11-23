#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_RENDER [--render]\n");
    fprintf(stderr, "  Triggers rendering of pending terminal pixel buffer.\n");
}

int main(int argc, char **argv) {
    int render = 0;

    if (argc == 1) {
        render = 1;
    } else if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        }

        if (strcmp(argv[1], "--render") == 0) {
            render = 1;
        }
    }

    if (!render) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;pixel=render\a") < 0) {
        perror("_TERM_RENDER: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_RENDER: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
