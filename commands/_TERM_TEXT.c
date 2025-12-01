#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_TEXT -x <pixels> -y <pixels> -text <string> -color <1-18> [-layer <1-16>]\n");
    fprintf(stderr, "  Renders UTF-8 text on the terminal's pixel surface using the system font.\n");
    fprintf(stderr, "  Colors are chosen from the active 18-color scheme. Default layer is 1 (top).\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_TEXT: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_TEXT: %s must be between %ld and %ld.\n", name, min_value, max_value);
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

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    long origin_x = -1;
    long origin_y = -1;
    long layer = 1;
    long color_index = -1;
    const char *text = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_TEXT: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &origin_x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_TEXT: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &origin_y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-layer") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_TEXT: missing value for -layer.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-layer", 1, 16, &layer) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_TEXT: missing value for -color.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-color", 1, 18, &color_index) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-text") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_TEXT: missing value for -text.\n");
                return EXIT_FAILURE;
            }
            text = argv[i];
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_TERM_TEXT: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (origin_x < 0 || origin_y < 0 || !text || color_index < 1 || color_index > 18) {
        fprintf(stderr, "_TERM_TEXT: missing required arguments.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    size_t text_len = strlen(text);
    if (text_len == 0u) {
        fprintf(stderr, "_TERM_TEXT: text must not be empty.\n");
        return EXIT_FAILURE;
    }

    size_t encoded_len = base64_encoded_size(text_len);
    if (encoded_len == 0 || encoded_len > SIZE_MAX - 1u) {
        fprintf(stderr, "_TERM_TEXT: failed to compute encoded size.\n");
        return EXIT_FAILURE;
    }

    char *encoded = malloc(encoded_len + 1u);
    if (!encoded) {
        fprintf(stderr, "_TERM_TEXT: failed to allocate %zu bytes for encoding.\n", encoded_len + 1u);
        return EXIT_FAILURE;
    }

    if (encode_base64((const uint8_t *)text, text_len, encoded, encoded_len + 1u) != 0) {
        free(encoded);
        fprintf(stderr, "_TERM_TEXT: failed to encode text.\n");
        return EXIT_FAILURE;
    }

    int print_status = printf("\x1b]777;text=draw;text_x=%ld;text_y=%ld;text_layer=%ld;text_color=%ld;text_data=%s\a",
                              origin_x,
                              origin_y,
                              layer,
                              color_index,
                              encoded);
    free(encoded);

    if (print_status < 0) {
        perror("_TERM_TEXT: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_TEXT: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
