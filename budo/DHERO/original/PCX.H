#ifndef PCX_H
#define PCX_H

typedef struct
{
    int width;
    int height;
    unsigned char *pixels;
    unsigned char palette[768];
} PCXImage;

int check_file(const char *filename);
int load_pcx(const char *filename);

int pcx_load_image(const char *filename, PCXImage *image);
void pcx_free_image(PCXImage *image);
void pcx_apply_palette(const PCXImage *image);
void pcx_remap_to_palette(PCXImage *image, const unsigned char *target_palette);
void pcx_blit_region(const PCXImage *image,
                     int source_x,
                     int source_y,
                     int width,
                     int height,
                     int dest_x,
                     int dest_y,
                     int transparent_index);

#endif
