#ifndef BUDO_GRAPHICS_H
#define BUDO_GRAPHICS_H

/*
 * budo_graphics provides access to the terminal's pixel rendering pipeline.
 * Functions below follow the interface described in ./budo/readme.md.
 */

void budo_graphics_init(int width, int height, int r, int g, int b);
void budo_graphics_pixel(int x, int y, int r, int g, int b, int layer);
void budo_graphics_clean(int x, int y, int layer);
void budo_graphics_text(int x, int y, const char *str, int layer);
void budo_graphics_render(int layer);

#endif
