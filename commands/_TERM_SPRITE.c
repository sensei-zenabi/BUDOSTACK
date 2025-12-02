#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/stb_image.h"

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SPRITE -x <pixels> -y <pixels> (-file <path> | -sprite <blob>) [-layer <1-16>]\n");
    fprintf(stderr, "  Draws a PNG or BMP sprite onto the terminal's pixel surface.\n");
    fprintf(stderr, "  Layers are numbered 1 (top) through 16 (bottom). Defaults to 1 or the blob layer.\n");
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

static int base64_decode_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    if (c == '=') {
        return -2;
    }
    return -1;
}

static int base64_decoded_size(const char *input, size_t *out_size) {
    if (!input || !out_size) {
        return -1;
    }

    size_t len = strlen(input);
    if (len == 0 || (len % 4u) != 0u) {
        return -1;
    }

    size_t padding = 0u;
    if (len >= 1u && input[len - 1u] == '=') {
        padding++;
    }
    if (len >= 2u && input[len - 2u] == '=') {
        padding++;
    }

    size_t blocks = len / 4u;
    if (blocks > (SIZE_MAX - padding) / 3u) {
        return -1;
    }

    *out_size = blocks * 3u - padding;
    return 0;
}

static int base64_decode(const char *input, uint8_t *out, size_t out_size, size_t *decoded_size) {
    if (!input || !out) {
        return -1;
    }

    size_t expected_size = 0u;
    if (base64_decoded_size(input, &expected_size) != 0) {
        return -1;
    }

    if (out_size < expected_size) {
        return -1;
    }

    size_t len = strlen(input);
    size_t out_idx = 0u;
    for (size_t i = 0u; i < len; i += 4u) {
        int v0 = base64_decode_value(input[i]);
        int v1 = base64_decode_value(input[i + 1u]);
        int v2 = base64_decode_value(input[i + 2u]);
        int v3 = base64_decode_value(input[i + 3u]);
        if (v0 < 0 || v1 < 0 || v2 < -1 || v3 < -1) {
            return -1;
        }

        if ((v2 == -2 || v3 == -2) && (i + 4u) != len) {
            return -1;
        }

        uint32_t block = 0u;
        block |= (uint32_t)v0 << 18;
        block |= (uint32_t)v1 << 12;
        if (v2 >= 0) {
            block |= (uint32_t)v2 << 6;
        }
        if (v3 >= 0) {
            block |= (uint32_t)v3;
        }

        size_t bytes_to_write = 3u;
        if (v3 == -2) {
            bytes_to_write = 2u;
        }
        if (v2 == -2) {
            bytes_to_write = 1u;
        }

        if (out_idx + bytes_to_write > out_size) {
            return -1;
        }

        out[out_idx++] = (uint8_t)((block >> 16) & 0xFFu);
        if (bytes_to_write > 1u) {
            out[out_idx++] = (uint8_t)((block >> 8) & 0xFFu);
        }
        if (bytes_to_write > 2u) {
            out[out_idx++] = (uint8_t)(block & 0xFFu);
        }
    }

    if (decoded_size) {
        *decoded_size = out_idx;
    }
    return 0;
}

static uint32_t read_u32_le(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long origin_x = -1;
    long origin_y = -1;
    long layer = 1;
    bool layer_set = false;
    const char *file = NULL;
    const char *sprite_blob = NULL;

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
            layer_set = true;
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
            sprite_blob = argv[i];
        } else {
            fprintf(stderr, "_TERM_SPRITE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || (file == NULL && sprite_blob == NULL) || (file && sprite_blob)) {
        fprintf(stderr, "_TERM_SPRITE: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    int width = 0;
    int height = 0;
    size_t raw_size = 0u;
    const uint8_t *pixel_data = NULL;
    stbi_uc *pixels = NULL;
    uint8_t *decoded_blob = NULL;
    if (file) {
        int channels = 0;
        pixels = stbi_load(file, &width, &height, &channels, 4);
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

        raw_size = pixel_count * 4u;
        pixel_data = pixels;
    } else {
        size_t decoded_size = 0u;
        if (base64_decoded_size(sprite_blob, &decoded_size) != 0) {
            fprintf(stderr, "_TERM_SPRITE: invalid sprite blob.\n");
            return EXIT_FAILURE;
        }

        const size_t header_size = 12u;
        decoded_blob = malloc(decoded_size);
        if (!decoded_blob) {
            fprintf(stderr, "_TERM_SPRITE: failed to allocate %zu bytes for sprite.\n", decoded_size);
            return EXIT_FAILURE;
        }

        size_t written = 0u;
        if (base64_decode(sprite_blob, decoded_blob, decoded_size, &written) != 0 || written < header_size) {
            free(decoded_blob);
            fprintf(stderr, "_TERM_SPRITE: failed to decode sprite blob.\n");
            return EXIT_FAILURE;
        }

        width = (int)read_u32_le(decoded_blob);
        height = (int)read_u32_le(decoded_blob + 4u);
        uint32_t blob_layer = read_u32_le(decoded_blob + 8u);

        if (width <= 0 || height <= 0) {
            free(decoded_blob);
            fprintf(stderr, "_TERM_SPRITE: sprite blob contains invalid dimensions.\n");
            return EXIT_FAILURE;
        }

        size_t width_sz = (size_t)width;
        size_t height_sz = (size_t)height;
        if (width_sz != 0 && height_sz > SIZE_MAX / width_sz) {
            free(decoded_blob);
            fprintf(stderr, "_TERM_SPRITE: sprite blob dimensions overflow.\n");
            return EXIT_FAILURE;
        }

        size_t pixel_count = width_sz * height_sz;
        if (pixel_count > SIZE_MAX / 4u) {
            free(decoded_blob);
            fprintf(stderr, "_TERM_SPRITE: sprite blob too large to encode.\n");
            return EXIT_FAILURE;
        }

        raw_size = pixel_count * 4u;
        size_t expected_total = header_size + raw_size;
        if (expected_total != written) {
            free(decoded_blob);
            fprintf(stderr, "_TERM_SPRITE: sprite blob size mismatch.\n");
            return EXIT_FAILURE;
        }

        if (!layer_set) {
            long layer_value = (long)blob_layer;
            if (layer_value < 1 || layer_value > 16) {
                free(decoded_blob);
                fprintf(stderr, "_TERM_SPRITE: sprite blob contains invalid layer %ld.\n", layer_value);
                return EXIT_FAILURE;
            }
            layer = layer_value;
        }

        pixel_data = decoded_blob + header_size;
    }
    if (!pixel_data) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        free(decoded_blob);
        fprintf(stderr, "_TERM_SPRITE: missing sprite data.\n");
        return EXIT_FAILURE;
    }

    size_t encoded_size = base64_encoded_size(raw_size);
    if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        free(decoded_blob);
        fprintf(stderr, "_TERM_SPRITE: failed to compute encoded size.\n");
        return EXIT_FAILURE;
    }

    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        free(decoded_blob);
        fprintf(stderr, "_TERM_SPRITE: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
        return EXIT_FAILURE;
    }

    int encode_status = encode_base64(pixel_data, raw_size, encoded, encoded_size + 1u);
    if (pixels) {
        stbi_image_free(pixels);
    }
    free(decoded_blob);
    if (encode_status != 0) {
        free(encoded);
        fprintf(stderr, "_TERM_SPRITE: failed to encode image data.\n");
        return EXIT_FAILURE;
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
