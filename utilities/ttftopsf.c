#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef TTFTOPSF_DEFAULT_PIXEL_SIZE
#define TTFTOPSF_DEFAULT_PIXEL_SIZE 16U
#endif

struct glyph_entry {
    FT_ULong codepoint;
    FT_UInt glyph_index;
};

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] <font.ttf>\n"
            "Options:\n"
            "  -o <file>   Output PSF file path (default: input name with .psf).\n"
            "  -s <size>   Cell size (height and width) in pixels (default: %u).\n"
            "  -W <width>  Cell width in pixels (overrides -s).\n"
            "  -H <height> Cell height in pixels (overrides -s).\n"
            "  -g <count>  Limit number of glyphs to export (default: all).\n"
            "  -h, --help  Show this help and exit.\n",
            prog,
            TTFTOPSF_DEFAULT_PIXEL_SIZE);
}

static bool
parse_unsigned(const char *text, unsigned int *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out = (unsigned int)value;
    return true;
}

static void
write_le32(FILE *fp, uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    if (fwrite(bytes, sizeof(bytes), 1, fp) != 1) {
        fprintf(stderr, "Failed to write output: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static unsigned char *
allocate_glyph_buffer(size_t glyphs, size_t charsize)
{
    size_t total = glyphs * charsize;
    unsigned char *buffer = calloc(total, sizeof(*buffer));
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate %zu bytes for glyph data.\n", total);
        exit(EXIT_FAILURE);
    }
    return buffer;
}

static void
blit_bitmap(unsigned char *glyph_data,
            size_t glyph_index,
            size_t charsize,
            unsigned int cell_width,
            unsigned int cell_height,
            int baseline,
            const FT_Bitmap *bitmap,
            int bitmap_left,
            int bitmap_top)
{
    if (glyph_data == NULL || bitmap == NULL) {
        return;
    }

    size_t row_bytes = (cell_width + 7u) / 8u;
    unsigned char *dest = glyph_data + glyph_index * charsize;

    int y_offset = baseline - bitmap_top;
    if (y_offset < 0) {
        y_offset = 0;
    }
    if (y_offset + (int)bitmap->rows > (int)cell_height) {
        y_offset = (int)cell_height - (int)bitmap->rows;
    }
    if (y_offset < 0) {
        return;
    }

    int x_offset = bitmap_left;
    if (x_offset < 0) {
        x_offset = 0;
    }
    if (x_offset + (int)bitmap->width > (int)cell_width) {
        x_offset = (int)cell_width - (int)bitmap->width;
        if (x_offset < 0) {
            x_offset = 0;
        }
    }

    for (unsigned int y = 0; y < bitmap->rows; ++y) {
        int target_row = y_offset + (int)y;
        if (target_row < 0 || target_row >= (int)cell_height) {
            continue;
        }

        int pitch = bitmap->pitch;
        const unsigned char *src = NULL;
        if (pitch >= 0) {
            src = bitmap->buffer + (size_t)y * (size_t)pitch;
        } else {
            size_t positive_pitch = (size_t)(-pitch);
            src = bitmap->buffer + ((size_t)(bitmap->rows - 1u - y) * positive_pitch);
        }

        unsigned char *row_ptr = dest + (size_t)target_row * row_bytes;

        switch (bitmap->pixel_mode) {
        case FT_PIXEL_MODE_MONO:
            for (unsigned int x = 0; x < bitmap->width; ++x) {
                int target_col = x_offset + (int)x;
                if (target_col < 0 || target_col >= (int)cell_width) {
                    continue;
                }
                unsigned char byte = src[x / 8u];
                if ((byte & (0x80u >> (x % 8u))) == 0) {
                    continue;
                }
                size_t dest_index = (size_t)target_col / 8u;
                unsigned int bit = target_col % 8u;
                row_ptr[dest_index] |= (unsigned char)(0x80u >> bit);
            }
            break;
        case FT_PIXEL_MODE_GRAY: {
            unsigned int threshold = bitmap->num_grays > 1 ? bitmap->num_grays / 2u : 1u;
            for (unsigned int x = 0; x < bitmap->width; ++x) {
                int target_col = x_offset + (int)x;
                if (target_col < 0 || target_col >= (int)cell_width) {
                    continue;
                }
                unsigned char value = src[x];
                if (value < threshold) {
                    continue;
                }
                size_t dest_index = (size_t)target_col / 8u;
                unsigned int bit = target_col % 8u;
                row_ptr[dest_index] |= (unsigned char)(0x80u >> bit);
            }
            break;
        }
        default:
            break;
        }
    }
}

int
main(int argc, char **argv)
{
    const char *output_path = NULL;
    const char *input_path = NULL;
    unsigned int cell_height = TTFTOPSF_DEFAULT_PIXEL_SIZE;
    unsigned int cell_width = TTFTOPSF_DEFAULT_PIXEL_SIZE;
    unsigned int glyph_limit = 0;
    unsigned char *glyph_data = NULL;
    struct glyph_entry *entries = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -o.\n");
                return EXIT_FAILURE;
            }
            output_path = argv[++i];
        } else if (strcmp(arg, "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -s.\n");
                return EXIT_FAILURE;
            }
            unsigned int size = 0;
            if (!parse_unsigned(argv[++i], &size) || size == 0) {
                fprintf(stderr, "Invalid size value: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
            cell_height = size;
            cell_width = size;
        } else if (strcmp(arg, "-W") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -W.\n");
                return EXIT_FAILURE;
            }
            unsigned int width = 0;
            if (!parse_unsigned(argv[++i], &width) || width == 0) {
                fprintf(stderr, "Invalid width value: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
            cell_width = width;
        } else if (strcmp(arg, "-H") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -H.\n");
                return EXIT_FAILURE;
            }
            unsigned int height = 0;
            if (!parse_unsigned(argv[++i], &height) || height == 0) {
                fprintf(stderr, "Invalid height value: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
            cell_height = height;
        } else if (strcmp(arg, "-g") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -g.\n");
                return EXIT_FAILURE;
            }
            unsigned int count = 0;
            if (!parse_unsigned(argv[++i], &count) || count == 0) {
                fprintf(stderr, "Invalid glyph count: %s.\n", argv[i]);
                return EXIT_FAILURE;
            }
            glyph_limit = count;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            usage(argv[0]);
            return EXIT_FAILURE;
        } else if (input_path == NULL) {
            input_path = arg;
        } else {
            fprintf(stderr, "Unexpected positional argument: %s\n", arg);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (input_path == NULL) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    char auto_output[4096];
    if (output_path == NULL) {
        const char *dot = strrchr(input_path, '.');
        size_t len = strlen(input_path);
        if (dot != NULL) {
            len = (size_t)(dot - input_path);
        }
        if (len >= sizeof(auto_output) - 5) {
            fprintf(stderr, "Input path is too long to derive output name.\n");
            return EXIT_FAILURE;
        }
        memcpy(auto_output, input_path, len);
        memcpy(auto_output + len, ".psf", 5);
        output_path = auto_output;
    }

    if (cell_width == 0 || cell_height == 0) {
        fprintf(stderr, "Cell dimensions must be greater than zero.\n");
        return EXIT_FAILURE;
    }

    FT_Library library = NULL;
    FT_Face face = NULL;
    FT_Error ft_error = FT_Init_FreeType(&library);
    if (ft_error != 0) {
        fprintf(stderr, "Failed to initialize FreeType: 0x%02X\n", ft_error);
        free(glyph_data);
        return EXIT_FAILURE;
    }

    ft_error = FT_New_Face(library, input_path, 0, &face);
    if (ft_error != 0) {
        fprintf(stderr, "Failed to load font '%s': 0x%02X\n", input_path, ft_error);
        FT_Done_FreeType(library);
        free(glyph_data);
        return EXIT_FAILURE;
    }

    ft_error = FT_Set_Pixel_Sizes(face, cell_width, cell_height);
    if (ft_error != 0) {
        fprintf(stderr, "Failed to set pixel size %ux%u: 0x%02X\n", cell_width, cell_height, ft_error);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return EXIT_FAILURE;
    }

    if (face->charmap == NULL || face->charmap->encoding != FT_ENCODING_UNICODE) {
        FT_Error select_error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        if (select_error != 0) {
            fprintf(stderr, "Font lacks a Unicode charmap (error 0x%02X).\n", select_error);
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return EXIT_FAILURE;
        }
    }

    FT_ULong charcode = 0;
    FT_UInt glyph_index = 0;
    size_t available_glyphs = 0;
    for (charcode = FT_Get_First_Char(face, &glyph_index); glyph_index != 0;
         charcode = FT_Get_Next_Char(face, charcode, &glyph_index)) {
        ++available_glyphs;
    }

    if (available_glyphs == 0) {
        fprintf(stderr, "Font does not expose any encodable glyphs.\n");
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return EXIT_FAILURE;
    }

    if (available_glyphs > (size_t)UINT_MAX) {
        available_glyphs = (size_t)UINT_MAX;
    }

    unsigned int export_glyphs = (unsigned int)available_glyphs;
    if (glyph_limit > 0 && glyph_limit < export_glyphs) {
        export_glyphs = glyph_limit;
    }

    entries = calloc(export_glyphs, sizeof(*entries));
    if (entries == NULL) {
        fprintf(stderr, "Failed to allocate %zu glyph entries.\n", (size_t)export_glyphs);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return EXIT_FAILURE;
    }

    size_t filled = 0;
    for (charcode = FT_Get_First_Char(face, &glyph_index);
         glyph_index != 0 && filled < export_glyphs;
         charcode = FT_Get_Next_Char(face, charcode, &glyph_index)) {
        entries[filled].codepoint = charcode;
        entries[filled].glyph_index = glyph_index;
        ++filled;
    }

    if (filled == 0) {
        fprintf(stderr, "Failed to enumerate glyphs from font charmap.\n");
        free(entries);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return EXIT_FAILURE;
    }

    export_glyphs = (unsigned int)filled;

    size_t row_bytes = (cell_width + 7u) / 8u;
    size_t charsize = row_bytes * (size_t)cell_height;
    glyph_data = allocate_glyph_buffer(export_glyphs, charsize);

    int baseline = 0;
    if (face->size != NULL) {
        baseline = (int)(face->size->metrics.ascender >> 6);
        int descender = (int)(-face->size->metrics.descender >> 6);
        if (baseline < 0) {
            baseline = 0;
        }
        if (descender < 0) {
            descender = 0;
        }
        if (baseline + descender > (int)cell_height) {
            baseline = (int)cell_height - descender;
        }
        if (baseline > (int)cell_height) {
            baseline = (int)cell_height;
        }
    }

    for (unsigned int i = 0; i < export_glyphs; ++i) {
        ft_error = FT_Load_Glyph(face, entries[i].glyph_index, FT_LOAD_DEFAULT);
        if (ft_error != 0) {
            continue;
        }
        ft_error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (ft_error != 0) {
            continue;
        }
        blit_bitmap(glyph_data,
                    i,
                    charsize,
                    cell_width,
                    cell_height,
                    baseline,
                    &face->glyph->bitmap,
                    face->glyph->bitmap_left,
                    face->glyph->bitmap_top);
    }

    FILE *out = fopen(output_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "Failed to open output '%s': %s\n", output_path, strerror(errno));
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        free(glyph_data);
        free(entries);
        return EXIT_FAILURE;
    }

    const uint32_t psf2_magic = 0x864ab572u;
    const uint32_t header_size = 32u;
    const uint32_t flags = 0x1u;

    write_le32(out, psf2_magic);
    write_le32(out, 0u);
    write_le32(out, header_size);
    write_le32(out, flags);
    write_le32(out, export_glyphs);
    write_le32(out, (uint32_t)charsize);
    write_le32(out, cell_height);
    write_le32(out, cell_width);

    if (fwrite(glyph_data, charsize, export_glyphs, out) != export_glyphs) {
        fprintf(stderr, "Failed to write glyph bitmap data: %s\n", strerror(errno));
        fclose(out);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        free(glyph_data);
        free(entries);
        return EXIT_FAILURE;
    }

    for (unsigned int i = 0; i < export_glyphs; ++i) {
        uint32_t codepoint = (uint32_t)entries[i].codepoint;
        write_le32(out, codepoint);
        write_le32(out, 0xFFFFFFFFu);
    }
    write_le32(out, 0xFFFFFFFFu);

    if (fclose(out) != 0) {
        fprintf(stderr, "Failed to close output file: %s\n", strerror(errno));
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        free(glyph_data);
        free(entries);
        return EXIT_FAILURE;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    free(glyph_data);
    free(entries);

    return EXIT_SUCCESS;
}
