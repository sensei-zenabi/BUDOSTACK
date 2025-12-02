#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/stb_image.h"

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE_LOAD -file <path> [-layer <1-16>]\n");
    fprintf(stderr, "  Loads a PNG or BMP sprite and writes a reusable base64 blob to stdout.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE_LOAD: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE_LOAD: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
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

static void write_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *file = NULL;
    long layer = 1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_LOAD: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_LOAD: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_SPRITE_LOAD: unknown argument '%s'.\n", arg);
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

    const size_t header_size = 12u;
    size_t raw_size = pixel_count * 4u;
    if (raw_size > SIZE_MAX - header_size) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: sprite data too large.\n");
        return EXIT_FAILURE;
    }

    size_t blob_size = header_size + raw_size;
    uint8_t *blob = malloc(blob_size);
    if (!blob) {
        stbi_image_free(pixels);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to allocate %zu bytes.\n", blob_size);
        return EXIT_FAILURE;
    }

    write_u32_le(blob, (uint32_t)width);
    write_u32_le(blob + 4u, (uint32_t)height);
    write_u32_le(blob + 8u, (uint32_t)layer);
    memcpy(blob + header_size, pixels, raw_size);
    stbi_image_free(pixels);

    size_t encoded_size = base64_encoded_size(blob_size);
    if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
        free(blob);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to compute encoded size.\n");
        return EXIT_FAILURE;
    }

    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        free(blob);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
        return EXIT_FAILURE;
    }

    int status = encode_base64(blob, blob_size, encoded, encoded_size + 1u);
    free(blob);
    if (status != 0) {
        free(encoded);
        fprintf(stderr, "_TERM_SPRITE_LOAD: failed to encode sprite.\n");
        return EXIT_FAILURE;
    }

    if (printf("%s\n", encoded) < 0) {
        perror("_TERM_SPRITE_LOAD: printf");
        free(encoded);
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_LOAD: fflush");
        free(encoded);
        return EXIT_FAILURE;
    }

    free(encoded);
    return EXIT_SUCCESS;
}
