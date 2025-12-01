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
    fprintf(stderr, "Usage: _TERM_SPRITE -x <pixels> -y <pixels> -file <path> [options]\n");
    fprintf(stderr, "  Draws a PNG or BMP sprite onto the terminal's pixel surface.\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -mirrorX           Flip the sprite horizontally before rendering.\n");
    fprintf(stderr, "  -mirrorY           Flip the sprite vertically before rendering.\n");
    fprintf(stderr, "  -rotate <angle>    Rotate the sprite clockwise (0/90/180/270).\n");
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

static void mirror_horizontal(stbi_uc *pixels, int width, int height, int channels) {
    for (int y = 0; y < height; ++y) {
        stbi_uc *row = pixels + (size_t)y * (size_t)width * (size_t)channels;
        for (int x = 0; x < width / 2; ++x) {
            stbi_uc *left = row + (size_t)x * (size_t)channels;
            stbi_uc *right = row + (size_t)(width - 1 - x) * (size_t)channels;
            for (int c = 0; c < channels; ++c) {
                stbi_uc tmp = left[c];
                left[c] = right[c];
                right[c] = tmp;
            }
        }
    }
}

static void mirror_vertical(stbi_uc *pixels, int width, int height, int channels) {
    size_t row_bytes = (size_t)width * (size_t)channels;
    stbi_uc *tmp_row = malloc(row_bytes);
    if (!tmp_row) {
        fprintf(stderr, "_TERM_SPRITE: failed to allocate temporary row for vertical flip.\n");
        return;
    }

    for (int y = 0; y < height / 2; ++y) {
        stbi_uc *top = pixels + (size_t)y * row_bytes;
        stbi_uc *bottom = pixels + (size_t)(height - 1 - y) * row_bytes;
        memcpy(tmp_row, top, row_bytes);
        memcpy(top, bottom, row_bytes);
        memcpy(bottom, tmp_row, row_bytes);
    }

    free(tmp_row);
}

static stbi_uc *rotate_sprite(const stbi_uc *pixels, int width, int height, int channels, int rotation,
                              int *out_width, int *out_height) {
    int rot = rotation % 360;
    if (rot < 0) {
        rot += 360;
    }

    if (rot == 0) {
        *out_width = width;
        *out_height = height;
        stbi_uc *copy = malloc((size_t)width * (size_t)height * (size_t)channels);
        if (!copy) {
            fprintf(stderr, "_TERM_SPRITE: failed to allocate rotation buffer.\n");
            return NULL;
        }
        memcpy(copy, pixels, (size_t)width * (size_t)height * (size_t)channels);
        return copy;
    }

    int new_width = width;
    int new_height = height;
    if (rot == 90 || rot == 270) {
        new_width = height;
        new_height = width;
    }

    stbi_uc *rotated = malloc((size_t)new_width * (size_t)new_height * (size_t)channels);
    if (!rotated) {
        fprintf(stderr, "_TERM_SPRITE: failed to allocate rotation buffer.\n");
        return NULL;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const stbi_uc *src = pixels + ((size_t)y * (size_t)width + (size_t)x) * (size_t)channels;
            int dst_x = 0;
            int dst_y = 0;
            switch (rot) {
                case 90:
                    dst_x = height - 1 - y;
                    dst_y = x;
                    break;
                case 180:
                    dst_x = width - 1 - x;
                    dst_y = height - 1 - y;
                    break;
                case 270:
                    dst_x = y;
                    dst_y = width - 1 - x;
                    break;
                default:
                    free(rotated);
                    fprintf(stderr, "_TERM_SPRITE: unsupported rotation %d.\n", rot);
                    return NULL;
            }

            stbi_uc *dst = rotated + ((size_t)dst_y * (size_t)new_width + (size_t)dst_x) * (size_t)channels;
            memcpy(dst, src, (size_t)channels);
        }
    }

    *out_width = new_width;
    *out_height = new_height;
    return rotated;
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
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long origin_x = -1;
    long origin_y = -1;
    const char *file = NULL;
    bool mirror_x = false;
    bool mirror_y = false;
    int rotation = 0;
    long rotation_arg = 0;

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
        } else if (strcmp(arg, "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -file.\n");
                return EXIT_FAILURE;
            }
            file = argv[i];
        } else if (strcmp(arg, "-mirrorX") == 0) {
            mirror_x = true;
        } else if (strcmp(arg, "-mirrorY") == 0) {
            mirror_y = true;
        } else if (strcmp(arg, "-rotate") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_SPRITE: missing value for -rotate.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-rotate", 0, 270, &rotation_arg) != 0) {
                return EXIT_FAILURE;
            }
            rotation = (int)rotation_arg;
        } else {
            fprintf(stderr, "_TERM_SPRITE: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || file == NULL) {
        fprintf(stderr, "_TERM_SPRITE: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        fprintf(stderr, "_TERM_SPRITE: rotation must be 0, 90, 180, or 270.\n");
        return EXIT_FAILURE;
    }

    int width = 0;
    int height = 0;
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

    if (mirror_x) {
        mirror_horizontal(pixels, width, height, 4);
    }
    if (mirror_y) {
        mirror_vertical(pixels, width, height, 4);
    }

    int render_width = width;
    int render_height = height;
    stbi_uc *render_pixels = rotate_sprite(pixels, width, height, 4, rotation, &render_width, &render_height);
    stbi_image_free(pixels);
    if (!render_pixels) {
        return EXIT_FAILURE;
    }

    size_t width_sz = (size_t)render_width;
    size_t height_sz = (size_t)render_height;
    if (width_sz != 0 && height_sz > SIZE_MAX / width_sz) {
        free(render_pixels);
        fprintf(stderr, "_TERM_SPRITE: image dimensions overflow.\n");
        return EXIT_FAILURE;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        free(render_pixels);
        fprintf(stderr, "_TERM_SPRITE: image too large to encode.\n");
        return EXIT_FAILURE;
    }

    size_t raw_size = pixel_count * 4u;
    size_t encoded_size = base64_encoded_size(raw_size);
    if (encoded_size == 0 || encoded_size > SIZE_MAX - 1u) {
        free(render_pixels);
        fprintf(stderr, "_TERM_SPRITE: failed to compute encoded size.\n");
        return EXIT_FAILURE;
    }

    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        free(render_pixels);
        fprintf(stderr, "_TERM_SPRITE: failed to allocate %zu bytes for encoding.\n", encoded_size + 1u);
        return EXIT_FAILURE;
    }

    int encode_status = encode_base64(render_pixels, raw_size, encoded, encoded_size + 1u);
    free(render_pixels);
    if (encode_status != 0) {
        free(encoded);
        fprintf(stderr, "_TERM_SPRITE: failed to encode image data.\n");
        return EXIT_FAILURE;
    }

    int print_status =
        printf("\x1b]777;sprite=draw;sprite_x=%ld;sprite_y=%ld;sprite_w=%d;sprite_h=%d;sprite_data=%s\a",
               origin_x,
               origin_y,
               render_width,
               render_height,
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
