#ifndef BUDO_GRAPHICS_H
#define BUDO_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *file_buf;
    size_t file_size;

    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_glyph;

    const uint8_t *glyphs;
} psf_font_t;

typedef struct {
    uint32_t *pixels;
    int width;
    int height;
    uint32_t colorkey;
    int has_colorkey;
} budo_sprite_t;

typedef enum {
    BUDO_SPRITE_FLIP_NONE = 0,
    BUDO_SPRITE_FLIP_X = 1 << 0,
    BUDO_SPRITE_FLIP_Y = 1 << 1
} budo_sprite_flip_t;

/* LOAD A PSF FONT FROM DISK INTO MEMORY.
*  CALLER MUST PROVIDE A psf_font_t AND PATH TO A PSF1/PSF2 FILE.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*  ON SUCCESS, CALL psf_font_destroy WHEN DONE TO FREE MEMORY.
*/
int psf_font_load(psf_font_t *font, const char *path);
/* FREE MEMORY HELD BY A PSF FONT.
*  SAFE TO CALL WITH NULL POINTERS OR ON ALREADY-CLEARED FONTS.
*  RESETS THE STRUCT TO ZEROED STATE.
*/
void psf_font_destroy(psf_font_t *font);

/* DRAW A SINGLE GLYPH FROM A LOADED PSF FONT.
*  pixels IS A CPU FRAMEBUFFER OF SIZE fb_w * fb_h IN 32-BIT RGBA.
*  (x, y) IS THE TOP-LEFT DESTINATION POSITION IN PIXELS.
*  glyph_index SELECTS THE GLYPH; OUT-OF-RANGE FALLS BACK TO '?'.
*/
void psf_draw_glyph(const psf_font_t *font,
                    uint32_t *pixels, int fb_w, int fb_h,
                    int x, int y, uint8_t glyph_index, uint32_t color);
/* DRAW A NUL-TERMINATED STRING USING A PSF FONT.
*  NEWLINES ADVANCE TO THE NEXT LINE; TABS ARE 4 GLYPHS WIDE.
*  color IS A PACKED 32-BIT RGBA COLOR (0xAARRGGBB).
*/
void psf_draw_text(const psf_font_t *font,
                   uint32_t *pixels, int fb_w, int fb_h,
                   int x, int y, const char *text, uint32_t color);

/* LOAD A SPRITE IMAGE INTO A CPU-FRIENDLY PIXEL BUFFER.
*  USES SDL_image IF AVAILABLE (PNG/JPG/GIF/etc.), OR SDL_LoadBMP OTHERWISE.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE. CALL budo_sprite_destroy WHEN DONE.
*  LOADED PIXELS ARE ARGB8888 (0xAARRGGBB) AND READY FOR DRAWING.
*/
int budo_sprite_load(budo_sprite_t *sprite, const char *path);
/* FREE MEMORY OWNED BY A SPRITE AND RESET ITS STATE.
*  SAFE TO CALL WITH NULL POINTERS OR ALREADY-FREED SPRITES.
*/
void budo_sprite_destroy(budo_sprite_t *sprite);
/* ENABLE COLOR-KEY TRANSPARENCY FOR A SPRITE.
*  ANY PIXEL EQUAL TO colorkey (0xAARRGGBB) WILL BE SKIPPED ON DRAW.
*  USEFUL FOR SPRITES WITHOUT ALPHA CHANNELS.
*/
void budo_sprite_set_colorkey(budo_sprite_t *sprite, uint32_t colorkey);

/* DRAW AN ENTIRE SPRITE TO THE FRAMEBUFFER AT (x, y).
*  ALPHA IS BLENDED OVER THE DESTINATION; COLOR-KEY IS RESPECTED IF SET.
*/
void budo_draw_sprite(const budo_sprite_t *sprite,
                      uint32_t *pixels, int fb_w, int fb_h,
                      int x, int y);
/* DRAW A RECTANGULAR REGION OF A SPRITE.
*  src_* DEFINES THE REGION INSIDE THE SPRITE SHEET.
*  flip_flags USES BUDO_SPRITE_FLIP_X/Y FOR MIRRORING.
*  THIS IS IDEAL FOR TILEMAPS AND CUTOUTS.
*/
void budo_draw_sprite_region(const budo_sprite_t *sprite,
                             uint32_t *pixels, int fb_w, int fb_h,
                             int x, int y,
                             int src_x, int src_y, int src_w, int src_h,
                             int flip_flags);
/* DRAW A FRAME FROM A GRID-ALIGNED SPRITE SHEET.
*  frame_w AND frame_h DEFINE THE CELL SIZE.
*  frame_index COUNTS LEFT-TO-RIGHT, TOP-TO-BOTTOM (0-BASED).
*  USE flip_flags FOR MIRRORING WALK CYCLES.
*/
void budo_draw_sprite_frame(const budo_sprite_t *sprite,
                            uint32_t *pixels, int fb_w, int fb_h,
                            int x, int y,
                            int frame_w, int frame_h, int frame_index,
                            int flip_flags);

/* FILL AN ENTIRE FRAMEBUFFER WITH A SOLID COLOR.
*  color IS A PACKED 32-BIT RGBA VALUE (0xAARRGGBB).
*/
void budo_clear_buffer(uint32_t *pixels, int width, int height, uint32_t color);
/* SET A SINGLE PIXEL IF IT LIES INSIDE THE FRAMEBUFFER BOUNDS. */
void budo_put_pixel(uint32_t *pixels, int width, int height, int x, int y, uint32_t color);
/* DRAW A LINE USING BRESENHAM'S ALGORITHM BETWEEN TWO POINTS. */
void budo_draw_line(uint32_t *pixels, int width, int height,
                    int x0, int y0, int x1, int y1, uint32_t color);

#endif
