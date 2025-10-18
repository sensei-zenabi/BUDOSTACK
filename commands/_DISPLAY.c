#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/libimage.h"
#include "../lib/termbg.h"

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_DISPLAY: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < 0 || parsed > INT_MAX) {
        fprintf(stderr, "_DISPLAY: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void print_usage(void) {
    fprintf(stderr, "Usage: _DISPLAY -x <col> -y <row> -file <path>\n");
}

int main(int argc, char *argv[]) {
    int x = 0;
    int y = 0;
    int have_x = 0;
    int have_y = 0;
    const char *file = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_DISPLAY: missing value for -x\n");
                print_usage();
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0) {
                return EXIT_FAILURE;
            }
            have_x = 1;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_DISPLAY: missing value for -y\n");
                print_usage();
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0) {
                return EXIT_FAILURE;
            }
            have_y = 1;
        } else if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_DISPLAY: missing value for -file\n");
                print_usage();
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else {
            fprintf(stderr, "_DISPLAY: unknown argument '%s'\n", argv[i]);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (!have_x || !have_y || file == NULL) {
        fprintf(stderr, "_DISPLAY: missing required arguments\n");
        print_usage();
        return EXIT_FAILURE;
    }

    LibImageResult result = libimage_render_file_at(file, x, y);
    if (result == LIBIMAGE_SUCCESS) {
        termbg_save();
        termbg_shutdown();
        return EXIT_SUCCESS;
    }

    termbg_save();
    termbg_shutdown();

    const char *message = libimage_last_error();
    if (message != NULL && message[0] != '\0') {
        fprintf(stderr, "_DISPLAY: %s\n", message);
    } else {
        fprintf(stderr, "_DISPLAY: failed to render image\n");
    }

    return EXIT_FAILURE;
}
