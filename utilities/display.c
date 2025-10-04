#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    char letter;
    int term256;
} Color;

typedef enum {
    FILETYPE_TEXT,
    FILETYPE_BMP,
    FILETYPE_PPM
} FileType;

static FileType detect_file_type(const char *path);
static int display_text(const char *path);
static int display_bmp(const char *path);
static int display_ppm(const char *path);
static void render_pixels(const Pixel *pixels, int width, int height);

#define PALETTE_VARIANTS 5
#define PALETTE_COLORS 26
#define TOTAL_COLORS (PALETTE_VARIANTS * PALETTE_COLORS)

static const Color base_palette[PALETTE_COLORS] = {
    {  0,  0,  0,'A', 16},
    {255,255,255,'B',231},
    {128,128,128,'C',244},
    {255,  0,  0,'D',196},
    {  0,255,  0,'E', 46},
    {  0,  0,255,'F', 21},
    {  0,255,255,'G', 51},
    {255,  0,255,'H',201},
    {255,255,  0,'I',226},
    {255,165,  0,'J',214},
    {165, 42, 42,'K', 94},
    {128,  0,128,'L',129},
    {255,192,203,'M',218},
    {135,206,235,'N',117},
    {144,238,144,'O',120},
    {139,  0,  0,'P', 88},
    {  0,100,  0,'Q', 22},
    {  0,  0,139,'R', 19},
    {  0,128,128,'S', 30},
    {128,128,  0,'T', 58},
    {  0,  0, 75,'U', 17},
    {210,105, 30,'V',166},
    {173,216,230,'W',153},
    { 75,  0,130,'X', 55},
    { 47, 79, 79,'Y', 23},
    {112,128,144,'Z',102}
};

static Color palettes[PALETTE_VARIANTS][PALETTE_COLORS];
static int palettes_initialized = 0;

static uint8_t clamp_u8(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static int component_to_level(uint8_t v) {
    int level = (v * 5 + 127) / 255;
    if (level < 0) {
        level = 0;
    }
    if (level > 5) {
        level = 5;
    }
    return level;
}

static int rgb_to_ansi256(uint8_t r, uint8_t g, uint8_t b) {
    if (r == g && g == b) {
        if (r < 8) {
            return 16;
        }
        if (r > 248) {
            return 231;
        }
        int gray = (r - 8) / 10;
        if (gray > 23) {
            gray = 23;
        }
        return 232 + gray;
    }
    int ri = component_to_level(r);
    int gi = component_to_level(g);
    int bi = component_to_level(b);
    return 16 + 36 * ri + 6 * gi + bi;
}

static uint8_t apply_brightness(uint8_t value, float factor) {
    int adjusted = (int)(value * factor + 0.5f);
    return clamp_u8(adjusted);
}

static void init_palettes(void) {
    if (palettes_initialized) {
        return;
    }
    const float factors[PALETTE_VARIANTS] = {0.6f, 0.8f, 1.0f, 1.2f, 1.4f};
    for (int variant = 0; variant < PALETTE_VARIANTS; ++variant) {
        for (int i = 0; i < PALETTE_COLORS; ++i) {
            Color c = base_palette[i];
            if (variant != 2) {
                c.r = apply_brightness(base_palette[i].r, factors[variant]);
                c.g = apply_brightness(base_palette[i].g, factors[variant]);
                c.b = apply_brightness(base_palette[i].b, factors[variant]);
                c.term256 = rgb_to_ansi256(c.r, c.g, c.b);
            }
            palettes[variant][i] = c;
        }
    }
    palettes_initialized = 1;
}

static const Color *color_from_variant(int variant, int index) {
    init_palettes();
    if (variant < 0 || variant >= PALETTE_VARIANTS) {
        return NULL;
    }
    if (index < 0 || index >= PALETTE_COLORS) {
        return NULL;
    }
    return &palettes[variant][index];
}

static const Color *color_from_index(int idx) {
    init_palettes();
    if (idx < 0 || idx >= TOTAL_COLORS) {
        return NULL;
    }
    int variant = idx / PALETTE_COLORS;
    int color_index = idx % PALETTE_COLORS;
    return color_from_variant(variant, color_index);
}

static int best_palette_match(uint8_t r, uint8_t g, uint8_t b) {
    init_palettes();
    int best_index = -1;
    int best_distance = INT_MAX;
    for (int i = 0; i < TOTAL_COLORS; ++i) {
        const Color *c = color_from_index(i);
        if (c == NULL) {
            continue;
        }
        int dr = (int)r - (int)c->r;
        int dg = (int)g - (int)c->g;
        int db = (int)b - (int)c->b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
            if (distance == 0) {
                break;
            }
        }
    }
    return best_index;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: display <file>\n");
        return EXIT_FAILURE;
    }

    const char *path = argv[1];
    FileType type = detect_file_type(path);

    switch (type) {
        case FILETYPE_BMP:
            return display_bmp(path);
        case FILETYPE_PPM:
            return display_ppm(path);
        case FILETYPE_TEXT:
        default:
            return display_text(path);
    }
}

static FileType detect_file_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot != NULL) {
        if (strcasecmp(dot, ".bmp") == 0) {
            return FILETYPE_BMP;
        }
        if (strcasecmp(dot, ".ppm") == 0) {
            return FILETYPE_PPM;
        }
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return FILETYPE_TEXT;
    }

    unsigned char magic[2];
    size_t n = fread(magic, 1, sizeof(magic), f);
    fclose(f);

    if (n == sizeof(magic)) {
        if (magic[0] == 'B' && magic[1] == 'M') {
            return FILETYPE_BMP;
        }
        if (magic[0] == 'P' && magic[1] == '6') {
            return FILETYPE_PPM;
        }
    }

    return FILETYPE_TEXT;
}

static int display_text(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror("fopen failed");
        return EXIT_FAILURE;
    }

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (putchar(ch) == EOF) {
            perror("putchar failed");
            fclose(fp);
            return EXIT_FAILURE;
        }
    }

    if (ferror(fp) != 0) {
        perror("fgetc failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);
    return EXIT_SUCCESS;
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

static int display_bmp(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        perror("fopen failed");
        return EXIT_FAILURE;
    }

    BMPFILEHEADER file_header;
    BMPINFOHEADER info_header;

    if (fread(&file_header, sizeof(file_header), 1, fp) != 1 ||
        fread(&info_header, sizeof(info_header), 1, fp) != 1) {
        fprintf(stderr, "Failed to read BMP header from '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (file_header.bfType != 0x4D42 || info_header.biBitCount != 24 ||
        info_header.biCompression != 0 || info_header.biPlanes != 1) {
        fprintf(stderr, "Unsupported BMP format in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    int width = info_header.biWidth;
    int height = info_header.biHeight;
    int flip_vertical = 0;
    if (height < 0) {
        flip_vertical = 1;
        height = -height;
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Invalid BMP dimensions in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (fseek(fp, (long)file_header.bfOffBits, SEEK_SET) != 0) {
        perror("fseek failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (width != 0 && pixel_count / (size_t)width != (size_t)height) {
        fprintf(stderr, "BMP dimensions overflow in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (pixel_count > SIZE_MAX / sizeof(Pixel)) {
        fprintf(stderr, "BMP image too large in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    Pixel *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        perror("malloc failed");
        fclose(fp);
        return EXIT_FAILURE;
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
                fprintf(stderr, "Unexpected EOF in BMP pixel data for '%s'\n", path);
                free(pixels);
                fclose(fp);
                return EXIT_FAILURE;
            }
            row[x].r = (uint8_t)r;
            row[x].g = (uint8_t)g;
            row[x].b = (uint8_t)b;
        }
        for (size_t p = 0; p < padding; ++p) {
            if (fgetc(fp) == EOF) {
                fprintf(stderr, "Unexpected EOF in BMP padding for '%s'\n", path);
                free(pixels);
                fclose(fp);
                return EXIT_FAILURE;
            }
        }
    }

    fclose(fp);

    render_pixels(pixels, width, height);
    free(pixels);

    return EXIT_SUCCESS;
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

static int display_ppm(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        perror("fopen failed");
        return EXIT_FAILURE;
    }

    char token[64];
    if (!read_ppm_token(fp, token, sizeof(token)) || strcmp(token, "P6") != 0) {
        fprintf(stderr, "Unsupported PPM format in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fprintf(stderr, "Missing width in PPM file '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }
    long width_long = strtol(token, NULL, 10);

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fprintf(stderr, "Missing height in PPM file '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }
    long height_long = strtol(token, NULL, 10);

    if (!read_ppm_token(fp, token, sizeof(token))) {
        fprintf(stderr, "Missing max value in PPM file '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }
    long max_value = strtol(token, NULL, 10);

    if (width_long <= 0 || height_long <= 0 || width_long > INT32_MAX ||
        height_long > INT32_MAX) {
        fprintf(stderr, "Invalid PPM dimensions in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }
    if (max_value <= 0 || max_value > 255) {
        fprintf(stderr, "Unsupported PPM max value in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    int width = (int)width_long;
    int height = (int)height_long;
    size_t pixel_count = (size_t)width * (size_t)height;
    if (width != 0 && pixel_count / (size_t)width != (size_t)height) {
        fprintf(stderr, "PPM dimensions overflow in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (pixel_count > SIZE_MAX / sizeof(Pixel)) {
        fprintf(stderr, "PPM image too large in '%s'\n", path);
        fclose(fp);
        return EXIT_FAILURE;
    }

    Pixel *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        perror("malloc failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (pixel_count > SIZE_MAX / 3U) {
        fprintf(stderr, "PPM pixel data too large in '%s'\n", path);
        free(pixels);
        fclose(fp);
        return EXIT_FAILURE;
    }

    size_t raw_size = pixel_count * 3U;
    uint8_t *raw = malloc(raw_size);
    if (raw == NULL) {
        perror("malloc failed");
        free(pixels);
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (fread(raw, 1, raw_size, fp) != raw_size) {
        fprintf(stderr, "Unexpected EOF in PPM pixel data for '%s'\n", path);
        free(raw);
        free(pixels);
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);

    for (size_t i = 0; i < pixel_count; ++i) {
        pixels[i].r = raw[i * 3 + 0];
        pixels[i].g = raw[i * 3 + 1];
        pixels[i].b = raw[i * 3 + 2];
    }

    free(raw);

    if (max_value != 255) {
        for (size_t i = 0; i < pixel_count; ++i) {
            pixels[i].r = (uint8_t)((pixels[i].r * 255U) / (uint32_t)max_value);
            pixels[i].g = (uint8_t)((pixels[i].g * 255U) / (uint32_t)max_value);
            pixels[i].b = (uint8_t)((pixels[i].b * 255U) / (uint32_t)max_value);
        }
    }

    render_pixels(pixels, width, height);
    free(pixels);

    return EXIT_SUCCESS;
}

static void render_pixels(const Pixel *pixels, int width, int height) {
    if (pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    init_palettes();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Pixel *p = &pixels[(size_t)y * (size_t)width + (size_t)x];
            int palette_index = best_palette_match(p->r, p->g, p->b);
            const Color *color = color_from_index(palette_index);
            if (color != NULL) {
                char seq[32];
                int len = snprintf(seq, sizeof(seq), "\x1b[48;5;%dm", color->term256);
                if (len > 0) {
                    fwrite(seq, 1, (size_t)len, stdout);
                }
                fputs("\x1b[39m", stdout);
                fputc(' ', stdout);
                fputs("\x1b[49m", stdout);
            } else {
                fputs("\x1b[49m\x1b[39m.", stdout);
            }
        }
        fputs("\x1b[49m\x1b[39m\n", stdout);
    }
    fputs("\x1b[49m\x1b[39m", stdout);
}
