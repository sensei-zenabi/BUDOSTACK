#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/stb_image.h"

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE_LOAD -file <path>\n");
    fprintf(stderr, "  Loads a PNG or BMP sprite and prints a TASK array literal\n");
    fprintf(stderr, "  in the form {width,height,\"<base64 RGBA data>\"}.\n");
    fprintf(stderr, "  Capture the output with `RUN _TERM_SPRITE_LOAD ... TO $VAR`\n");
    fprintf(stderr, "  to reuse the sprite data without re-reading the file. Pass the\n");
    fprintf(stderr, "  literal back to _TERM_SPRITE with -sprite for faster calls.\n");
}

static size_t base64_encoded_size(size_t raw_size) {
    if (raw_size == 0) {
        return 0;
    }
    size_t rem = raw_size % 3u;
    size_t blocks = raw_size / 3u;
    size_t encoded = blocks * 4u;
    if (rem > 0u) {
        encoded += 4u;
    }
    return encoded;
}

static char base64_encode_table(int idx) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (idx < 0 || idx >= 64) {
        return '=';
    }
    return table[idx];
}

static int encode_base64(const uint8_t *data, size_t size, char *out, size_t out_size) {
    if (!data || !out) {
        return -1;
    }
    size_t required = base64_encoded_size(size);
    if (out_size < required + 1u) {
        return -1;
    }

    size_t out_idx = 0u;
    for (size_t i = 0u; i + 2u < size; i += 3u) {
        uint32_t block = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) | (uint32_t)data[i + 2u];
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)(block & 0x3Fu));
    }

    size_t remaining = size % 3u;
    if (remaining == 1u) {
        uint32_t block = ((uint32_t)data[size - 1u]) << 16;
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = '=';
        out[out_idx++] = '=';
    } else if (remaining == 2u) {
        uint32_t block = ((uint32_t)data[size - 2u] << 16) | ((uint32_t)data[size - 1u] << 8);
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = '=';
    }

    out[out_idx] = '\0';
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }

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

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(file, &width, &height, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        if (reason && *reason) {
            fprintf(stderr, "_TERM_SPRITE_LOAD: failed to load '%s': %s\n", file, reason);
        } else {
            fprintf(stderr, "_TERM_SPRITE_LOAD: failed to load '%s'\n", file);
        }
        return EXIT_FAILURE;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: invalid image dimensions in '%s'\n", file);
        return EXIT_FAILURE;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (width_sz != 0 && height_sz > SIZE_MAX / width_sz) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: image dimensions overflow.\n");
        return EXIT_FAILURE;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: image too large to encode.\n");
        return EXIT_FAILURE;
    }

    size_t raw_size = pixel_count * 4u;
    size_t encoded_size = base64_encoded_size(raw_size);
    if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to compute encoded size.\n");
        return EXIT_FAILURE;
    }

    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
        return EXIT_FAILURE;
    }

    int encode_status = encode_base64(pixels, raw_size, encoded, encoded_size + 1u);
    stbi_image_free(pixels);
    if (encode_status != 0) {
        free(encoded);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to encode image data.\n");
        return EXIT_FAILURE;
    }

    if (printf("{%d,%d,\"%s\"}\n", width, height, encoded) < 0) {
        perror("_TERM_SPRITE_LOAD: printf");
        free(encoded);
        return EXIT_FAILURE;
    }

    free(encoded);

    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_LOAD: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
