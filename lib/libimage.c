#include "libimage.h"

#include "termbg.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} Pixel;

typedef void (*RenderPixelsFn)(const Pixel *pixels, int width, int height, int origin_x, int origin_y);

static char g_last_error[256];

static void set_error(const char *fmt, ...) {
    if (fmt == NULL) {
        g_last_error[0] = '\0';
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

const char *libimage_last_error(void) {
    return g_last_error;
}

static int has_extension(const char *path, const char *ext) {
    if (path == NULL || ext == NULL) {
        return 0;
    }

    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (ext_len == 0 || path_len < ext_len) {
        return 0;
    }

    const char *path_ext = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        unsigned char path_ch = (unsigned char)path_ext[i];
        unsigned char ext_ch = (unsigned char)ext[i];
        if (tolower(path_ch) != tolower(ext_ch)) {
            return 0;
        }
    }
    return 1;
}

static void output_truecolor_bg(uint8_t r, uint8_t g, uint8_t b) {
    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\x1b[48;2;%u;%u;%um",
                       (unsigned int)r, (unsigned int)g, (unsigned int)b);
    if (len > 0) {
        fwrite(seq, 1, (size_t)len, stdout);
    }
}

static int ansi256_to_rgb(int color, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
    if (color < 0 || color > 255) {
        return 0;
    }

    static const uint8_t ansi16[16][3] = {
        {  0,   0,   0}, {128,   0,   0}, {  0, 128,   0}, {128, 128,   0},
        {  0,   0, 128}, {128,   0, 128}, {  0, 128, 128}, {192, 192, 192},
        {128, 128, 128}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
        {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255}
    };

    if (color < 16) {
        if (r_out)
            *r_out = ansi16[color][0];
        if (g_out)
            *g_out = ansi16[color][1];
        if (b_out)
            *b_out = ansi16[color][2];
        return 1;
    }

    if (color <= 231) {
        int idx = color - 16;
        int r = idx / 36;
        int g = (idx / 6) % 6;
        int b = idx % 6;
        static const uint8_t steps[6] = {0, 95, 135, 175, 215, 255};
        if (r_out)
            *r_out = steps[r];
        if (g_out)
            *g_out = steps[g];
        if (b_out)
            *b_out = steps[b];
        return 1;
    }

    int gray = (color - 232) * 10 + 8;
    if (gray < 0)
        gray = 0;
    if (gray > 255)
        gray = 255;
    if (r_out)
        *r_out = (uint8_t)gray;
    if (g_out)
        *g_out = (uint8_t)gray;
    if (b_out)
        *b_out = (uint8_t)gray;
    return 1;
}

static void blend_over(uint8_t *r, uint8_t *g, uint8_t *b,
                       uint8_t a, uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
    uint32_t alpha = a;
    uint32_t inv = 255U - alpha;
    *r = (uint8_t)((((uint32_t)(*r) * alpha) + ((uint32_t)bg_r * inv) + 127U) / 255U);
    *g = (uint8_t)((((uint32_t)(*g) * alpha) + ((uint32_t)bg_g * inv) + 127U) / 255U);
    *b = (uint8_t)((((uint32_t)(*b) * alpha) + ((uint32_t)bg_b * inv) + 127U) / 255U);
}

static void render_pixels_at(const Pixel *pixels, int width, int height, int origin_x, int origin_y) {
    if (pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    if (origin_x < 0 || origin_y < 0) {
        return;
    }

    if (origin_x > INT_MAX - 1 || origin_y > INT_MAX - 1) {
        return;
    }

    int start_col = origin_x + 1;
    int start_row = origin_y + 1;

    for (int y = 0; y < height; ++y) {
        int row = start_row + y;
        char move_seq[32];
        int move_len = snprintf(move_seq, sizeof(move_seq), "\x1b[%d;%dH", row, start_col);
        if (move_len > 0) {
            fwrite(move_seq, 1, (size_t)move_len, stdout);
        }

        for (int x = 0; x < width; ++x) {
            const Pixel *p = &pixels[(size_t)y * (size_t)width + (size_t)x];
            int bg_color = -1;
            if (p->a < 16U) {
                if (termbg_get(origin_x + x, origin_y + y, &bg_color) != 0 && bg_color >= 0) {
                    if (termbg_is_truecolor(bg_color)) {
                        int r, g, b;
                        termbg_decode_truecolor(bg_color, &r, &g, &b);
                        output_truecolor_bg((uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        uint8_t bg_r = 0;
                        uint8_t bg_g = 0;
                        uint8_t bg_b = 0;
                        ansi256_to_rgb(bg_color, &bg_r, &bg_g, &bg_b);
                        output_truecolor_bg(bg_r, bg_g, bg_b);
                    }
                    fputs("\x1b[39m ", stdout);
                    fputs("\x1b[49m", stdout);
                } else {
                    fputs("\x1b[49m\x1b[39m ", stdout);
                }
                continue;
            }

            uint8_t out_r = p->r;
            uint8_t out_g = p->g;
            uint8_t out_b = p->b;
            if (p->a < 255U) {
                if (termbg_get(origin_x + x, origin_y + y, &bg_color) != 0 && bg_color >= 0) {
                    uint8_t bg_r = 0;
                    uint8_t bg_g = 0;
                    uint8_t bg_b = 0;
                    if (termbg_is_truecolor(bg_color)) {
                        int r, g, b;
                        termbg_decode_truecolor(bg_color, &r, &g, &b);
                        bg_r = (uint8_t)r;
                        bg_g = (uint8_t)g;
                        bg_b = (uint8_t)b;
                    } else {
                        ansi256_to_rgb(bg_color, &bg_r, &bg_g, &bg_b);
                    }
                    blend_over(&out_r, &out_g, &out_b, p->a, bg_r, bg_g, bg_b);
                } else {
                    blend_over(&out_r, &out_g, &out_b, p->a, 0, 0, 0);
                }
            }

            output_truecolor_bg(out_r, out_g, out_b);
            fputs("\x1b[39m", stdout);
            fputc(' ', stdout);
            fputs("\x1b[49m", stdout);
            termbg_set(origin_x + x, origin_y + y,
                       termbg_encode_truecolor(out_r, out_g, out_b));
        }
        fputs("\x1b[49m\x1b[39m", stdout);
    }
    fputs("\x1b[49m\x1b[39m", stdout);
    fflush(stdout);
}

static void render_pixels_streamed(const Pixel *pixels, int width, int height, int origin_x, int origin_y) {
    if (pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    if (origin_x < 0 || origin_y < 0) {
        return;
    }

    for (int y = 0; y < height; ++y) {
        fputc('\r', stdout);
        if (origin_x > 0) {
            char move_seq[32];
            int move_len = snprintf(move_seq, sizeof(move_seq), "\x1b[%dC", origin_x);
            if (move_len > 0) {
                fwrite(move_seq, 1, (size_t)move_len, stdout);
            }
        }

        for (int x = 0; x < width; ++x) {
            const Pixel *p = &pixels[(size_t)y * (size_t)width + (size_t)x];
            int bg_color = -1;
            int abs_x = origin_x + x;
            int abs_y = origin_y + y;
            if (p->a < 16U) {
                if (termbg_get(abs_x, abs_y, &bg_color) != 0 && bg_color >= 0) {
                    if (termbg_is_truecolor(bg_color)) {
                        int r, g, b;
                        termbg_decode_truecolor(bg_color, &r, &g, &b);
                        output_truecolor_bg((uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        uint8_t bg_r = 0;
                        uint8_t bg_g = 0;
                        uint8_t bg_b = 0;
                        ansi256_to_rgb(bg_color, &bg_r, &bg_g, &bg_b);
                        output_truecolor_bg(bg_r, bg_g, bg_b);
                    }
                    fputs("\x1b[39m ", stdout);
                    fputs("\x1b[49m", stdout);
                } else {
                    fputs("\x1b[49m\x1b[39m ", stdout);
                }
                continue;
            }

            uint8_t out_r = p->r;
            uint8_t out_g = p->g;
            uint8_t out_b = p->b;
            if (p->a < 255U) {
                if (termbg_get(abs_x, abs_y, &bg_color) != 0 && bg_color >= 0) {
                    uint8_t bg_r = 0;
                    uint8_t bg_g = 0;
                    uint8_t bg_b = 0;
                    if (termbg_is_truecolor(bg_color)) {
                        int r, g, b;
                        termbg_decode_truecolor(bg_color, &r, &g, &b);
                        bg_r = (uint8_t)r;
                        bg_g = (uint8_t)g;
                        bg_b = (uint8_t)b;
                    } else {
                        ansi256_to_rgb(bg_color, &bg_r, &bg_g, &bg_b);
                    }
                    blend_over(&out_r, &out_g, &out_b, p->a, bg_r, bg_g, bg_b);
                } else {
                    blend_over(&out_r, &out_g, &out_b, p->a, 0, 0, 0);
                }
            }

            output_truecolor_bg(out_r, out_g, out_b);
            fputs("\x1b[39m", stdout);
            fputc(' ', stdout);
            fputs("\x1b[49m", stdout);
            termbg_set(abs_x, abs_y, termbg_encode_truecolor(out_r, out_g, out_b));
        }
        fputs("\x1b[49m\x1b[39m", stdout);
        if (y + 1 < height) {
            fputc('\n', stdout);
        }
    }
    fputs("\x1b[49m\x1b[39m", stdout);
    fflush(stdout);
}

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPINFOHEADER;
#pragma pack(pop)

static LibImageResult render_bmp(const char *path, int origin_x, int origin_y, RenderPixelsFn render_pixels) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_error("Unable to open '%s': %s", path, strerror(errno));
        return LIBIMAGE_IO_ERROR;
    }

    BMPFILEHEADER file_header;
    BMPINFOHEADER info_header;

    if (fread(&file_header, sizeof(file_header), 1, fp) != 1 ||
        fread(&info_header, sizeof(info_header), 1, fp) != 1) {
        fclose(fp);
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    if (file_header.bfType != 0x4D42) {
        fclose(fp);
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    if (info_header.biBitCount != 24 || info_header.biCompression != 0 || info_header.biPlanes != 1) {
        fclose(fp);
        set_error("Unsupported BMP format in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }

    int width = info_header.biWidth;
    int height = info_header.biHeight;
    int flip_vertical = 0;
    if (height < 0) {
        flip_vertical = 1;
        height = -height;
    }

    if (width <= 0 || height <= 0) {
        fclose(fp);
        set_error("Invalid BMP dimensions in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }

    if (origin_x > INT_MAX - width || origin_y > INT_MAX - height) {
        fclose(fp);
        set_error("Image dimensions exceed terminal limits");
        return LIBIMAGE_INVALID_ARGUMENT;
    }

    if (fseek(fp, (long)file_header.bfOffBits, SEEK_SET) != 0) {
        set_error("Failed to seek BMP pixel data in '%s': %s", path, strerror(errno));
        fclose(fp);
        return LIBIMAGE_IO_ERROR;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (width != 0 && pixel_count / (size_t)width != (size_t)height) {
        fclose(fp);
        set_error("BMP dimensions overflow in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }

    if (pixel_count > SIZE_MAX / sizeof(Pixel)) {
        fclose(fp);
        set_error("BMP image too large in '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    Pixel *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        fclose(fp);
        set_error("Failed to allocate memory for BMP '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    size_t row_bytes = (size_t)width * 3U;
    size_t padding = (4U - (row_bytes % 4U)) & 3U;

    for (int y = 0; y < height; ++y) {
        int target_row = flip_vertical ? y : (height - 1 - y);
        Pixel *row = pixels + (size_t)target_row * (size_t)width;
        for (int x = 0; x < width; ++x) {
            int b = fgetc(fp);
            int g = fgetc(fp);
            int r = fgetc(fp);
            if (b == EOF || g == EOF || r == EOF) {
                free(pixels);
                fclose(fp);
                set_error("Unexpected EOF in BMP pixel data for '%s'", path);
                return LIBIMAGE_IO_ERROR;
            }
            row[x].r = (uint8_t)r;
            row[x].g = (uint8_t)g;
            row[x].b = (uint8_t)b;
            row[x].a = 255U;
        }
        for (size_t p = 0; p < padding; ++p) {
            if (fgetc(fp) == EOF) {
                free(pixels);
                fclose(fp);
                set_error("Unexpected EOF in BMP padding for '%s'", path);
                return LIBIMAGE_IO_ERROR;
            }
        }
    }

    fclose(fp);

    if (render_pixels != NULL) {
        render_pixels(pixels, width, height, origin_x, origin_y);
    }
    free(pixels);
    set_error(NULL);
    return LIBIMAGE_SUCCESS;
}

static int read_ppm_token(FILE *fp, char *buffer, size_t buffer_size) {
    if (buffer_size == 0) {
        return 0;
    }

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (isspace(c) != 0) {
            continue;
        }
        if (c == '#') {
            while ((c = fgetc(fp)) != EOF && c != '\n') {
                /* skip comment */
            }
            continue;
        }
        ungetc(c, fp);
        break;
    }

    if (c == EOF) {
        return 0;
    }

    size_t len = 0;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '#') {
            while ((c = fgetc(fp)) != EOF && c != '\n') {
                /* skip comment */
            }
            break;
        }
        if (isspace(c) != 0) {
            break;
        }
        if (len + 1 < buffer_size) {
            buffer[len++] = (char)c;
        }
    }
    buffer[len] = '\0';

    return len > 0 ? 1 : 0;
}

static LibImageResult render_ppm(const char *path, int origin_x, int origin_y, RenderPixelsFn render_pixels) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_error("Unable to open '%s': %s", path, strerror(errno));
        return LIBIMAGE_IO_ERROR;
    }

    char token[64];
    if (!read_ppm_token(fp, token, sizeof(token)) || strcmp(token, "P6") != 0) {
        fclose(fp);
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fclose(fp);
        set_error("Missing width in PPM file '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }
    long width_long = strtol(token, NULL, 10);

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fclose(fp);
        set_error("Missing height in PPM file '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }
    long height_long = strtol(token, NULL, 10);

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fclose(fp);
        set_error("Missing max value in PPM file '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }
    long max_value = strtol(token, NULL, 10);

    if (width_long <= 0 || height_long <= 0 || width_long > INT32_MAX ||
        height_long > INT32_MAX) {
        fclose(fp);
        set_error("Invalid PPM dimensions in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }
    if (max_value <= 0 || max_value > 255) {
        fclose(fp);
        set_error("Unsupported PPM max value in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }

    int width = (int)width_long;
    int height = (int)height_long;

    if (origin_x > INT_MAX - width || origin_y > INT_MAX - height) {
        fclose(fp);
        set_error("Image dimensions exceed terminal limits");
        return LIBIMAGE_INVALID_ARGUMENT;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (width != 0 && pixel_count / (size_t)width != (size_t)height) {
        fclose(fp);
        set_error("PPM dimensions overflow in '%s'", path);
        return LIBIMAGE_DATA_ERROR;
    }

    if (pixel_count > SIZE_MAX / sizeof(Pixel)) {
        fclose(fp);
        set_error("PPM image too large in '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    Pixel *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        fclose(fp);
        set_error("Failed to allocate memory for PPM '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    if (pixel_count > SIZE_MAX / 3U) {
        free(pixels);
        fclose(fp);
        set_error("PPM pixel data too large in '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    size_t raw_size = pixel_count * 3U;
    uint8_t *raw = malloc(raw_size);
    if (raw == NULL) {
        free(pixels);
        fclose(fp);
        set_error("Failed to allocate raw buffer for PPM '%s'", path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    if (fread(raw, 1, raw_size, fp) != raw_size) {
        free(raw);
        free(pixels);
        fclose(fp);
        set_error("Unexpected EOF in PPM pixel data for '%s'", path);
        return LIBIMAGE_IO_ERROR;
    }

    fclose(fp);

    for (size_t i = 0; i < pixel_count; ++i) {
        pixels[i].r = raw[i * 3 + 0];
        pixels[i].g = raw[i * 3 + 1];
        pixels[i].b = raw[i * 3 + 2];
        pixels[i].a = 255U;
    }

    free(raw);

    if (max_value != 255) {
        for (size_t i = 0; i < pixel_count; ++i) {
            pixels[i].r = (uint8_t)((pixels[i].r * 255U) / (uint32_t)max_value);
            pixels[i].g = (uint8_t)((pixels[i].g * 255U) / (uint32_t)max_value);
            pixels[i].b = (uint8_t)((pixels[i].b * 255U) / (uint32_t)max_value);
        }
    }

    if (render_pixels != NULL) {
        render_pixels(pixels, width, height, origin_x, origin_y);
    }
    free(pixels);
    set_error(NULL);
    return LIBIMAGE_SUCCESS;
}

static LibImageResult render_stbi_image(const char *path, const char *label, int origin_x, int origin_y,
                                        RenderPixelsFn render_pixels) {
    int width = 0;
    int height = 0;
    stbi_uc *raw = stbi_load(path, &width, &height, NULL, 4);
    if (raw == NULL) {
        const char *reason = stbi_failure_reason();
        if (reason != NULL && reason[0] != '\0') {
            set_error("Failed to decode %s '%s': %s", label, path, reason);
        } else {
            set_error("Failed to decode %s '%s'", label, path);
        }
        return LIBIMAGE_DATA_ERROR;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(raw);
        set_error("Invalid %s dimensions in '%s'", label, path);
        return LIBIMAGE_DATA_ERROR;
    }

    if (origin_x > INT_MAX - width || origin_y > INT_MAX - height) {
        stbi_image_free(raw);
        set_error("Image dimensions exceed terminal limits");
        return LIBIMAGE_INVALID_ARGUMENT;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (width_sz != 0 && height_sz > SIZE_MAX / width_sz) {
        stbi_image_free(raw);
        set_error("%s dimensions overflow in '%s'", label, path);
        return LIBIMAGE_DATA_ERROR;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / sizeof(Pixel)) {
        stbi_image_free(raw);
        set_error("%s image too large in '%s'", label, path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    Pixel *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        stbi_image_free(raw);
        set_error("Failed to allocate memory for %s '%s'", label, path);
        return LIBIMAGE_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 4U;
        uint8_t r = raw[idx + 0];
        uint8_t g = raw[idx + 1];
        uint8_t b = raw[idx + 2];
        uint8_t a = raw[idx + 3];
        pixels[i].r = r;
        pixels[i].g = g;
        pixels[i].b = b;
        pixels[i].a = a;
    }

    stbi_image_free(raw);

    if (render_pixels != NULL) {
        render_pixels(pixels, width, height, origin_x, origin_y);
    }
    free(pixels);
    set_error(NULL);
    return LIBIMAGE_SUCCESS;
}

static LibImageResult render_png(const char *path, int origin_x, int origin_y, RenderPixelsFn render_pixels) {
    if (!has_extension(path, ".png")) {
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_error("Unable to open '%s': %s", path, strerror(errno));
        return LIBIMAGE_IO_ERROR;
    }

    unsigned char signature[8];
    size_t sig_read = fread(signature, 1, sizeof(signature), fp);
    int read_error = ferror(fp);
    fclose(fp);

    if (sig_read < sizeof(signature) || read_error != 0) {
        if (read_error != 0) {
            set_error("Failed to read PNG header from '%s': %s", path, strerror(errno));
        } else {
            set_error("PNG file '%s' is too short", path);
        }
        return LIBIMAGE_DATA_ERROR;
    }

    static const unsigned char expected_signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'
    };
    if (memcmp(signature, expected_signature, sizeof(expected_signature)) != 0) {
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    return render_stbi_image(path, "PNG", origin_x, origin_y, render_pixels);
}

static LibImageResult render_jpeg(const char *path, int origin_x, int origin_y, RenderPixelsFn render_pixels) {
    if (!has_extension(path, ".jpg") && !has_extension(path, ".jpeg")) {
        return LIBIMAGE_UNSUPPORTED_FORMAT;
    }

    return render_stbi_image(path, "JPEG", origin_x, origin_y, render_pixels);
}

LibImageResult libimage_render_file_at(const char *path, int origin_x, int origin_y) {
    if (path == NULL) {
        set_error("Image path is NULL");
        return LIBIMAGE_INVALID_ARGUMENT;
    }
    if (origin_x < 0 || origin_y < 0) {
        set_error("Image coordinates must be non-negative");
        return LIBIMAGE_INVALID_ARGUMENT;
    }

    LibImageResult result = render_png(path, origin_x, origin_y, render_pixels_at);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_bmp(path, origin_x, origin_y, render_pixels_at);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_jpeg(path, origin_x, origin_y, render_pixels_at);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_ppm(path, origin_x, origin_y, render_pixels_at);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    set_error("File '%s' is not a supported PNG, BMP, JPEG, or PPM image", path);
    return LIBIMAGE_UNSUPPORTED_FORMAT;
}

LibImageResult libimage_render_file_streamed_at(const char *path, int origin_x, int origin_y) {
    if (path == NULL) {
        set_error("Image path is NULL");
        return LIBIMAGE_INVALID_ARGUMENT;
    }
    if (origin_x < 0 || origin_y < 0) {
        set_error("Image coordinates must be non-negative");
        return LIBIMAGE_INVALID_ARGUMENT;
    }

    LibImageResult result = render_png(path, origin_x, origin_y, render_pixels_streamed);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_bmp(path, origin_x, origin_y, render_pixels_streamed);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_jpeg(path, origin_x, origin_y, render_pixels_streamed);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    result = render_ppm(path, origin_x, origin_y, render_pixels_streamed);
    if (result == LIBIMAGE_SUCCESS) {
        return LIBIMAGE_SUCCESS;
    }
    if (result != LIBIMAGE_UNSUPPORTED_FORMAT) {
        return result;
    }

    set_error("File '%s' is not a supported PNG, BMP, JPEG, or PPM image", path);
    return LIBIMAGE_UNSUPPORTED_FORMAT;
}
