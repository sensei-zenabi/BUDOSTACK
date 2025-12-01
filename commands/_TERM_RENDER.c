#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_RENDER [--render] [-layer <1-16>]\n");
    fprintf(stderr, "  Triggers rendering of pending terminal pixel buffer.\n");
    fprintf(stderr, "  Use -layer to render only a single layer. Defaults to all layers.\n");
}

static int parse_layer(const char *arg, long *out_layer) {
    if (!arg || !out_layer) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_RENDER: invalid integer for -layer: '%s'\n", arg);
        return -1;
    }

    if (value < 1 || value > 16) {
        fprintf(stderr, "_TERM_RENDER: -layer must be between 1 and 16.\n");
        return -1;
    }

    *out_layer = value;
    return 0;
}

int main(int argc, char **argv) {
    int render = 0;
    long layer = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "--render") == 0) {
            render = 1;
            continue;
        }
        if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_RENDER: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_layer(argv[i], &layer) != 0) {
                return EXIT_FAILURE;
            }
            render = 1;
            continue;
        }
        fprintf(stderr, "_TERM_RENDER: unknown argument '%s'.\n", arg);
        print_usage();
        return EXIT_FAILURE;
    }

    if (!render) {
        render = 1;
    }

    if (layer == 0) {
        if (printf("\x1b]777;pixel=render\a") < 0) {
            perror("_TERM_RENDER: printf");
            return EXIT_FAILURE;
        }
    } else {
        if (printf("\x1b]777;pixel=render;pixel_layer=%ld\a", layer) < 0) {
            perror("_TERM_RENDER: printf");
            return EXIT_FAILURE;
        }
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_RENDER: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
