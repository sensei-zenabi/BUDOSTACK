#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TERM_PIXEL_BUFFER_PATH
#define TERM_PIXEL_BUFFER_PATH "/tmp/budostack_term_pixel_buffer.bin"
#endif

struct term_pixel_record {
    int32_t x;
    int32_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static int buffer_pipeline_active(void) {
    FILE *file = fopen(TERM_PIXEL_BUFFER_PATH, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

static int buffer_pipeline_open(void) {
    FILE *file = fopen(TERM_PIXEL_BUFFER_PATH, "wb");
    if (!file) {
        perror("_TERM_PIXEL: fopen");
        return -1;
    }
    if (fclose(file) != 0) {
        perror("_TERM_PIXEL: fclose");
        return -1;
    }
    return 0;
}

static int buffer_pipeline_append(const struct term_pixel_record *record) {
    if (!record) {
        return -1;
    }
    FILE *file = fopen(TERM_PIXEL_BUFFER_PATH, "ab");
    if (!file) {
        return -1;
    }
    uint8_t encoded[sizeof(*record)] = {0};
    encoded[0] = (uint8_t)(record->x & 0xff);
    encoded[1] = (uint8_t)((record->x >> 8) & 0xff);
    encoded[2] = (uint8_t)((record->x >> 16) & 0xff);
    encoded[3] = (uint8_t)((record->x >> 24) & 0xff);
    encoded[4] = (uint8_t)(record->y & 0xff);
    encoded[5] = (uint8_t)((record->y >> 8) & 0xff);
    encoded[6] = (uint8_t)((record->y >> 16) & 0xff);
    encoded[7] = (uint8_t)((record->y >> 24) & 0xff);
    encoded[8] = record->r;
    encoded[9] = record->g;
    encoded[10] = record->b;

    size_t written = fwrite(encoded, 1u, sizeof(encoded), file);
    if (fclose(file) != 0) {
        perror("_TERM_PIXEL: fclose");
        return -1;
    }
    return (written == sizeof(encoded)) ? 0 : -1;
}

static unsigned char *buffer_pipeline_read(size_t *out_size) {
    if (!out_size) {
        return NULL;
    }
    FILE *file = fopen(TERM_PIXEL_BUFFER_PATH, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    size_t buffer_size = (size_t)length;
    if (buffer_size == 0u) {
        fclose(file);
        *out_size = 0u;
        return NULL;
    }
    unsigned char *buffer = malloc(buffer_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1u, buffer_size, file);
    if (fclose(file) != 0) {
        perror("_TERM_PIXEL: fclose");
        free(buffer);
        return NULL;
    }
    if (read != buffer_size) {
        free(buffer);
        return NULL;
    }
    *out_size = buffer_size;
    return buffer;
}

static int buffer_pipeline_clear(void) {
    FILE *file = fopen(TERM_PIXEL_BUFFER_PATH, "wb");
    if (!file) {
        return -1;
    }
    if (fclose(file) != 0) {
        perror("_TERM_PIXEL: fclose");
        return -1;
    }
    return 0;
}

static char base64_encode_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *encode_base64(const unsigned char *data, size_t length) {
    if (!data || length == 0u) {
        return NULL;
    }
    size_t output_length = ((length + 2u) / 3u) * 4u;
    char *encoded = malloc(output_length + 1u);
    if (!encoded) {
        return NULL;
    }
    size_t out_index = 0u;
    for (size_t i = 0u; i < length; i += 3u) {
        uint32_t triple = (uint32_t)data[i] << 16u;
        if ((i + 1u) < length) {
            triple |= (uint32_t)data[i + 1u] << 8u;
        }
        if ((i + 2u) < length) {
            triple |= data[i + 2u];
        }
        encoded[out_index++] = base64_encode_table[(triple >> 18u) & 0x3fu];
        encoded[out_index++] = base64_encode_table[(triple >> 12u) & 0x3fu];
        encoded[out_index++] = (i + 1u < length)
            ? base64_encode_table[(triple >> 6u) & 0x3fu]
            : '=';
        encoded[out_index++] = (i + 2u < length)
            ? base64_encode_table[triple & 0x3fu]
            : '=';
    }
    encoded[out_index] = '\0';
    return encoded;
}

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "       _TERM_PIXEL --render\n");
    fprintf(stderr, "       _TERM_PIXEL --open\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_PIXEL: invalid integer for %s: '%s'\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_PIXEL: %s must be between %ld and %ld.\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int clear = 0;
    int render = 0;
    int open = 0;
    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--clear") == 0) {
            clear = 1;
        } else if (strcmp(arg, "--open") == 0) {
            open = 1;
        } else if (strcmp(arg, "--render") == 0) {
            render = 1;
        } else if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &x) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &y) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-r") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -r.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-r", 0, 255, &r) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-g") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -g.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-g", 0, 255, &g) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-b") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -b.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-b", 0, 255, &b) != 0) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_PIXEL: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (open) {
        if (clear || render || x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --open cannot be combined with other arguments.\n");
            return EXIT_FAILURE;
        }
        if (buffer_pipeline_open() != 0) {
            fprintf(stderr, "_TERM_PIXEL: Failed to open pixel buffer.\n");
            return EXIT_FAILURE;
        }
    } else if (clear) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --clear cannot be combined with draw arguments.\n");
            return EXIT_FAILURE;
        }
        (void)buffer_pipeline_clear();
        if (printf("\x1b]777;pixel=clear\a") < 0) {
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }
    } else if (render) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --render cannot be combined with draw arguments.\n");
            return EXIT_FAILURE;
        }
        if (buffer_pipeline_active()) {
            size_t buffer_size = 0u;
            unsigned char *buffer = buffer_pipeline_read(&buffer_size);
            if (!buffer) {
                buffer_size = 0u;
            }
            size_t pixel_count = (buffer_size / sizeof(struct term_pixel_record));
            if (buffer_size % sizeof(struct term_pixel_record) != 0u) {
                fprintf(stderr, "_TERM_PIXEL: Pixel buffer is corrupted.\n");
                free(buffer);
                return EXIT_FAILURE;
            }
            if (pixel_count > 0u) {
                char *encoded = encode_base64(buffer, buffer_size);
                free(buffer);
                if (!encoded) {
                    fprintf(stderr, "_TERM_PIXEL: Failed to encode pixel buffer.\n");
                    return EXIT_FAILURE;
                }
                if (printf("\x1b]777;pixel=batch;pixel_count=%zu;pixel_data=%s\a",
                           pixel_count,
                           encoded) < 0) {
                    perror("_TERM_PIXEL: printf");
                    free(encoded);
                    return EXIT_FAILURE;
                }
                free(encoded);
            } else {
                if (printf("\x1b]777;pixel=render\a") < 0) {
                    perror("_TERM_PIXEL: printf");
                    free(buffer);
                    return EXIT_FAILURE;
                }
                free(buffer);
            }
            if (buffer_pipeline_clear() != 0) {
                fprintf(stderr, "_TERM_PIXEL: Failed to clear pixel buffer.\n");
                return EXIT_FAILURE;
            }
        } else {
            if (printf("\x1b]777;pixel=render\a") < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
    } else {
        if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
            fprintf(stderr, "_TERM_PIXEL: missing required draw arguments.\n");
            print_usage();
            return EXIT_FAILURE;
        }
        if (buffer_pipeline_active()) {
            struct term_pixel_record record = {0};
            record.x = (int32_t)x;
            record.y = (int32_t)y;
            record.r = (uint8_t)r;
            record.g = (uint8_t)g;
            record.b = (uint8_t)b;
            if (buffer_pipeline_append(&record) != 0) {
                fprintf(stderr, "_TERM_PIXEL: Failed to append to pixel buffer.\n");
                return EXIT_FAILURE;
            }
        } else {
            if (printf("\x1b]777;pixel=draw;pixel_x=%ld;pixel_y=%ld;pixel_r=%ld;pixel_g=%ld;pixel_b=%ld\a",
                       x,
                       y,
                       r,
                       g,
                       b) < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
