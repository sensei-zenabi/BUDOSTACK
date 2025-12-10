#include "../lib/stb_image.h"

#define STBIW_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "../lib/stb_image_write.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

typedef enum {
    DITHER_NONE = 0,
    DITHER_FLOYD_STEINBERG = 1,
    DITHER_ORDERED_4x4 = 2
} DitheringMode;

static const Pixel PIXEL_PALETTE[] = {
    {  0,   0,   0}, {255, 255, 255}, {128, 128, 128}, {255,   0,   0},
    {  0, 255,   0}, {  0,   0, 255}, {255, 255,   0}, {255,   0, 255},
    {  0, 255, 255}, {255, 165,   0}, {165,  42,  42}, { 75,   0, 130},
    {210, 105,  30}, {144, 238, 144}, {135, 206, 235}, { 47,  79,  79}
};
static const size_t PIXEL_PALETTE_SIZE = sizeof(PIXEL_PALETTE) / sizeof(PIXEL_PALETTE[0]);

static void print_help(void) {
    printf("pixart - convert an image into pixel art with palette quantization.\n\n");
    printf("Usage:\n");
    printf("  pixart -mode <integer> -size <integer> -file <input> -output <output>\n\n");
    printf("Options:\n");
    printf("  -mode <mode>        Dithering algorithm to use (default: 1)\n");
    printf("                        0 = None\n");
    printf("                        1 = Floyd-Steinberg error diffusion\n");
    printf("                        2 = Ordered 4x4 Bayer matrix\n");
    printf("  -size <percent>     Output size as percent of the source (default: 50)\n");
    printf("  -file <path>        Input image file (PNG/JPG/TGA/BMP and more)\n");
    printf("  -output <path>      Output PNG file path\n");
    printf("  -help               Show this help message\n");
}

static int parse_int(const char *text, int *out) {
    if (text == NULL || out == NULL) {
        return 0;
    }
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    if (value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

static Pixel clamp_pixel(float r, float g, float b) {
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    Pixel p = {(uint8_t)(r + 0.5f), (uint8_t)(g + 0.5f), (uint8_t)(b + 0.5f)};
    return p;
}

static Pixel nearest_palette_color(Pixel px) {
    size_t best_idx = 0;
    unsigned int best_distance = UINT_MAX;
    for (size_t i = 0; i < PIXEL_PALETTE_SIZE; ++i) {
        int dr = (int)px.r - (int)PIXEL_PALETTE[i].r;
        int dg = (int)px.g - (int)PIXEL_PALETTE[i].g;
        int db = (int)px.b - (int)PIXEL_PALETTE[i].b;
        unsigned int dist = (unsigned int)(dr * dr + dg * dg + db * db);
        if (dist < best_distance) {
            best_distance = dist;
            best_idx = i;
        }
    }
    return PIXEL_PALETTE[best_idx];
}

static Pixel *nearest_resize(const unsigned char *input, int width, int height, int new_w, int new_h) {
    if (input == NULL || new_w <= 0 || new_h <= 0 || width <= 0 || height <= 0) {
        return NULL;
    }
    Pixel *out = malloc((size_t)new_w * (size_t)new_h * sizeof(Pixel));
    if (out == NULL) {
        return NULL;
    }
    for (int y = 0; y < new_h; ++y) {
        int src_y = y * height / new_h;
        for (int x = 0; x < new_w; ++x) {
            int src_x = x * width / new_w;
            size_t src_idx = ((size_t)src_y * (size_t)width + (size_t)src_x) * 3U;
            size_t dst_idx = (size_t)y * (size_t)new_w + (size_t)x;
            out[dst_idx].r = input[src_idx + 0];
            out[dst_idx].g = input[src_idx + 1];
            out[dst_idx].b = input[src_idx + 2];
        }
    }
    return out;
}

static Pixel *apply_ordered_dither(const Pixel *input, int width, int height) {
    static const int bayer4x4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };
    Pixel *out = malloc((size_t)width * (size_t)height * sizeof(Pixel));
    if (out == NULL) {
        return NULL;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            const Pixel src = input[idx];
            float threshold = (float)bayer4x4[y & 3][x & 3] / 16.0f - 0.5f;
            float adjust = threshold * 32.0f;
            Pixel adjusted = clamp_pixel((float)src.r + adjust, (float)src.g + adjust, (float)src.b + adjust);
            out[idx] = nearest_palette_color(adjusted);
        }
    }
    return out;
}

static Pixel *apply_floyd_steinberg(const Pixel *input, int width, int height) {
    Pixel *out = malloc((size_t)width * (size_t)height * sizeof(Pixel));
    float *error = calloc((size_t)width * (size_t)height * 3U, sizeof(float));
    if (out == NULL || error == NULL) {
        free(out);
        free(error);
        return NULL;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            float r = (float)input[idx].r + error[idx * 3U + 0];
            float g = (float)input[idx].g + error[idx * 3U + 1];
            float b = (float)input[idx].b + error[idx * 3U + 2];
            Pixel corrected = clamp_pixel(r, g, b);
            Pixel quant = nearest_palette_color(corrected);
            out[idx] = quant;

            float err_r = (float)corrected.r - (float)quant.r;
            float err_g = (float)corrected.g - (float)quant.g;
            float err_b = (float)corrected.b - (float)quant.b;

            if (x + 1 < width) {
                size_t e_idx = idx + 1U;
                error[e_idx * 3U + 0] += err_r * 7.0f / 16.0f;
                error[e_idx * 3U + 1] += err_g * 7.0f / 16.0f;
                error[e_idx * 3U + 2] += err_b * 7.0f / 16.0f;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    size_t e_idx = idx + (size_t)width - 1U;
                    error[e_idx * 3U + 0] += err_r * 3.0f / 16.0f;
                    error[e_idx * 3U + 1] += err_g * 3.0f / 16.0f;
                    error[e_idx * 3U + 2] += err_b * 3.0f / 16.0f;
                }
                size_t e_idx = idx + (size_t)width;
                error[e_idx * 3U + 0] += err_r * 5.0f / 16.0f;
                error[e_idx * 3U + 1] += err_g * 5.0f / 16.0f;
                error[e_idx * 3U + 2] += err_b * 5.0f / 16.0f;
                if (x + 1 < width) {
                    e_idx = idx + (size_t)width + 1U;
                    error[e_idx * 3U + 0] += err_r * 1.0f / 16.0f;
                    error[e_idx * 3U + 1] += err_g * 1.0f / 16.0f;
                    error[e_idx * 3U + 2] += err_b * 1.0f / 16.0f;
                }
            }
        }
    }
    free(error);
    return out;
}

static Pixel *quantize_image(const Pixel *input, int width, int height, DitheringMode mode) {
    if (mode == DITHER_FLOYD_STEINBERG) {
        return apply_floyd_steinberg(input, width, height);
    }
    if (mode == DITHER_ORDERED_4x4) {
        return apply_ordered_dither(input, width, height);
    }
    Pixel *out = malloc((size_t)width * (size_t)height * sizeof(Pixel));
    if (out == NULL) {
        return NULL;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            out[idx] = nearest_palette_color(input[idx]);
        }
    }
    return out;
}

static int write_png(const char *path, const Pixel *data, int width, int height) {
    unsigned char *buffer = malloc((size_t)width * (size_t)height * 3U);
    if (buffer == NULL) {
        return 0;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            size_t buf_idx = idx * 3U;
            buffer[buf_idx + 0] = data[idx].r;
            buffer[buf_idx + 1] = data[idx].g;
            buffer[buf_idx + 2] = data[idx].b;
        }
    }
    int result = stbi_write_png(path, width, height, 3, buffer, 0);
    free(buffer);
    return result != 0;
}

int main(int argc, char *argv[]) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int size_pct = 50;
    int dithering = DITHER_FLOYD_STEINBERG;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-mode") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], &dithering)) {
                fprintf(stderr, "pixart: invalid dithering mode value.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-size") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], &size_pct)) {
                fprintf(stderr, "pixart: invalid size value.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-file") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "-output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else {
            fprintf(stderr, "pixart: unknown or incomplete argument '%s'. Use -help for usage.\n", argv[i]);
            return 1;
        }
    }

    if (input_path == NULL || output_path == NULL) {
        fprintf(stderr, "pixart: -file and -output are required. Use -help for usage.\n");
        return 1;
    }

    if (size_pct <= 0 || size_pct > 800) {
        fprintf(stderr, "pixart: size must be between 1 and 800.\n");
        return 1;
    }

    if (dithering < DITHER_NONE || dithering > DITHER_ORDERED_4x4) {
        fprintf(stderr, "pixart: unsupported dithering mode. Use -help to list options.\n");
        return 1;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *source = stbi_load(input_path, &width, &height, &channels, 3);
    if (source == NULL) {
        fprintf(stderr, "pixart: failed to load '%s'.\n", input_path);
        return 1;
    }

    long long scaled_w = (long long)width * size_pct / 100LL;
    long long scaled_h = (long long)height * size_pct / 100LL;
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;
    if (scaled_w > INT_MAX || scaled_h > INT_MAX) {
        stbi_image_free(source);
        fprintf(stderr, "pixart: calculated size exceeds supported range.\n");
        return 1;
    }

    Pixel *resized = nearest_resize(source, width, height, (int)scaled_w, (int)scaled_h);
    stbi_image_free(source);
    if (resized == NULL) {
        fprintf(stderr, "pixart: unable to resize image.\n");
        return 1;
    }

    Pixel *quantized = quantize_image(resized, (int)scaled_w, (int)scaled_h, (DitheringMode)dithering);
    free(resized);
    if (quantized == NULL) {
        fprintf(stderr, "pixart: unable to apply dithering.\n");
        return 1;
    }

    if (!write_png(output_path, quantized, (int)scaled_w, (int)scaled_h)) {
        fprintf(stderr, "pixart: failed to write output file '%s'.\n", output_path);
        free(quantized);
        return 1;
    }

    free(quantized);
    return 0;
}

