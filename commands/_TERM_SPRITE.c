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
            "Usage: _TERM_SPRITE -x <pixels> -y <pixels> (-file <path> | -sprite {w,h,\"data\"} | -data <base64> -width <px> -height <px>) [-layer <1-16>]\n");
    fprintf(stderr, "  Draws a PNG or BMP sprite onto the terminal's pixel surface.\n");
    fprintf(stderr, "  Layers are numbered 1 (top) through 16 (bottom). Defaults to 1.\n");
    fprintf(stderr, "  Use -sprite with the literal produced by _TERM_SPRITE_LOAD to avoid passing width/height separately.\n");
    fprintf(stderr, "  The base64 payload may be quoted ({w,h,\"data\"}) or unquoted ({w,h,data}).\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_SPRITE: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_SPRITE: %s must be between %ld and %ld.\n", name, min_value, max_value);
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
        fprintf(stderr, "_TERM_SPRITE: sprite literal must start with '{'.\n");
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
        fprintf(stderr, "_TERM_SPRITE: invalid sprite width in literal.\n");
        return -1;
    }

    p = endptr;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != ',') {
        fprintf(stderr, "_TERM_SPRITE: sprite literal missing comma after width.\n");
        return -1;
    }
    ++p;

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }

    errno = 0;
    long height = strtol(p, &endptr, 10);
    if (errno != 0 || endptr == p || height <= 0 || height > INT_MAX) {
        fprintf(stderr, "_TERM_SPRITE: invalid sprite height in literal.\n");
        return -1;
    }

    p = endptr;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != ',') {
        fprintf(stderr, "_TERM_SPRITE: sprite literal missing comma after height.\n");
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
            fprintf(stderr, "_TERM_SPRITE: sprite literal is missing the closing quote for data.\n");
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

    if (data_end == NULL || data_end <= data_start) {
        fprintf(stderr, "_TERM_SPRITE: sprite literal must contain base64 data.\n");
        return -1;
    }

    size_t data_len = (size_t)(data_end - data_start);
    char *data_copy = malloc(data_len + 1u);
    if (!data_copy) {
        fprintf(stderr, "_TERM_SPRITE: failed to allocate memory for sprite data.\n");
        return -1;
    }
    memcpy(data_copy, data_start, data_len);
    data_copy[data_len] = '\0';

    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '}') {
        free(data_copy);
        fprintf(stderr, "_TERM_SPRITE: sprite literal must end with '}'.\n");
        return -1;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '\0') {
        free(data_copy);
        fprintf(stderr, "_TERM_SPRITE: unexpected characters after sprite literal.\n");
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

    long origin_x = -1;
    long origin_y = -1;
    long layer = 1;
    long width_arg = -1;
    long height_arg = -1;
    const char *file = NULL;
    const char *data = NULL;
    const char *sprite_literal = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &origin_x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &origin_y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else if (strcmp(arg, "-sprite") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -sprite.\n");
                return EXIT_FAILURE;
            }
            sprite_literal = argv[i];
        } else if (strcmp(arg, "-data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -data.\n");
                return EXIT_FAILURE;
            }
            data = argv[i];
        } else if (strcmp(arg, "-width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -width.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-width", 1, INT_MAX, &width_arg) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -height.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-height", 1, INT_MAX, &height_arg) != 0) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_SPRITE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || (file == NULL && data == NULL && sprite_literal == NULL)) {
        fprintf(stderr, "_TERM_SPRITE: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if ((file != NULL) + (data != NULL) + (sprite_literal != NULL) > 1) {
        fprintf(stderr, "_TERM_SPRITE: specify only one of -file, -sprite, or -data.\n");
        return EXIT_FAILURE;
    }

    int width = 0;
    int height = 0;
    char *encoded = NULL;

    if (sprite_literal != NULL) {
        if (parse_sprite_literal(sprite_literal, &width, &height, &encoded) != 0) {
            return EXIT_FAILURE;
        }
    } else if (data != NULL) {
        if (width_arg <= 0 || height_arg <= 0) {
            fprintf(stderr, "_TERM_SPRITE: -width and -height are required when using -data.\n");
            return EXIT_FAILURE;
        }
        width = (int)width_arg;
        height = (int)height_arg;
        encoded = strdup(data);
        if (!encoded) {
            fprintf(stderr, "_TERM_SPRITE: failed to duplicate sprite data string.\n");
            return EXIT_FAILURE;
        }
    } else {
        int channels = 0;
        stbi_uc *pixels = stbi_load(file, &width, &height, &channels, 4);
        if (!pixels) {
            const char *reason = stbi_failure_reason();
            if (reason && *reason) {
                fprintf(stderr, "_TERM_SPRITE: failed to load '%s': %s\n", file, reason);
            } else {
                fprintf(stderr, "_TERM_SPRITE: failed to load '%s'\n", file);
            }
            return EXIT_FAILURE;
        }

        if (width <= 0 || height <= 0) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE: invalid image dimensions in '%s'\n", file);
            return EXIT_FAILURE;
        }

        size_t width_sz = (size_t)width;
        size_t height_sz = (size_t)height;
        if (width_sz != 0 && height_sz > SIZE_MAX / width_sz) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE: image dimensions overflow.\n");
            return EXIT_FAILURE;
        }

        size_t pixel_count = width_sz * height_sz;
        if (pixel_count > SIZE_MAX / 4u) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE: image too large to encode.\n");
            return EXIT_FAILURE;
        }

        size_t raw_size = pixel_count * 4u;
        size_t encoded_size = base64_encoded_size(raw_size);
        if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE: failed to compute encoded size.\n");
            return EXIT_FAILURE;
        }

        encoded = malloc(encoded_size + 1u);
        if (!encoded) {
            stbi_image_free(pixels);
            fprintf(stderr, "_TERM_SPRITE: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
            return EXIT_FAILURE;
        }

        int encode_status = encode_base64(pixels, raw_size, (char *)encoded, encoded_size + 1u);
        stbi_image_free(pixels);
        if (encode_status != 0) {
            free(encoded);
            fprintf(stderr, "_TERM_SPRITE: failed to encode image data.\n");
            return EXIT_FAILURE;
        }
    }

    int print_status = printf("\x1b]777;sprite=draw;sprite_x=%ld;sprite_y=%ld;sprite_w=%d;sprite_h=%d;sprite_layer=%ld;sprite_data=%s\a",
                              origin_x,
                              origin_y,
                              width,
                              height,
                              layer,
                              encoded);
    free(encoded);
    if (print_status < 0) {
        perror("_TERM_SPRITE: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_SPRITE: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
