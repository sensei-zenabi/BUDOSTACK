#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <sys/movedata.h>
#include <pc.h>

#include "PCX.H"

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  200
#define SCREEN_SIZE    64000
#define VGA_MEMORY     0xA0000

/*
 * Engine-reserved VGA palette entries.
 * These colors are always rewritten after a PCX palette is loaded, so
 * UI visibility never depends on the artwork palette.
 *
 * Artwork should avoid palette indices 0 and 252-255.
 */
#define UI_BLACK        0
#define UI_DARK_GRAY    252
#define UI_MID_GRAY     253
#define UI_LIGHT_GRAY   254
#define UI_WHITE_GRAY   255

#pragma pack(1)
typedef struct
{
    unsigned char manufacturer;
    unsigned char version;
    unsigned char encoding;
    unsigned char bits_per_pixel;
    unsigned short xmin;
    unsigned short ymin;
    unsigned short xmax;
    unsigned short ymax;
    unsigned short hres;
    unsigned short vres;
    unsigned char palette16[48];
    unsigned char reserved;
    unsigned char color_planes;
    unsigned short bytes_per_line;
    unsigned short palette_type;
    unsigned short hscreen_size;
    unsigned short vscreen_size;
    unsigned char filler[54];
} PCXHeader;
#pragma pack()

static void pcx_set_video_mode(int mode)
{
    __dpmi_regs r;

    memset(&r, 0, sizeof(r));
    r.x.ax = (unsigned short)mode;
    __dpmi_int(0x10, &r);
}

static void display_fullscreen(const unsigned char *pixels)
{
    dosmemput(pixels, SCREEN_SIZE, VGA_MEMORY);
}

static void set_dac_color(int index, int gray)
{
    outportb(0x3C8, index);
    outportb(0x3C9, gray);
    outportb(0x3C9, gray);
    outportb(0x3C9, gray);
}

static void apply_engine_ui_palette(void)
{
    /* VGA DAC components are 0..63. */
    set_dac_color(UI_BLACK,       0);
    set_dac_color(UI_DARK_GRAY,  16);
    set_dac_color(UI_MID_GRAY,   32);
    set_dac_color(UI_LIGHT_GRAY, 48);
    set_dac_color(UI_WHITE_GRAY, 63);
}

int check_file(const char *filename)
{
    FILE *file;

    file = fopen(filename, "rb");
    if (file == NULL)
    {
        return 0;
    }

    fclose(file);
    return 1;
}

int pcx_load_image(const char *filename, PCXImage *image)
{
    FILE *file;
    PCXHeader header;
    unsigned char *scanline;
    int width;
    int height;
    int x;
    int y;
    int data;
    int value;
    int count;
    int decoded;
    int palette_marker;

    image->width = 0;
    image->height = 0;
    image->pixels = NULL;

    file = fopen(filename, "rb");
    if (file == NULL)
    {
        return 0;
    }

    if (fread(&header, 1, sizeof(PCXHeader), file) != sizeof(PCXHeader))
    {
        fclose(file);
        return 0;
    }

    width = header.xmax - header.xmin + 1;
    height = header.ymax - header.ymin + 1;

    if (header.manufacturer != 10 ||
        header.encoding != 1 ||
        header.bits_per_pixel != 8 ||
        header.color_planes != 1 ||
        width <= 0 ||
        height <= 0 ||
        header.bytes_per_line < width)
    {
        fclose(file);
        return 0;
    }

    image->pixels = (unsigned char *)malloc((unsigned long)width * height);
    if (image->pixels == NULL)
    {
        fclose(file);
        return 0;
    }

    scanline = (unsigned char *)malloc(header.bytes_per_line);
    if (scanline == NULL)
    {
        free(image->pixels);
        image->pixels = NULL;
        fclose(file);
        return 0;
    }

    for (y = 0; y < height; y++)
    {
        decoded = 0;

        while (decoded < header.bytes_per_line)
        {
            data = fgetc(file);
            if (data == EOF)
            {
                free(scanline);
                free(image->pixels);
                image->pixels = NULL;
                fclose(file);
                return 0;
            }

            if ((data & 0xC0) == 0xC0)
            {
                count = data & 0x3F;
                value = fgetc(file);

                if (value == EOF)
                {
                    free(scanline);
                    free(image->pixels);
                    image->pixels = NULL;
                    fclose(file);
                    return 0;
                }

                while (count > 0 && decoded < header.bytes_per_line)
                {
                    scanline[decoded++] = (unsigned char)value;
                    count--;
                }
            }
            else
            {
                scanline[decoded++] = (unsigned char)data;
            }
        }

        for (x = 0; x < width; x++)
        {
            image->pixels[y * width + x] = scanline[x];
        }
    }

    if (fseek(file, -769L, SEEK_END) != 0)
    {
        free(scanline);
        free(image->pixels);
        image->pixels = NULL;
        fclose(file);
        return 0;
    }

    palette_marker = fgetc(file);
    if (palette_marker != 12 || fread(image->palette, 1, 768, file) != 768)
    {
        free(scanline);
        free(image->pixels);
        image->pixels = NULL;
        fclose(file);
        return 0;
    }

    free(scanline);
    fclose(file);

    image->width = width;
    image->height = height;
    return 1;
}

void pcx_free_image(PCXImage *image)
{
    if (image->pixels != NULL)
    {
        free(image->pixels);
    }

    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

void pcx_apply_palette(const PCXImage *image)
{
    int i;

    outportb(0x3C8, 0);

    for (i = 0; i < 768; i++)
    {
        outportb(0x3C9, image->palette[i] >> 2);
    }

    /* Reclaim engine UI colors after every artwork palette load. */
    apply_engine_ui_palette();
}

void pcx_remap_to_palette(PCXImage *image, const unsigned char *target_palette)
{
    unsigned char map[256];
    long best_distance;
    long distance;
    long dr;
    long dg;
    long db;
    unsigned long pixel_count;
    unsigned long p;
    int source;
    int target;
    int best_target;

    for (source = 0; source < 256; source++)
    {
        best_distance = 0x7FFFFFFFL;
        best_target = 0;

        for (target = 0; target < 256; target++)
        {
            dr = (long)image->palette[source * 3] - target_palette[target * 3];
            dg = (long)image->palette[source * 3 + 1] - target_palette[target * 3 + 1];
            db = (long)image->palette[source * 3 + 2] - target_palette[target * 3 + 2];
            distance = dr * dr + dg * dg + db * db;

            if (distance < best_distance)
            {
                best_distance = distance;
                best_target = target;
            }
        }

        map[source] = (unsigned char)best_target;
    }

    pixel_count = (unsigned long)image->width * image->height;

    for (p = 0; p < pixel_count; p++)
    {
        image->pixels[p] = map[image->pixels[p]];
    }

    for (p = 0; p < 768; p++)
    {
        image->palette[p] = target_palette[p];
    }
}

void pcx_blit_region(const PCXImage *image,
                     int source_x,
                     int source_y,
                     int width,
                     int height,
                     int dest_x,
                     int dest_y,
                     int transparent_index)
{
    unsigned char row[320];
    const unsigned char *source;
    int x;
    int y;

    if (width <= 0 || width > 320 || height <= 0)
    {
        return;
    }

    if (source_x < 0 || source_y < 0 ||
        source_x + width > image->width ||
        source_y + height > image->height ||
        dest_x < 0 || dest_y < 0 ||
        dest_x + width > SCREEN_WIDTH ||
        dest_y + height > SCREEN_HEIGHT)
    {
        return;
    }

    for (y = 0; y < height; y++)
    {
        source = image->pixels + (source_y + y) * image->width + source_x;

        if (transparent_index < 0)
        {
            dosmemput(source,
                      width,
                      VGA_MEMORY + (dest_y + y) * SCREEN_WIDTH + dest_x);
        }
        else
        {
            dosmemget(VGA_MEMORY + (dest_y + y) * SCREEN_WIDTH + dest_x,
                      width,
                      row);

            for (x = 0; x < width; x++)
            {
                if (source[x] != (unsigned char)transparent_index)
                {
                    row[x] = source[x];
                }
            }

            dosmemput(row,
                      width,
                      VGA_MEMORY + (dest_y + y) * SCREEN_WIDTH + dest_x);
        }
    }
}

int load_pcx(const char *filename)
{
    PCXImage image;

    if (!pcx_load_image(filename, &image))
    {
        return 0;
    }

    if (image.width != SCREEN_WIDTH || image.height != SCREEN_HEIGHT)
    {
        pcx_free_image(&image);
        return 0;
    }

    pcx_set_video_mode(0x13);
    pcx_apply_palette(&image);
    display_fullscreen(image.pixels);
    pcx_free_image(&image);

    return 1;
}
