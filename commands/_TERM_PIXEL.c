#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "       _TERM_PIXEL --render\n");
    fprintf(stderr, "       _TERM_PIXEL --bulk <file|- >\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window.\n");
    fprintf(stderr, "  Bulk mode reads lines of 'x y r g b' (space-separated) from the file or stdin.\n");
}

struct pixel_bulk_entry {
    int32_t x;
    int32_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t reserved;
};

_Static_assert(sizeof(struct pixel_bulk_entry) == 12u,
               "pixel_bulk_entry must be 12 bytes");

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

static int read_bulk_pixels(const char *path, struct pixel_bulk_entry **out_entries, size_t *out_count) {
    if (!path || !out_entries || !out_count) {
        return -1;
    }

    *out_entries = NULL;
    *out_count = 0u;

    FILE *fp = NULL;
    int close_fp = 0;

    if (strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            perror("_TERM_PIXEL: fopen");
            return -1;
        }
        close_fp = 1;
    }

    char *line = NULL;
    size_t line_capacity = 0u;
    ssize_t line_length = 0;
    size_t capacity = 0u;
    size_t count = 0u;

    while ((line_length = getline(&line, &line_capacity, fp)) != -1) {
        (void)line_length;
        char *cursor = line;
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }

        if (*cursor == '\0' || *cursor == '#') {
            continue;
        }

        long values[5];
        if (sscanf(cursor, "%ld %ld %ld %ld %ld", &values[0], &values[1], &values[2], &values[3], &values[4]) != 5) {
            fprintf(stderr, "_TERM_PIXEL: invalid bulk pixel line: '%s'\n", cursor);
            free(line);
            free(*out_entries);
            *out_entries = NULL;
            if (close_fp && fp) {
                fclose(fp);
            }
            return -1;
        }

        if (values[0] < 0 || values[1] < 0 || values[0] > INT32_MAX || values[1] > INT32_MAX ||
            values[2] < 0 || values[2] > 255 || values[3] < 0 || values[3] > 255 || values[4] < 0 || values[4] > 255) {
            fprintf(stderr, "_TERM_PIXEL: bulk pixel values out of range.\n");
            free(line);
            free(*out_entries);
            *out_entries = NULL;
            if (close_fp && fp) {
                fclose(fp);
            }
            return -1;
        }

        if (count == capacity) {
            size_t new_capacity = (capacity == 0u) ? 64u : capacity * 2u;
            if (new_capacity < capacity) {
                fprintf(stderr, "_TERM_PIXEL: too many bulk pixels.\n");
                free(line);
                free(*out_entries);
                *out_entries = NULL;
                if (close_fp && fp) {
                    fclose(fp);
                }
                return -1;
            }
            struct pixel_bulk_entry *new_entries =
                realloc(*out_entries, new_capacity * sizeof(**out_entries));
            if (!new_entries) {
                perror("_TERM_PIXEL: realloc");
                free(line);
                free(*out_entries);
                *out_entries = NULL;
                if (close_fp && fp) {
                    fclose(fp);
                }
                return -1;
            }
            *out_entries = new_entries;
            capacity = new_capacity;
        }

        struct pixel_bulk_entry *entry = &(*out_entries)[count++];
        entry->x = (int32_t)values[0];
        entry->y = (int32_t)values[1];
        entry->r = (uint8_t)values[2];
        entry->g = (uint8_t)values[3];
        entry->b = (uint8_t)values[4];
        entry->reserved = 0u;
    }

    free(line);
    if (close_fp && fp) {
        fclose(fp);
    }

    if (count == 0u) {
        fprintf(stderr, "_TERM_PIXEL: no pixels read from bulk input.\n");
        free(*out_entries);
        *out_entries = NULL;
        return -1;
    }

    *out_count = count;
    return 0;
}

static int build_bulk_payload(const struct pixel_bulk_entry *entries, size_t count, uint8_t **out_data, size_t *out_size) {
    if (!entries || !out_data || !out_size || count == 0u) {
        return -1;
    }

    size_t entry_size = sizeof(struct pixel_bulk_entry);
    if (count > SIZE_MAX / entry_size) {
        return -1;
    }

    size_t payload_size = count * entry_size;
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        return -1;
    }

    uint8_t *cursor = payload;
    for (size_t i = 0u; i < count; i++) {
        int32_t x = entries[i].x;
        int32_t y = entries[i].y;
        cursor[0] = (uint8_t)(x & 0xFF);
        cursor[1] = (uint8_t)((x >> 8) & 0xFF);
        cursor[2] = (uint8_t)((x >> 16) & 0xFF);
        cursor[3] = (uint8_t)((uint32_t)x >> 24);
        cursor[4] = (uint8_t)(y & 0xFF);
        cursor[5] = (uint8_t)((y >> 8) & 0xFF);
        cursor[6] = (uint8_t)((y >> 16) & 0xFF);
        cursor[7] = (uint8_t)((uint32_t)y >> 24);
        cursor[8] = entries[i].r;
        cursor[9] = entries[i].g;
        cursor[10] = entries[i].b;
        cursor[11] = 0u;
        cursor += entry_size;
    }

    *out_data = payload;
    *out_size = payload_size;
    return 0;
}

static char *base64_encode(const uint8_t *data, size_t length) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (!data) {
        return NULL;
    }

    size_t output_length = 4u * ((length + 2u) / 3u);
    char *output = malloc(output_length + 1u);
    if (!output) {
        return NULL;
    }

    size_t out_index = 0u;
    for (size_t i = 0u; i < length; i += 3u) {
        uint32_t chunk = (uint32_t)data[i] << 16u;
        size_t remaining = length - i;
        if (remaining > 1u) {
            chunk |= (uint32_t)data[i + 1u] << 8u;
        }
        if (remaining > 2u) {
            chunk |= (uint32_t)data[i + 2u];
        }

        output[out_index++] = table[(chunk >> 18u) & 0x3Fu];
        output[out_index++] = table[(chunk >> 12u) & 0x3Fu];
        output[out_index++] = (remaining > 1u) ? table[(chunk >> 6u) & 0x3Fu] : '=';
        output[out_index++] = (remaining > 2u) ? table[chunk & 0x3Fu] : '=';
    }

    output[out_index] = '\0';
    return output;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int clear = 0;
    int render = 0;
    const char *bulk_path = NULL;
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
        } else if (strcmp(arg, "--bulk") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for --bulk.\n");
                return EXIT_FAILURE;
            }
            bulk_path = argv[i];
        } else {
            fprintf(stderr, "_TERM_PIXEL: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (bulk_path) {
        if (clear || render || x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --bulk cannot be combined with draw, --clear, or --render flags.\n");
            return EXIT_FAILURE;
        }

        struct pixel_bulk_entry *entries = NULL;
        size_t entry_count = 0u;
        if (read_bulk_pixels(bulk_path, &entries, &entry_count) != 0) {
            free(entries);
            return EXIT_FAILURE;
        }

        uint8_t *payload = NULL;
        size_t payload_size = 0u;
        if (build_bulk_payload(entries, entry_count, &payload, &payload_size) != 0) {
            fprintf(stderr, "_TERM_PIXEL: failed to prepare bulk payload.\n");
            free(entries);
            return EXIT_FAILURE;
        }

        char *encoded = base64_encode(payload, payload_size);
        free(payload);
        free(entries);
        if (!encoded) {
            fprintf(stderr, "_TERM_PIXEL: failed to encode bulk payload.\n");
            return EXIT_FAILURE;
        }

        if (printf("\x1b]777;pixel=bulk;pixel_count=%zu;pixel_data=%s\a", entry_count, encoded) < 0) {
            perror("_TERM_PIXEL: printf");
            free(encoded);
            return EXIT_FAILURE;
        }

        free(encoded);
    } else if (clear) {
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
