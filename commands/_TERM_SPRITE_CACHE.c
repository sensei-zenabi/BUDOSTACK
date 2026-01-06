#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/stb_image.h"

static void print_usage(void) {
    fprintf(stderr,
            "Usage: _TERM_SPRITE_CACHE -id <number> (-file <path> | -sprite {w,h,\"data\"} | -data <base64> -width <px> -height <px>)\n");
    fprintf(stderr, "  Caches a sprite in the terminal for faster repeated drawing.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE_CACHE: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: %s must be between %ld and %ld.\n", name, min_value, max_value);
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

static int parse_sprite_literal(const char *literal, int *width_out, int *height_out, char **data_out) {
    if (!literal || !width_out || !height_out || !data_out) {
        return -1;
    }

    const char *p = literal;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p != '{') {
        fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal must start with '{'.\n");
        return -1;
    }
    ++p;

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }

    errno = 0;
    char *endptr = NULL;
    long width = strtol(p, &endptr, 10);
    if (errno != 0 || endptr == p || width <= 0 || width > INT_MAX) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: invalid sprite width in literal.\n");
        return -1;
    }

    p = endptr;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != ',') {
        fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal missing comma after width.\n");
        return -1;
    }
    ++p;

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }

    errno = 0;
    long height = strtol(p, &endptr, 10);
    if (errno != 0 || endptr == p || height <= 0 || height > INT_MAX) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: invalid sprite height in literal.\n");
        return -1;
    }

    p = endptr;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != ',') {
        fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal missing comma after height.\n");
        return -1;
    }
    ++p;

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    const char *data_start = NULL;
    const char *data_end = NULL;
    if (*p == '"') {
        ++p;
        data_start = p;
        const char *closing_quote = strchr(data_start, '"');
        if (!closing_quote) {
            fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal missing closing quote.\n");
            return -1;
        }
        data_end = closing_quote;
        p = closing_quote + 1;
    } else {
        data_start = p;
        while (*p && !isspace((unsigned char)*p) && *p != '}') {
            ++p;
        }
        data_end = p;
    }

    if (!data_start || !data_end || data_end <= data_start) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal missing data.\n");
        return -1;
    }

    size_t data_len = (size_t)(data_end - data_start);
    char *data_copy = malloc(data_len + 1u);
    if (!data_copy) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: failed to allocate sprite data.\n");
        return -1;
    }
    memcpy(data_copy, data_start, data_len);
    data_copy[data_len] = '\0';

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '}') {
        free(data_copy);
        fprintf(stderr, "_TERM_SPRITE_CACHE: sprite literal must end with '}'.\n");
        return -1;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '\0') {
        free(data_copy);
        fprintf(stderr, "_TERM_SPRITE_CACHE: unexpected characters after sprite literal.\n");
        return -1;
    }

    *width_out = (int)width;
    *height_out = (int)height;
    *data_out = data_copy;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long id = -1;
    long width = -1;
    long height = -1;
    const char *file = NULL;
    const char *data = NULL;
    const char *sprite_literal = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-id") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -id.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-id", 0, INT_MAX, &id) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else if (strcmp(arg, "-sprite") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -sprite.\n");
                return EXIT_FAILURE;
            }
            sprite_literal = argv[i];
        } else if (strcmp(arg, "-data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -data.\n");
                return EXIT_FAILURE;
            }
            data = argv[i];
        } else if (strcmp(arg, "-width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -width.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-width", 1, INT_MAX, &width) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: missing value for -height.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-height", 1, INT_MAX, &height) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_SPRITE_CACHE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (id < 0) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: missing -id.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    int sources = 0;
    if (file) {
        sources++;
    }
    if (sprite_literal) {
        sources++;
    }
    if (data) {
        sources++;
    }
    if (sources != 1) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: specify exactly one sprite source.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    char *literal_data = NULL;
    char *encoded = NULL;
    int sprite_width = 0;
    int sprite_height = 0;

    if (file) {
        int image_w = 0;
        int image_h = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load(file, &image_w, &image_h, &channels, 4);
        if (!pixels) {
            const char *reason = stbi_failure_reason();
            if (reason && *reason) {
                fprintf(stderr, "_TERM_SPRITE_CACHE: failed to load '%s': %s\n", file, reason);
            } else {
                fprintf(stderr, "_TERM_SPRITE_CACHE: failed to load '%s'\n", file);
            }
            return EXIT_FAILURE;
        }

        if (image_w <= 0 || image_h <= 0) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE_CACHE: invalid image dimensions in '%s'\n", file);
            return EXIT_FAILURE;
        }

        size_t pixel_count = (size_t)image_w * (size_t)image_h;
        if (pixel_count > SIZE_MAX / 4u) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE_CACHE: image too large to encode.\n");
            return EXIT_FAILURE;
        }

        size_t raw_size = pixel_count * 4u;
        size_t encoded_size = base64_encoded_size(raw_size);
        if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE_CACHE: failed to compute encoded size.\n");
            return EXIT_FAILURE;
        }

        encoded = malloc(encoded_size + 1u);
        if (!encoded) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE_CACHE: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
            return EXIT_FAILURE;
        }

        if (encode_base64(pixels, raw_size, encoded, encoded_size + 1u) != 0) {
            stbi_image_free(pixels);
            free(encoded);
            fprintf(stderr, "_TERM_SPRITE_CACHE: failed to encode image data.\n");
            return EXIT_FAILURE;
        }

        stbi_image_free(pixels);
        sprite_width = image_w;
        sprite_height = image_h;
        data = encoded;
    } else if (sprite_literal) {
        if (parse_sprite_literal(sprite_literal, &sprite_width, &sprite_height, &literal_data) != 0) {
            return EXIT_FAILURE;
        }
        data = literal_data;
    } else if (data) {
        if (width <= 0 || height <= 0) {
            fprintf(stderr, "_TERM_SPRITE_CACHE: -data requires -width and -height.\n");
            print_usage();
            return EXIT_FAILURE;
        }
        sprite_width = (int)width;
        sprite_height = (int)height;
    } else {
        fprintf(stderr, "_TERM_SPRITE_CACHE: missing sprite source.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (sprite_width <= 0 || sprite_height <= 0 || !data) {
        fprintf(stderr, "_TERM_SPRITE_CACHE: invalid sprite data.\n");
        free(literal_data);
        free(encoded);
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;sprite_cache=add;sprite_id=%ld;sprite_cache_w=%d;sprite_cache_h=%d;sprite_cache_data=%s\a",
               id, sprite_width, sprite_height, data) < 0) {
        perror("_TERM_SPRITE_CACHE: printf");
        free(literal_data);
        free(encoded);
        return EXIT_FAILURE;
    }

    free(literal_data);
    free(encoded);

    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE_CACHE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
