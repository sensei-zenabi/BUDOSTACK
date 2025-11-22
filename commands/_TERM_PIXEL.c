#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pixel_spec {
    long x;
    long y;
    long r;
    long g;
    long b;
};

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "       _TERM_PIXEL --batch < stdin(lines: x y r g b)>\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window.\n");
    fprintf(stderr, "  --batch packs multiple pixel writes into a single OSC message for speed.\n");
}

static int parse_long(const char *arg, const char *name, long min_value, long max_value, long *out_value) {
    if (!arg || !out_value) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_TERM_PIXEL: invalid integer for %s: '%s'\\n", name, arg);
        return -1;
    }

    if (value < min_value || value > max_value) {
        fprintf(stderr, "_TERM_PIXEL: %s must be between %ld and %ld.\\n", name, min_value, max_value);
        return -1;
    }

    *out_value = value;
    return 0;
}

static int append_pixel(struct pixel_spec **pixels, size_t *count, size_t *capacity, struct pixel_spec value) {
    if (!pixels || !count || !capacity) {
        return -1;
    }

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0u) ? 64u : (*capacity * 2u);
        if (new_capacity < *capacity) {
            return -1;
        }
        struct pixel_spec *new_pixels = realloc(*pixels, new_capacity * sizeof(*new_pixels));
        if (!new_pixels) {
            return -1;
        }
        *pixels = new_pixels;
        *capacity = new_capacity;
    }

    (*pixels)[*count] = value;
    (*count)++;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int clear = 0;
    int batch = 0;
    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;
    struct pixel_spec *batch_pixels = NULL;
    size_t batch_count = 0u;
    size_t batch_capacity = 0u;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--clear") == 0) {
            clear = 1;
        } else if (strcmp(arg, "--batch") == 0) {
            batch = 1;
        } else if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -x.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &x) != 0) {
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -y.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &y) != 0) {
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-r") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -r.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-r", 0, 255, &r) != 0) {
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-g") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -g.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-g", 0, 255, &g) != 0) {
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "-b") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -b.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-b", 0, 255, &b) != 0) {
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "_TERM_PIXEL: unknown argument '%s'.\\n", arg);
            print_usage();
            free(batch_pixels);
            return EXIT_FAILURE;
        }
    }

    if (clear) {
        if (batch || x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --clear cannot be combined with draw arguments.\\n");
            free(batch_pixels);
            return EXIT_FAILURE;
        }
        if (printf("\\x1b]777;pixel=clear\\a") < 0) {
            perror("_TERM_PIXEL: printf");
            free(batch_pixels);
            return EXIT_FAILURE;
        }
    } else if (batch) {
        char line[256];
        while (fgets(line, sizeof(line), stdin)) {
            char *newline = strchr(line, '\n');
            if (newline) {
                *newline = '\0';
            }
            char *cursor = line;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            if (*cursor == '\0') {
                continue;
            }

            char *saveptr = NULL;
            char *token = strtok_r(cursor, " ,\t", &saveptr);
            long values[5] = {-1, -1, -1, -1, -1};
            size_t index = 0u;
            while (token && index < 5u) {
                long min_value = (index < 2u) ? 0 : 0;
                long max_value = (index < 2u) ? INT_MAX : 255;
                if (parse_long(token, "batch value", min_value, max_value, &values[index]) != 0) {
                    fprintf(stderr, "_TERM_PIXEL: invalid batch entry '%s'.\\n", token);
                    free(batch_pixels);
                    return EXIT_FAILURE;
                }
                index++;
                token = strtok_r(NULL, " ,\\t", &saveptr);
            }

            if (index != 5u || token) {
                fprintf(stderr, "_TERM_PIXEL: each batch line must contain five integers (x y r g b).\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }

            struct pixel_spec spec = {values[0], values[1], values[2], values[3], values[4]};
            if (append_pixel(&batch_pixels, &batch_count, &batch_capacity, spec) != 0) {
                fprintf(stderr, "_TERM_PIXEL: failed to store batch pixel.\\n");
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        }

        if (batch_count == 0u) {
            fprintf(stderr, "_TERM_PIXEL: --batch requires at least one pixel entry on stdin.\\n");
            free(batch_pixels);
            return EXIT_FAILURE;
        }

        char *payload = NULL;
        size_t payload_size = 0u;
        FILE *payload_stream = open_memstream(&payload, &payload_size);
        if (!payload_stream) {
            perror("_TERM_PIXEL: open_memstream");
            free(batch_pixels);
            return EXIT_FAILURE;
        }

        if (fprintf(payload_stream, "\\x1b]777;pixel=batch;pixels=") < 0) {
            perror("_TERM_PIXEL: fprintf");
            fclose(payload_stream);
            free(payload);
            free(batch_pixels);
            return EXIT_FAILURE;
        }

        for (size_t i = 0u; i < batch_count; i++) {
            const struct pixel_spec *spec = &batch_pixels[i];
            if (i > 0u) {
                fputc('|', payload_stream);
            }
            if (fprintf(payload_stream, "%ld,%ld,%ld,%ld,%ld", spec->x, spec->y, spec->r, spec->g, spec->b) < 0) {
                perror("_TERM_PIXEL: fprintf");
                fclose(payload_stream);
                free(payload);
                free(batch_pixels);
                return EXIT_FAILURE;
            }
        }

        if (fputs("\\a", payload_stream) == EOF) {
            perror("_TERM_PIXEL: fputs");
            fclose(payload_stream);
            free(payload);
            free(batch_pixels);
            return EXIT_FAILURE;
        }

        if (fclose(payload_stream) != 0) {
            perror("_TERM_PIXEL: fclose");
            free(payload);
            free(batch_pixels);
            return EXIT_FAILURE;
        }

        if (payload && fputs(payload, stdout) == EOF) {
            perror("_TERM_PIXEL: fputs");
            free(payload);
            free(batch_pixels);
            return EXIT_FAILURE;
        }
        free(payload);
    } else {
        if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
            fprintf(stderr, "_TERM_PIXEL: missing required draw arguments.\\n");
            print_usage();
            free(batch_pixels);
            return EXIT_FAILURE;
        }
        if (printf("\\x1b]777;pixel=draw;pixel_x=%ld;pixel_y=%ld;pixel_r=%ld;pixel_g=%ld;pixel_b=%ld\\a",
                   x,
                   y,
                   r,
                   g,
                   b) < 0) {
            perror("_TERM_PIXEL: printf");
            free(batch_pixels);
            return EXIT_FAILURE;
        }
    }

    free(batch_pixels);

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
