#define _POSIX_C_SOURCE 200809L
#define STB_TRUETYPE_IMPLEMENTATION
#include "../lib/stb_truetype.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_WIDTH 8u
#define DEFAULT_HEIGHT 8u
#define DEFAULT_FIRST 0u
#define DEFAULT_COUNT 256u
#define DEFAULT_FALLBACK_CODEPOINT 0x3Fu

#define PSF2_MAGIC 0x864ab572u
#define PSF2_HEADER_SIZE 32u

struct conversion_options {
    uint32_t width;
    uint32_t height;
    uint32_t first_codepoint;
    uint32_t glyph_count;
    uint32_t fallback_codepoint;
};

struct psf_font {
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t glyph_size;
    uint8_t *glyphs;
};

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-w width] [-h height] [-f first_codepoint] [-c glyph_count] [-b fallback] <input.ttf> <output.psf>\n"
            "\n"
            "Options:\n"
            "  -w width            Width of each glyph in pixels (default %u).\n"
            "  -h height           Height of each glyph in pixels (default %u).\n"
            "  -f first_codepoint  First Unicode codepoint to export (default %u).\n"
            "  -c glyph_count      Number of glyphs to export (default %u).\n"
            "  -b fallback         Fallback codepoint for missing glyphs (default '?' / U+%04X).\n",
            prog,
            DEFAULT_WIDTH,
            DEFAULT_HEIGHT,
            DEFAULT_FIRST,
            DEFAULT_COUNT,
            DEFAULT_FALLBACK_CODEPOINT);
}

static int parse_u32_option(const char *name, const char *value, uint32_t min, uint32_t max, uint32_t *out) {
    if (!value || !out) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long parsed = strtoul(value, &endptr, 0);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "Invalid value for %s: '%s'\n", name, value);
        return -1;
    }
    if (parsed < min || parsed > max) {
        fprintf(stderr, "%s must be in range [%lu, %lu]\n", name, (unsigned long)min, (unsigned long)max);
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static int parse_codepoint_option(const char *name, const char *value, uint32_t *out) {
    if (!value || !out) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long parsed = strtoul(value, &endptr, 0);
    if (errno == 0 && endptr && *endptr == '\0') {
        if (parsed > UINT32_MAX) {
            fprintf(stderr, "%s is out of range\n", name);
            return -1;
        }
        *out = (uint32_t)parsed;
        return 0;
    }

    if (value[0] != '\0' && value[1] == '\0') {
        *out = (uint32_t)(unsigned char)value[0];
        return 0;
    }

    fprintf(stderr, "Invalid value for %s: '%s'\n", name, value);
    return -1;
}

static void free_psf_font(struct psf_font *font) {
    if (!font) {
        return;
    }
    free(font->glyphs);
    font->glyphs = NULL;
    font->glyph_count = 0;
    font->width = 0;
    font->height = 0;
    font->stride = 0;
    font->glyph_size = 0;
}

static void write_u32_le(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8u) & 0xFFu);
    dst[2] = (unsigned char)((value >> 16u) & 0xFFu);
    dst[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static int write_psf2(const char *path, const struct psf_font *font) {
    if (!path || !font) {
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open '%s' for writing: %s\n", path, strerror(errno));
        return -1;
    }

    unsigned char header[PSF2_HEADER_SIZE];
    memset(header, 0, sizeof(header));
    write_u32_le(header + 0, PSF2_MAGIC);
    write_u32_le(header + 4, 0u);
    write_u32_le(header + 8, PSF2_HEADER_SIZE);
    write_u32_le(header + 12, 0u);
    write_u32_le(header + 16, font->glyph_count);
    write_u32_le(header + 20, font->glyph_size);
    write_u32_le(header + 24, font->height);
    write_u32_le(header + 28, font->width);

    if (fwrite(header, sizeof(header), 1u, fp) != 1u) {
        fprintf(stderr, "Failed to write PSF header: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    size_t expected = (size_t)font->glyph_size * (size_t)font->glyph_count;
    if (fwrite(font->glyphs, 1u, expected, fp) != expected) {
        fprintf(stderr, "Failed to write glyph data: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "Failed to close '%s': %s\n", path, strerror(errno));
        return -1;
    }

    return 0;
}

static int read_file(const char *path, unsigned char **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek '%s': %s\n", path, strerror(errno));
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fprintf(stderr, "Failed to determine size of '%s': %s\n", path, strerror(errno));
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to rewind '%s': %s\n", path, strerror(errno));
        fclose(fp);
        return -1;
    }

    if (file_size == 0) {
        fprintf(stderr, "File '%s' is empty\n", path);
        fclose(fp);
        return -1;
    }

    size_t size = (size_t)file_size;
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        fprintf(stderr, "Out of memory reading '%s'\n", path);
        fclose(fp);
        return -1;
    }

    size_t read = fread(data, 1u, size, fp);
    if (read != size) {
        fprintf(stderr, "Failed to read '%s': %s\n", path, strerror(errno));
        free(data);
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "Failed to close '%s': %s\n", path, strerror(errno));
        free(data);
        return -1;
    }

    *out_data = data;
    *out_size = size;
    return 0;
}

static int convert_font(const char *input_path, const char *output_path, const struct conversion_options *options) {
    if (!input_path || !output_path || !options) {
        return -1;
    }

    if (options->width == 0u || options->height == 0u) {
        fprintf(stderr, "Width and height must be greater than zero\n");
        return -1;
    }

    unsigned char *font_buffer = NULL;
    size_t font_size = 0u;
    if (read_file(input_path, &font_buffer, &font_size) != 0) {
        return -1;
    }
    (void)font_size;

    int font_offset = stbtt_GetFontOffsetForIndex(font_buffer, 0);
    if (font_offset < 0) {
        fprintf(stderr, "'%s' does not contain a valid font\n", input_path);
        free(font_buffer);
        return -1;
    }

    stbtt_fontinfo font_info;
    if (stbtt_InitFont(&font_info, font_buffer, font_offset) == 0) {
        fprintf(stderr, "Failed to initialize font '%s'\n", input_path);
        free(font_buffer);
        return -1;
    }

    float height_px = (float)options->height;
    if (height_px <= 0.0f) {
        fprintf(stderr, "Invalid height %lu\n", (unsigned long)options->height);
        free(font_buffer);
        return -1;
    }

    float scale = stbtt_ScaleForPixelHeight(&font_info, height_px);
    if (scale <= 0.0f) {
        fprintf(stderr, "Failed to compute scale for '%s'\n", input_path);
        free(font_buffer);
        return -1;
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    (void)line_gap;
    int baseline = (int)(scale * (float)ascent + 0.5f);
    int descent_px = (int)(-scale * (float)descent + 0.5f);
    if (descent_px < 0) {
        descent_px = 0;
    }
    if (baseline < 0) {
        baseline = 0;
    }
    if (baseline + descent_px > (int)options->height) {
        baseline = (int)options->height - descent_px;
        if (baseline < 0) {
            baseline = 0;
        }
    }

    size_t stride = (size_t)((options->width + 7u) / 8u);
    uint64_t glyph_size64 = (uint64_t)stride * (uint64_t)options->height;
    if (glyph_size64 == 0u || glyph_size64 > UINT32_MAX) {
        fprintf(stderr, "Glyph dimensions are too large\n");
        free(font_buffer);
        return -1;
    }

    uint64_t total_size64 = glyph_size64 * (uint64_t)options->glyph_count;
    if (total_size64 == 0u || total_size64 > SIZE_MAX) {
        fprintf(stderr, "Requested glyph count is too large\n");
        free(font_buffer);
        return -1;
    }

    uint8_t *glyph_data = (uint8_t *)calloc((size_t)total_size64, 1u);
    if (!glyph_data) {
        fprintf(stderr, "Out of memory allocating glyph data\n");
        free(font_buffer);
        return -1;
    }

    struct psf_font font = {
        .glyph_count = options->glyph_count,
        .width = options->width,
        .height = options->height,
        .stride = (uint32_t)stride,
        .glyph_size = (uint32_t)glyph_size64,
        .glyphs = glyph_data,
    };

    int fallback_index = stbtt_FindGlyphIndex(&font_info, (int)options->fallback_codepoint);
    uint32_t fallback_used = 0u;
    uint32_t missing_without_fallback = 0u;

    for (uint32_t i = 0; i < options->glyph_count; ++i) {
        uint32_t codepoint = options->first_codepoint + i;
        int glyph_index = stbtt_FindGlyphIndex(&font_info, (int)codepoint);
        int render_codepoint = (int)codepoint;

        if (glyph_index == 0 && codepoint != 0u) {
            if (fallback_index != 0) {
                render_codepoint = (int)options->fallback_codepoint;
                ++fallback_used;
            } else {
                ++missing_without_fallback;
                continue;
            }
        }

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&font_info, render_codepoint, scale, scale, 0.0f, 0.0f, &x0, &y0, &x1, &y1);

        int glyph_width = x1 - x0;
        int glyph_height = y1 - y0;
        if (glyph_width <= 0 || glyph_height <= 0) {
            continue;
        }

        size_t temp_size = (size_t)glyph_width * (size_t)glyph_height;
        unsigned char *bitmap = (unsigned char *)malloc(temp_size);
        if (!bitmap) {
            fprintf(stderr, "Out of memory rendering glyph U+%04X\n", (unsigned int)codepoint);
            free_psf_font(&font);
            free(font_buffer);
            return -1;
        }

        stbtt_MakeCodepointBitmapSubpixel(&font_info,
                                          bitmap,
                                          glyph_width,
                                          glyph_height,
                                          glyph_width,
                                          scale,
                                          scale,
                                          0.0f,
                                          0.0f,
                                          render_codepoint);

        int dest_left = 0;
        if (font.width > (uint32_t)glyph_width) {
            dest_left = (int)((font.width - (uint32_t)glyph_width) / 2u);
        }

        int dest_top = baseline + y0;

        uint8_t *glyph_out = font.glyphs + (size_t)i * font.glyph_size;
        for (int y = 0; y < glyph_height; ++y) {
            int dest_y = dest_top + y;
            if (dest_y < 0 || dest_y >= (int)font.height) {
                continue;
            }
            size_t row_offset = (size_t)dest_y * (size_t)font.stride;
            for (int x = 0; x < glyph_width; ++x) {
                int dest_x = dest_left + x;
                if (dest_x < 0 || dest_x >= (int)font.width) {
                    continue;
                }
                unsigned char value = bitmap[(size_t)y * (size_t)glyph_width + (size_t)x];
                if (value >= 128u) {
                    size_t byte_index = row_offset + (size_t)(dest_x / 8);
                    uint8_t mask = (uint8_t)(0x80u >> (dest_x % 8));
                    glyph_out[byte_index] |= mask;
                }
            }
        }

        free(bitmap);
    }

    if (fallback_used > 0u) {
        fprintf(stderr,
                "Warning: %lu codepoints were missing and replaced with fallback U+%04X.\n",
                (unsigned long)fallback_used,
                (unsigned int)options->fallback_codepoint);
    }
    if (missing_without_fallback > 0u) {
        fprintf(stderr,
                "Warning: %lu codepoints missing from font with no fallback available.\n",
                (unsigned long)missing_without_fallback);
    }

    int result = write_psf2(output_path, &font);
    free_psf_font(&font);
    free(font_buffer);

    if (result == 0) {
        printf("Wrote %lu glyphs (%lux%lu) to '%s'.\n",
               (unsigned long)options->glyph_count,
               (unsigned long)options->width,
               (unsigned long)options->height,
               output_path);
    }

    return result;
}

int main(int argc, char **argv) {
    struct conversion_options options = {
        .width = DEFAULT_WIDTH,
        .height = DEFAULT_HEIGHT,
        .first_codepoint = DEFAULT_FIRST,
        .glyph_count = DEFAULT_COUNT,
        .fallback_codepoint = DEFAULT_FALLBACK_CODEPOINT,
    };

    int opt = 0;
    while ((opt = getopt(argc, argv, "w:h:f:c:b:")) != -1) {
        switch (opt) {
        case 'w':
            if (parse_u32_option("width", optarg, 1u, UINT32_MAX, &options.width) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'h':
            if (parse_u32_option("height", optarg, 1u, UINT32_MAX, &options.height) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            if (parse_u32_option("first_codepoint", optarg, 0u, UINT32_MAX, &options.first_codepoint) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'c':
            if (parse_u32_option("glyph_count", optarg, 1u, UINT32_MAX, &options.glyph_count) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'b':
            if (parse_codepoint_option("fallback", optarg, &options.fallback_codepoint) != 0) {
                return EXIT_FAILURE;
            }
            break;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind + 2 > argc) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[optind];
    const char *output_path = argv[optind + 1];

    if (convert_font(input_path, output_path, &options) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
