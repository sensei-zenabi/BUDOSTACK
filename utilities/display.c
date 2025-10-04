#include <stdio.h>
#include <stdlib.h>

#include "../lib/libimage.h"

static int display_text(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror("display: fopen failed");
        return EXIT_FAILURE;
    }

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (putchar(ch) == EOF) {
            perror("display: putchar failed");
            fclose(fp);
            return EXIT_FAILURE;
        }
    }

    if (ferror(fp) != 0) {
        perror("display: fgetc failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: display <file>\n");
        return EXIT_FAILURE;
    }

    const char *path = argv[1];
    LibImageResult result = libimage_render_file_at(path, 0, 0);

    if (result == LIBIMAGE_SUCCESS) {
        return EXIT_SUCCESS;
    }

    if (result == LIBIMAGE_UNSUPPORTED_FORMAT) {
        return display_text(path);
    }

    const char *message = libimage_last_error();
    if (message != NULL && message[0] != '\0') {
        fprintf(stderr, "display: %s\n", message);
    } else {
        fprintf(stderr, "display: failed to render image\n");
    }
    return EXIT_FAILURE;
}
