#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255> [repeated]\n");
    fprintf(stderr, "       _TERM_PIXEL --stdin\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window. Multiple -x/-y/-r/-g/-b\n");
    fprintf(stderr, "  groups or stdin rows (x y r g b) are combined into a single OSC payload.\n");
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

struct pixel_spec {
    long x;
    long y;
    long r;
    long g;
    long b;
    int have_x;
    int have_y;
    int have_r;
    int have_g;
    int have_b;
};

static void reset_pixel_spec(struct pixel_spec *spec) {
    if (!spec) {
        return;
    }
    spec->x = 0;
    spec->y = 0;
    spec->r = 0;
    spec->g = 0;
    spec->b = 0;
    spec->have_x = 0;
    spec->have_y = 0;
    spec->have_r = 0;
    spec->have_g = 0;
    spec->have_b = 0;
}

static int pixel_spec_complete(const struct pixel_spec *spec) {
    return spec && spec->have_x && spec->have_y && spec->have_r && spec->have_g && spec->have_b;
}

static int pixel_spec_has_any(const struct pixel_spec *spec) {
    return spec && (spec->have_x || spec->have_y || spec->have_r || spec->have_g || spec->have_b);
}

static int append_pixel_spec(struct pixel_spec **list, size_t *count, size_t *capacity, const struct pixel_spec *spec) {
    if (!list || !count || !capacity || !spec) {
        return -1;
    }

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0u) ? 8u : (*capacity * 2u);
        if (new_capacity < *capacity) {
            return -1;
        }
        struct pixel_spec *new_list = realloc(*list, new_capacity * sizeof(*new_list));
        if (!new_list) {
            return -1;
        }
        *list = new_list;
        *capacity = new_capacity;
    }

    (*list)[*count] = *spec;
    (*count)++;
    return 0;
}

static int append_format(char **buffer, size_t *length, size_t *capacity, const char *fmt, ...) {
    if (!buffer || !length || !capacity || !fmt) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return -1;
    }

    size_t required = (size_t)needed;
    if (*length + required + 1u > *capacity) {
        size_t new_capacity = *capacity == 0u ? 256u : *capacity;
        while (*length + required + 1u > new_capacity) {
            if (new_capacity > SIZE_MAX / 2u) {
                return -1;
            }
            new_capacity *= 2u;
        }
        char *new_buffer = realloc(*buffer, new_capacity);
        if (!new_buffer) {
            return -1;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    va_start(args, fmt);
    int written = vsnprintf(*buffer + *length, *capacity - *length, fmt, args);
    va_end(args);
    if (written < 0) {
        return -1;
    }

    *length += (size_t)written;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int clear = 0;
    int read_stdin = 0;
    struct pixel_spec *pixel_specs = NULL;
    size_t pixel_spec_count = 0u;
    size_t pixel_spec_capacity = 0u;
    struct pixel_spec current_spec;
    reset_pixel_spec(&current_spec);
    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--clear") == 0) {
            clear = 1;
        } else if (strcmp(arg, "--stdin") == 0) {
            read_stdin = 1;
        } else if (strcmp(arg, "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -x.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-x", 0, INT_MAX, &x) != 0) {
                return EXIT_FAILURE;
            }
            current_spec.x = x;
            current_spec.have_x = 1;
        } else if (strcmp(arg, "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -y.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-y", 0, INT_MAX, &y) != 0) {
                return EXIT_FAILURE;
            }
            current_spec.y = y;
            current_spec.have_y = 1;
        } else if (strcmp(arg, "-r") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -r.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-r", 0, 255, &r) != 0) {
                return EXIT_FAILURE;
            }
            current_spec.r = r;
            current_spec.have_r = 1;
        } else if (strcmp(arg, "-g") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -g.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-g", 0, 255, &g) != 0) {
                return EXIT_FAILURE;
            }
            current_spec.g = g;
            current_spec.have_g = 1;
        } else if (strcmp(arg, "-b") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for -b.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "-b", 0, 255, &b) != 0) {
                return EXIT_FAILURE;
            }
            current_spec.b = b;
            current_spec.have_b = 1;
        } else {
            fprintf(stderr, "_TERM_PIXEL: unknown argument '%s'.\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }

        if (pixel_spec_complete(&current_spec)) {
            if (append_pixel_spec(&pixel_specs, &pixel_spec_count, &pixel_spec_capacity, &current_spec) != 0) {
                fprintf(stderr, "_TERM_PIXEL: failed to record pixel spec.\n");
                free(pixel_specs);
                return EXIT_FAILURE;
            }
            reset_pixel_spec(&current_spec);
        }
    }

    if (clear) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0 || pixel_spec_count > 0u || read_stdin) {
            fprintf(stderr, "_TERM_PIXEL: --clear cannot be combined with draw arguments.\n");
            free(pixel_specs);
            return EXIT_FAILURE;
        }
        if (printf("\x1b]777;pixel=clear\a") < 0) {
            perror("_TERM_PIXEL: printf");
            free(pixel_specs);
            return EXIT_FAILURE;
        }
        free(pixel_specs);
    } else {
        if (pixel_spec_has_any(&current_spec)) {
            if (!pixel_spec_complete(&current_spec)) {
                fprintf(stderr, "_TERM_PIXEL: incomplete pixel specification provided.\n");
                free(pixel_specs);
                return EXIT_FAILURE;
            }
            if (append_pixel_spec(&pixel_specs, &pixel_spec_count, &pixel_spec_capacity, &current_spec) != 0) {
                fprintf(stderr, "_TERM_PIXEL: failed to record pixel spec.\n");
                free(pixel_specs);
                return EXIT_FAILURE;
            }
            reset_pixel_spec(&current_spec);
        }

        if (read_stdin) {
            char *line = NULL;
            size_t line_capacity = 0u;
            ssize_t line_length = 0;
            unsigned long line_number = 0u;
            while ((line_length = getline(&line, &line_capacity, stdin)) != -1) {
                line_number++;
                if (line_length == 0) {
                    continue;
                }
                char *cursor = line;
                while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor == '\0' || *cursor == '#') {
                    continue;
                }

                struct pixel_spec stdin_spec;
                reset_pixel_spec(&stdin_spec);
                char *saveptr = NULL;
                for (int field = 0; field < 5; field++) {
                    char *token = strtok_r(cursor, " \t\r\n", &saveptr);
                    cursor = NULL;
                    if (!token) {
                        fprintf(stderr, "_TERM_PIXEL: expected 5 values on stdin line %lu.\n", line_number);
                        free(line);
                        free(pixel_specs);
                        return EXIT_FAILURE;
                    }
                    long value = 0;
                    if (parse_long(token, "stdin", 0, field < 2 ? INT_MAX : 255, &value) != 0) {
                        free(line);
                        free(pixel_specs);
                        return EXIT_FAILURE;
                    }
                    switch (field) {
                    case 0:
                        stdin_spec.x = value;
                        stdin_spec.have_x = 1;
                        break;
                    case 1:
                        stdin_spec.y = value;
                        stdin_spec.have_y = 1;
                        break;
                    case 2:
                        stdin_spec.r = value;
                        stdin_spec.have_r = 1;
                        break;
                    case 3:
                        stdin_spec.g = value;
                        stdin_spec.have_g = 1;
                        break;
                    case 4:
                        stdin_spec.b = value;
                        stdin_spec.have_b = 1;
                        break;
                    default:
                        break;
                    }
                }

                if (!pixel_spec_complete(&stdin_spec)) {
                    fprintf(stderr, "_TERM_PIXEL: incomplete stdin specification on line %lu.\n", line_number);
                    free(line);
                    free(pixel_specs);
                    return EXIT_FAILURE;
                }

                if (append_pixel_spec(&pixel_specs, &pixel_spec_count, &pixel_spec_capacity, &stdin_spec) != 0) {
                    fprintf(stderr, "_TERM_PIXEL: failed to record stdin pixel spec.\n");
                    free(line);
                    free(pixel_specs);
                    return EXIT_FAILURE;
                }
            }

            free(line);
        }

        if (pixel_spec_count == 0u) {
            if (x < 0 || y < 0 || r < 0 || g < 0 || b < 0) {
                fprintf(stderr, "_TERM_PIXEL: missing required draw arguments.\n");
                print_usage();
                free(pixel_specs);
                return EXIT_FAILURE;
            }
            struct pixel_spec legacy_spec = {
                .x = x,
                .y = y,
                .r = r,
                .g = g,
                .b = b,
                .have_x = 1,
                .have_y = 1,
                .have_r = 1,
                .have_g = 1,
                .have_b = 1
            };
            if (append_pixel_spec(&pixel_specs, &pixel_spec_count, &pixel_spec_capacity, &legacy_spec) != 0) {
                fprintf(stderr, "_TERM_PIXEL: failed to record pixel spec.\n");
                free(pixel_specs);
                return EXIT_FAILURE;
            }
        }

        char *payload = NULL;
        size_t payload_length = 0u;
        size_t payload_capacity = 0u;
        if (append_format(&payload, &payload_length, &payload_capacity, "\x1b]777;") != 0) {
            fprintf(stderr, "_TERM_PIXEL: failed to build payload.\n");
            free(pixel_specs);
            free(payload);
            return EXIT_FAILURE;
        }

        for (size_t i = 0u; i < pixel_spec_count; i++) {
            const struct pixel_spec *spec = &pixel_specs[i];
            if (i > 0u) {
                if (append_format(&payload, &payload_length, &payload_capacity, ";") != 0) {
                    fprintf(stderr, "_TERM_PIXEL: failed to build payload.\n");
                    free(pixel_specs);
                    free(payload);
                    return EXIT_FAILURE;
                }
            }
            if (append_format(&payload,
                              &payload_length,
                              &payload_capacity,
                              "pixel=draw;pixel_x=%ld;pixel_y=%ld;pixel_r=%ld;pixel_g=%ld;pixel_b=%ld",
                              spec->x,
                              spec->y,
                              spec->r,
                              spec->g,
                              spec->b) != 0) {
                fprintf(stderr, "_TERM_PIXEL: failed to build payload.\n");
                free(pixel_specs);
                free(payload);
                return EXIT_FAILURE;
            }
        }

        if (append_format(&payload, &payload_length, &payload_capacity, "\a") != 0) {
            fprintf(stderr, "_TERM_PIXEL: failed to finalize payload.\n");
            free(pixel_specs);
            free(payload);
            return EXIT_FAILURE;
        }

        if (fwrite(payload, 1, payload_length, stdout) != payload_length) {
            perror("_TERM_PIXEL: fwrite");
            free(pixel_specs);
            free(payload);
            return EXIT_FAILURE;
        }

        free(payload);
        free(pixel_specs);
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_PIXEL: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
