#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_PIXEL -x <pixels> -y <pixels> -r <0-255> -g <0-255> -b <0-255>\n");
    fprintf(stderr, "       _TERM_PIXEL --clear\n");
    fprintf(stderr, "       _TERM_PIXEL --render\n");
    fprintf(stderr, "       _TERM_PIXEL --open [--width <pixels>] [--height <pixels>] [--resolution <WxH>]\n");
    fprintf(stderr, "       _TERM_PIXEL --bulk --width <pixels> --height <pixels> [--format rgb|rgba] [--render] < raw_pixel_data\n");
    fprintf(stderr, "  Draws or clears raw SDL pixels on the terminal window.\n");
    fprintf(stderr, "  --open prepares a fast framebuffer of the given size (if provided)\n");
    fprintf(stderr, "  that can be reused between draw calls before a later --render.\n");
    fprintf(stderr, "  --bulk reads a full frame from stdin (rgb or rgba) and sends it in one escape;\n");
    fprintf(stderr, "  append --render to present the uploaded frame immediately.\n");
}

static char *base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!data) {
        return NULL;
    }

    size_t encoded_len = ((len + 2u) / 3u) * 4u;
    if (encoded_len == 0u) {
        encoded_len = 4u;
    }
    char *out = malloc(encoded_len + 1u);
    if (!out) {
        return NULL;
    }

    size_t out_index = 0u;
    for (size_t i = 0u; i < len; i += 3u) {
        uint32_t chunk = (uint32_t)data[i] << 16u;
        if (i + 1u < len) {
            chunk |= (uint32_t)data[i + 1u] << 8u;
        }
        if (i + 2u < len) {
            chunk |= (uint32_t)data[i + 2u];
        }

        out[out_index++] = table[(chunk >> 18u) & 0x3Fu];
        out[out_index++] = table[(chunk >> 12u) & 0x3Fu];
        out[out_index++] = (i + 1u < len) ? table[(chunk >> 6u) & 0x3Fu] : '=';
        out[out_index++] = (i + 2u < len) ? table[chunk & 0x3Fu] : '=';
    }

    out[out_index] = '\0';
    if (out_len) {
        *out_len = out_index;
    }
    return out;
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
    int bulk = 0;
    long x = -1;
    long y = -1;
    long r = -1;
    long g = -1;
    long b = -1;
    long width = -1;
    long height = -1;
    long resolution_width = -1;
    long resolution_height = -1;
    const char *bulk_format = "rgb";

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--clear") == 0) {
            clear = 1;
        } else if (strcmp(arg, "--render") == 0) {
            render = 1;
        } else if (strcmp(arg, "--open") == 0) {
            open = 1;
        } else if (strcmp(arg, "--bulk") == 0) {
            bulk = 1;
        } else if (strcmp(arg, "--width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for --width.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "--width", 1, INT_MAX, &width) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for --height.\n");
                return EXIT_FAILURE;
            }
            if (parse_long(argv[i], "--height", 1, INT_MAX, &height) != 0) {
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--format") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for --format.\n");
                return EXIT_FAILURE;
            }
            const char *value = argv[i];
            if (strcasecmp(value, "rgb") == 0) {
                bulk_format = "rgb";
            } else if (strcasecmp(value, "rgba") == 0) {
                bulk_format = "rgba";
            } else {
                fprintf(stderr, "_TERM_PIXEL: unsupported format '%s'. Use rgb or rgba.\n", value);
                return EXIT_FAILURE;
            }
        } else if (strcmp(arg, "--resolution") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TERM_PIXEL: missing value for --resolution.\n");
                return EXIT_FAILURE;
            }
            const char *value = argv[i];
            const char *sep = strchr(value, 'x');
            if (!sep) {
                sep = strchr(value, 'X');
            }
            if (!sep) {
                fprintf(stderr, "_TERM_PIXEL: --resolution must be formatted as WxH.\n");
                return EXIT_FAILURE;
            }
            size_t width_len = (size_t)(sep - value);
            if (width_len == 0u) {
                fprintf(stderr, "_TERM_PIXEL: missing width in --resolution.\n");
                return EXIT_FAILURE;
            }
            char *width_copy = strndup(value, width_len);
            if (!width_copy) {
                perror("_TERM_PIXEL: strndup");
                return EXIT_FAILURE;
            }
            char *height_copy = strdup(sep + 1);
            if (!height_copy) {
                free(width_copy);
                perror("_TERM_PIXEL: strdup");
                return EXIT_FAILURE;
            }
            int parse_error = 0;
            if (parse_long(width_copy, "--resolution width", 1, INT_MAX, &resolution_width) != 0) {
                parse_error = 1;
            }
            if (!parse_error && parse_long(height_copy, "--resolution height", 1, INT_MAX, &resolution_height) != 0) {
                parse_error = 1;
            }
            free(width_copy);
            free(height_copy);
            if (parse_error) {
                return EXIT_FAILURE;
            }
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

    if (clear) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0 || open || render) {
            fprintf(stderr, "_TERM_PIXEL: --clear cannot be combined with other actions.\n");
            return EXIT_FAILURE;
        }
        if (printf("\x1b]777;pixel=clear\a") < 0) {
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }
    } else if (open) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0) {
            fprintf(stderr, "_TERM_PIXEL: --open cannot be combined with draw arguments.\n");
            return EXIT_FAILURE;
        }
        if (resolution_width >= 0 && resolution_height >= 0) {
            if (printf("\x1b]777;pixel=open;resolution=%ldx%ld", resolution_width, resolution_height) < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        } else {
            if (printf("\x1b]777;pixel=open") < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
        if (width > 0) {
            if (printf(";pixel_width=%ld", width) < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
        if (height > 0) {
            if (printf(";pixel_height=%ld", height) < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
        if (printf("\a") < 0) {
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }
    } else if (bulk) {
        if (width <= 0 || height <= 0) {
            fprintf(stderr, "_TERM_PIXEL: --bulk requires --width and --height.\n");
            return EXIT_FAILURE;
        }
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0 || open || clear) {
            fprintf(stderr, "_TERM_PIXEL: --bulk cannot be combined with draw, open, or clear arguments.\n");
            return EXIT_FAILURE;
        }

        int bytes_per_pixel = (strcmp(bulk_format, "rgba") == 0) ? 4 : 3;
        if (width > LONG_MAX / height) {
            fprintf(stderr, "_TERM_PIXEL: width and height are too large.\n");
            return EXIT_FAILURE;
        }
        long pixel_count = width * height;
        if (pixel_count <= 0 || pixel_count > LONG_MAX / bytes_per_pixel) {
            fprintf(stderr, "_TERM_PIXEL: pixel count is too large.\n");
            return EXIT_FAILURE;
        }
        size_t expected_size = (size_t)pixel_count * (size_t)bytes_per_pixel;
        uint8_t *frame = malloc(expected_size);
        if (!frame) {
            perror("_TERM_PIXEL: malloc");
            return EXIT_FAILURE;
        }
        size_t total_read = 0u;
        while (total_read < expected_size) {
            size_t bytes_to_read = expected_size - total_read;
            if (bytes_to_read > 4096u) {
                bytes_to_read = 4096u;
            }
            size_t n = fread(frame + total_read, 1u, bytes_to_read, stdin);
            if (n == 0u) {
                if (ferror(stdin)) {
                    perror("_TERM_PIXEL: fread");
                } else {
                    fprintf(stderr, "_TERM_PIXEL: not enough data provided on stdin for bulk frame.\n");
                }
                free(frame);
                return EXIT_FAILURE;
            }
            total_read += n;
        }

        size_t encoded_len = 0u;
        char *encoded = base64_encode(frame, expected_size, &encoded_len);
        free(frame);
        if (!encoded) {
            fprintf(stderr, "_TERM_PIXEL: failed to encode frame to base64.\n");
            return EXIT_FAILURE;
        }

        if (printf("\x1b]777;pixel=bulk;pixel_width=%ld;pixel_height=%ld;pixel_format=%s;pixel_data=%s\a",
                   width,
                   height,
                   bulk_format,
                   encoded) < 0) {
            free(encoded);
            perror("_TERM_PIXEL: printf");
            return EXIT_FAILURE;
        }

        free(encoded);

        if (render) {
            if (printf("\x1b]777;pixel=render\a") < 0) {
                perror("_TERM_PIXEL: printf");
                return EXIT_FAILURE;
            }
        }
    } else if (render) {
        if (x >= 0 || y >= 0 || r >= 0 || g >= 0 || b >= 0 || open || clear) {
            fprintf(stderr, "_TERM_PIXEL: --render cannot be combined with other actions.\n");
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
