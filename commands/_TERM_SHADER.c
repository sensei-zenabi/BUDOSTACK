#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SHADER <disable|light|shader>\n");
    fprintf(stderr, "  disable : disable CRT post-processing effects.\n");
    fprintf(stderr, "  light   : enable lightweight CPU CRT simulation (curvature + scanlines).\n");
    fprintf(stderr, "  shader  : enable full shader stack CRT processing.\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv || !argv[1]) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *action = argv[1];
    if (strcmp(action, "disable") != 0 &&
        strcmp(action, "light") != 0 &&
        strcmp(action, "shader") != 0) {
        fprintf(stderr, "_TERM_SHADER: action must be 'disable', 'light', or 'shader'.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;shader=%s\a", action) < 0) {
        perror("_TERM_SHADER: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SHADER: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
