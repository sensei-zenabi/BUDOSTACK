#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr,
            "Usage: _TERM_FRAME -x <pixels> -y <pixels> -width <px> -height <px> (-data <base64> | -raw <path>)\n");
    fprintf(stderr, "  Sends a raw RGBA frame to the terminal via OSC 777.\n");
    fprintf(stderr, "  Use -raw with a file that is width*height*4 bytes of RGBA.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_FRAME: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_FRAME: %s must be between %ld and %ld.\n", name, min_value, max_value);
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

static int read_raw_file(const char *path, uint8_t **out_data, size_t expected_size) {
    if (!path || !out_data) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("_TERM_FRAME: fopen");
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("_TERM_FRAME: fseek");
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("_TERM_FRAME: ftell");
        fclose(fp);
        return -1;
    }
    if ((size_t)file_size != expected_size) {
        fprintf(stderr, "_TERM_FRAME: raw file size mismatch (expected %zu bytes, got %ld).\n",
                expected_size, file_size);
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("_TERM_FRAME: fseek");
        fclose(fp);
        return -1;
    }

    uint8_t *data = malloc(expected_size);
    if (!data) {
        perror("_TERM_FRAME: malloc");
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(data, 1u, expected_size, fp);
    fclose(fp);
    if (read_bytes != expected_size) {
        fprintf(stderr, "_TERM_FRAME: failed to read raw frame data.\n");
        free(data);
        return -1;
    }

    *out_data = data;
    return 0;
}

int main(int argc, char **argv) {
    long x = 0;
    long y = 0;
    long width = -1;
    long height = -1;
    const char *data = NULL;
    const char *raw_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            if (parse_long(argv[++i], "x", 0, INT_MAX, &x) != 0) {
                return 1;
            }
        } else if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            if (parse_long(argv[++i], "y", 0, INT_MAX, &y) != 0) {
                return 1;
            }
        } else if (strcmp(argv[i], "-width") == 0 && i + 1 < argc) {
            if (parse_long(argv[++i], "width", 1, INT_MAX, &width) != 0) {
                return 1;
            }
        } else if (strcmp(argv[i], "-height") == 0 && i + 1 < argc) {
            if (parse_long(argv[++i], "height", 1, INT_MAX, &height) != 0) {
                return 1;
            }
        } else if (strcmp(argv[i], "-data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "-raw") == 0 && i + 1 < argc) {
            raw_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            fprintf(stderr, "_TERM_FRAME: unknown option '%s'.\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (width <= 0 || height <= 0) {
        print_usage();
        return 1;
    }
    if ((!data && !raw_path) || (data && raw_path)) {
        fprintf(stderr, "_TERM_FRAME: choose exactly one of -data or -raw.\n");
        print_usage();
        return 1;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / 4u) {
        fprintf(stderr, "_TERM_FRAME: frame dimensions too large.\n");
        return 1;
    }
    size_t raw_size = pixel_count * 4u;

    char *encoded = NULL;
    uint8_t *raw_pixels = NULL;
    if (raw_path) {
        if (read_raw_file(raw_path, &raw_pixels, raw_size) != 0) {
            return 1;
        }
        size_t encoded_size = base64_encoded_size(raw_size);
        encoded = malloc(encoded_size + 1u);
        if (!encoded) {
            perror("_TERM_FRAME: malloc");
            free(raw_pixels);
            return 1;
        }
        if (encode_base64(raw_pixels, raw_size, encoded, encoded_size + 1u) != 0) {
            fprintf(stderr, "_TERM_FRAME: failed to encode raw data.\n");
            free(encoded);
            free(raw_pixels);
            return 1;
        }
        data = encoded;
    }

    if (printf("\x1b]777;frame=draw;frame_x=%ld;frame_y=%ld;frame_w=%ld;frame_h=%ld;frame_data=%s\a",
               x, y, width, height, data) < 0) {
        fprintf(stderr, "_TERM_FRAME: failed to write OSC 777 sequence.\n");
        free(encoded);
        free(raw_pixels);
        return 1;
    }

    free(encoded);
    free(raw_pixels);
    return 0;
}
