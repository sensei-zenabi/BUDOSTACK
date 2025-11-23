#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pixel_entry {
    int32_t x;
    int32_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const char *pixel_buffer_path(void) {
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') {
        tmpdir = "/tmp";
    }
    return tmpdir;
}

static int build_pixel_path(char *buffer, size_t buffer_size) {
    const char *tmpdir = pixel_buffer_path();
    int written = snprintf(buffer, buffer_size, "%s/_term_pixel_buffer.bin", tmpdir);
    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "_TERM_PIXEL: failed to build buffer path.\n");
        return -1;
    }
    return 0;
}

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "       _TERM_PIXEL --render\n");
    fprintf(stderr, "       _TERM_PIXEL --queue -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --flush [--keep]\n");
    fprintf(stderr, "       _TERM_PIXEL --memory-clear\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window.\n");
    fprintf(stderr, "  Use --queue repeatedly to stage pixels in memory, then --flush to send\n");
    fprintf(stderr, "  them to apps/terminal in a single bulk update for 30FPS TASK scripts.\n");
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

static int load_pixel_buffer(struct pixel_entry **out_pixels, size_t *out_count) {
    if (!out_pixels || !out_count) {
        return -1;
    }

    *out_pixels = NULL;
    *out_count = 0u;

    char path[PATH_MAX];
    if (build_pixel_path(path, sizeof(path)) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    uint32_t count = 0u;
    size_t read_count = fread(&count, sizeof(count), 1u, fp);
    if (read_count != 1u) {
        fclose(fp);
        return -1;
    }

    if (count == 0u) {
        fclose(fp);
        return 0;
    }

    struct pixel_entry *pixels = malloc(sizeof(*pixels) * count);
    if (!pixels) {
        fclose(fp);
        return -1;
    }

    size_t loaded = fread(pixels, sizeof(*pixels), count, fp);
    fclose(fp);
    if (loaded != count) {
        free(pixels);
        return -1;
    }

    *out_pixels = pixels;
    *out_count = count;
    return 0;
}

static int save_pixel_buffer(const struct pixel_entry *pixels, size_t count) {
    char path[PATH_MAX];
    if (build_pixel_path(path, sizeof(path)) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror("_TERM_PIXEL: fopen");
        return -1;
    }

    uint32_t stored = (count > UINT32_MAX) ? UINT32_MAX : (uint32_t)count;
    size_t written = fwrite(&stored, sizeof(stored), 1u, fp);
    if (written != 1u) {
        perror("_TERM_PIXEL: fwrite");
        fclose(fp);
        return -1;
    }

    if (stored > 0u) {
        size_t entry_written = fwrite(pixels, sizeof(*pixels), stored, fp);
        if (entry_written != stored) {
            perror("_TERM_PIXEL: fwrite");
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        perror("_TERM_PIXEL: fclose");
        return -1;
    }

    return 0;
}

static int clear_pixel_buffer(void) {
    char path[PATH_MAX];
    if (build_pixel_path(path, sizeof(path)) != 0) {
        return -1;
    }
    if (remove(path) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        perror("_TERM_PIXEL: remove");
        return -1;
    }
    return 0;
}

static int append_pixel_to_buffer(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) {
    struct pixel_entry *pixels = NULL;
    size_t count = 0u;
    if (load_pixel_buffer(&pixels, &count) != 0) {
        free(pixels);
        return -1;
    }

    struct pixel_entry *grown = realloc(pixels, sizeof(*pixels) * (count + 1u));
    if (!grown) {
        free(pixels);
        return -1;
    }

    pixels = grown;
    pixels[count].x = x;
    pixels[count].y = y;
    pixels[count].r = r;
    pixels[count].g = g;
    pixels[count].b = b;
    count++;

    if (save_pixel_buffer(pixels, count) != 0) {
        free(pixels);
        return -1;
    }

    free(pixels);
    return 0;
}

static char hex_digit(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    return digits[value & 0x0Fu];
}

static int emit_bulk_payload(const struct pixel_entry *pixels, size_t count, int render) {
    if (count > SIZE_MAX / 11u) {
        fprintf(stderr, "_TERM_PIXEL: too many staged pixels.\n");
        return -1;
    }

    size_t payload_bytes = count * 11u;
    size_t hex_length = payload_bytes * 2u + 1u;
    char *hex = malloc(hex_length);
    if (!hex) {
        return -1;
    }

    size_t offset = 0u;
    for (size_t i = 0u; i < count; i++) {
        const struct pixel_entry *entry = &pixels[i];
        uint8_t packed[11];
        packed[0] = (uint8_t)(entry->x & 0xFF);
        packed[1] = (uint8_t)((entry->x >> 8) & 0xFF);
        packed[2] = (uint8_t)((entry->x >> 16) & 0xFF);
        packed[3] = (uint8_t)((entry->x >> 24) & 0xFF);
        packed[4] = (uint8_t)(entry->y & 0xFF);
        packed[5] = (uint8_t)((entry->y >> 8) & 0xFF);
        packed[6] = (uint8_t)((entry->y >> 16) & 0xFF);
        packed[7] = (uint8_t)((entry->y >> 24) & 0xFF);
        packed[8] = entry->r;
        packed[9] = entry->g;
        packed[10] = entry->b;
        for (size_t j = 0u; j < sizeof(packed); j++) {
            uint8_t byte = packed[j];
            hex[offset++] = hex_digit((uint8_t)(byte >> 4));
            hex[offset++] = hex_digit(byte);
        }
    }

    hex[offset] = '\0';

    int printed = printf("\x1b]777;pixel=bulk;pixel_format=xy_rgb8_le;pixel_count=%zu;pixel_render=%d;pixel_data=%s\a",
                         count,
                         render ? 1 : 0,
                         hex);
    free(hex);
    if (printed < 0) {
        perror("_TERM_PIXEL: printf");
        return -1;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int clear = 0;
    int render = 0;
    int queue_mode = 0;
    int flush_mode = 0;
    int keep_buffer = 0;
    int memory_clear = 0;
    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--clear") == 0) {
            clear = 1;
        } else if (strcmp(arg, "--render") == 0) {
            render = 1;
        } else if (strcmp(arg, "--queue") == 0) {
            queue_mode = 1;
        } else if (strcmp(arg, "--flush") == 0) {
            flush_mode = 1;
        } else if (strcmp(arg, "--keep") == 0) {
            keep_buffer = 1;
        } else if (strcmp(arg, "--memory-clear") == 0) {
            memory_clear = 1;
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

    if (memory_clear) {
        if (clear_pixel_buffer() != 0) {
            return EXIT_FAILURE;
        }
        if (!queue_mode && !flush_mode && !clear && !render && x < 0 && y < 0 && r < 0 && g < 0 && b < 0) {
            return EXIT_SUCCESS;
        }
    }

    if (queue_mode) {
        if (flush_mode || clear || render) {
            fprintf(stderr, "_TERM_PIXEL: --queue cannot be combined with --flush, --clear, or --render.\n");
            return EXIT_FAILURE;
        }
        if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
            fprintf(stderr, "_TERM_PIXEL: --queue requires -x, -y, -r, -g, and -b.\n");
            return EXIT_FAILURE;
        }
        if (append_pixel_to_buffer((int32_t)x, (int32_t)y, (uint8_t)r, (uint8_t)g, (uint8_t)b) != 0) {
            fprintf(stderr, "_TERM_PIXEL: failed to queue pixel.\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (flush_mode) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --flush does not accept draw arguments.\n");
            return EXIT_FAILURE;
        }
        if (clear || render) {
            fprintf(stderr, "_TERM_PIXEL: --flush cannot be combined with --clear or --render.\n");
            return EXIT_FAILURE;
        }

        struct pixel_entry *pixels = NULL;
        size_t count = 0u;
        if (load_pixel_buffer(&pixels, &count) != 0) {
            free(pixels);
            fprintf(stderr, "_TERM_PIXEL: failed to read queued pixels.\n");
            return EXIT_FAILURE;
        }

        if (emit_bulk_payload(pixels, count, 1) != 0) {
            free(pixels);
            return EXIT_FAILURE;
        }

        if (!keep_buffer) {
            clear_pixel_buffer();
        }

        free(pixels);
        return EXIT_SUCCESS;
    }

    if (clear) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --clear cannot be combined with draw arguments.\n");
            return EXIT_FAILURE;
        }
        if (printf("\x1b]777;pixel=clear\a") < 0) {
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }
    } else if (render) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --render cannot be combined with draw arguments.\n");
            return EXIT_FAILURE;
        }
        if (printf("\x1b]777;pixel=render\a") < 0) {
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }
    } else {
        if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
            fprintf(stderr, "_TERM_PIXEL: missing required draw arguments.\n");
            print_usage();
            return EXIT_FAILURE;
        }
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

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
