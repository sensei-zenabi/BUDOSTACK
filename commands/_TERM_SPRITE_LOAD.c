#define _POSIX_C_SOURCE 200809L

#include "../lib/termgfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE_LOAD -file <path>\n");
    fprintf(stderr, "  Load a PNG/BMP file and print a reusable TASK sprite literal.\n");
    fprintf(stderr, "  Capture the output with `RUN _TERM_SPRITE_LOAD ... TO $VAR`\n");
    fprintf(stderr, "  and pass that literal back to _TERM_SPRITE with -sprite.\n");
}

int main(int argc, char **argv) {
    const char *file = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_LOAD: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else {
            fprintf(stderr, "_TERM_SPRITE_LOAD: unknown argument '%s'.\n", argv[i]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (!file) {
        fprintf(stderr, "_TERM_SPRITE_LOAD: missing -file argument.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    char *literal = NULL;
    if (termgfx_sprite_load_literal(file, &literal) != 0) {
        return EXIT_FAILURE;
    }

    if (printf("%s\n", literal) < 0) {
        perror("_TERM_SPRITE_LOAD: printf");
        free(literal);
        return EXIT_FAILURE;
    }
    free(literal);

    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_LOAD: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
