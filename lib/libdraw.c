// =========================
// libdraw.c — minimal 2D primitives on Linux framebuffer (/dev/fb0)
// Optimized: 32bpp fast paths, backbuffer + single blit per frame
// Build: see bottom of this file or drawdemo.c notes
// =========================

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple color type and helper macro
typedef struct { uint8_t r, g, b; } Color;
#define COLOR(r,g,b) ((Color){(uint8_t)(r),(uint8_t)(g),(uint8_t)(b)})

// ------- Public API (prototypes) -------
// These functions form the simple drawing interface exposed to games.
// Example usage:
//   if (draw_open(320, 200) != 0) return 1;
//   draw_clear(COLOR(0,0,0));
//   draw_rect_fill(10,10,50,30, COLOR(255,0,0));
//   draw_present();

int draw_open(int width, int height);   // initialize framebuffer
void draw_close(void);                  // release resources
int draw_w(void);                       // current virtual width
int draw_h(void);                       // current virtual height

void draw_clear(Color c);                    // fill entire screen with color
void draw_pixel(int x, int y, Color c);      // draw a single pixel
void draw_hline(int x, int y, int w, Color c); // horizontal line
void draw_vline(int x, int y, int h, Color c); // vertical line
void draw_line(int x0, int y0, int x1, int y1, Color c); // generic line
void draw_rect(int x, int y, int w, int h, Color c); // rectangle outline
void draw_rect_fill(int x, int y, int w, int h, Color c); // filled rectangle
void draw_circle(int cx, int cy, int r, Color c); // circle outline
void draw_circle_fill(int cx, int cy, int r, Color c); // filled circle
void draw_text(int x, int y, const char *text, Color c); // draw 8x8 text string
void draw_present(void);                     // copy backbuffer to real screen
uint8_t *draw_pixels(void);                  // direct pointer to backbuffer
size_t draw_stride(void);                    // bytes per row of backbuffer

// -------- Internal framebuffer state --------
static struct {
    int fbfd;
    uint8_t *fbp;               // mapped framebuffer
    size_t screensize;          // bytes
    int width, height;          // virtual drawing resolution
    int phys_width, phys_height; // actual framebuffer resolution
    int bpp;                    // bits per pixel
    int line_length;            // bytes per line in fb
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    // Backbuffer for drawing (malloc'd)
    uint8_t *back;
    size_t back_stride;         // bytes per line in backbuffer

    // Precomputed scaling maps for draw_fb_present
    int *xmap;
    int *ymap;
} FB;

// 8x8 bitmap font for ASCII characters, public domain
static const uint8_t draw_font8x8[128][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
};
// draw_pack_rgb — convert 8-bit r/g/b into current framebuffer pixel format
static inline uint32_t draw_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    const struct fb_var_screeninfo *vi = &FB.vinfo;
    uint32_t R = r, G = g, B = b;
    if (vi->red.length   < 8) R >>= (8 - vi->red.length);
    if (vi->green.length < 8) G >>= (8 - vi->green.length);
    if (vi->blue.length  < 8) B >>= (8 - vi->blue.length);
    uint32_t px = 0;
    px |= (R & ((1u << vi->red.length)   - 1))   << vi->red.offset;
    px |= (G & ((1u << vi->green.length) - 1))   << vi->green.offset;
    px |= (B & ((1u << vi->blue.length)  - 1))   << vi->blue.offset;
    return px;
}

// draw_fb_init_res — open framebuffer and setup virtual resolution
int draw_fb_init_res(const char *devpath, int width, int height) {
    memset(&FB, 0, sizeof(FB));
    FB.fbfd = open(devpath ? devpath : "/dev/fb0", O_RDWR);
    if (FB.fbfd < 0) { perror("open fb"); return -1; }
    if (ioctl(FB.fbfd, FBIOGET_FSCREENINFO, &FB.finfo) == -1) { perror("FBIOGET_FSCREENINFO"); goto fail; }
    if (ioctl(FB.fbfd, FBIOGET_VSCREENINFO, &FB.vinfo) == -1) { perror("FBIOGET_VSCREENINFO"); goto fail; }

    FB.phys_width  = (int)FB.vinfo.xres;
    FB.phys_height = (int)FB.vinfo.yres;
    FB.bpp    = (int)FB.vinfo.bits_per_pixel;
    FB.line_length = FB.finfo.line_length;
    FB.screensize = (size_t)FB.line_length * FB.phys_height;

    FB.width  = (width  > 0) ? width  : FB.phys_width;
    FB.height = (height > 0) ? height : FB.phys_height;

    FB.fbp = (uint8_t*)mmap(NULL, FB.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, FB.fbfd, 0);
    if (FB.fbp == MAP_FAILED) { perror("mmap"); goto fail; }

    FB.back_stride = (size_t)FB.width * (size_t)(FB.bpp/8);
    if (posix_memalign((void**)&FB.back, 64, FB.back_stride * (size_t)FB.height) != 0) {
        perror("posix_memalign"); goto fail2; }
    memset(FB.back, 0, FB.back_stride * (size_t)FB.height);

    if (FB.width != FB.phys_width) {
        FB.xmap = malloc(sizeof(int) * (size_t)FB.phys_width);
        if (!FB.xmap) { perror("malloc xmap"); goto fail3; }
        for (int x = 0; x < FB.phys_width; ++x)
            FB.xmap[x] = (int)((long long)x * FB.width / FB.phys_width);
    }
    if (FB.height != FB.phys_height) {
        FB.ymap = malloc(sizeof(int) * (size_t)FB.phys_height);
        if (!FB.ymap) { perror("malloc ymap"); goto fail3; }
        for (int y = 0; y < FB.phys_height; ++y)
            FB.ymap[y] = (int)((long long)y * FB.height / FB.phys_height);
    }
    return 0;

fail3:
    if (FB.xmap) { free(FB.xmap); FB.xmap = NULL; }
    if (FB.ymap) { free(FB.ymap); FB.ymap = NULL; }
    if (FB.back) { free(FB.back); FB.back = NULL; }
fail2:
    if (FB.fbp && FB.fbp != MAP_FAILED) munmap(FB.fbp, FB.screensize);
fail:
    if (FB.fbfd >= 0) close(FB.fbfd);
    memset(&FB, 0, sizeof(FB));
    return -1;
}

// draw_fb_init — convenience wrapper using physical resolution
int draw_fb_init(const char *devpath){
    return draw_fb_init_res(devpath, 0, 0);
}

// draw_fb_close — release internal state
void draw_fb_close(void) {
    if (FB.back) { free(FB.back); FB.back = NULL; }
    if (FB.xmap) { free(FB.xmap); FB.xmap = NULL; }
    if (FB.ymap) { free(FB.ymap); FB.ymap = NULL; }
    if (FB.fbp && FB.fbp != MAP_FAILED) munmap(FB.fbp, FB.screensize);
    if (FB.fbfd >= 0) close(FB.fbfd);
    memset(&FB, 0, sizeof(FB));
}

// draw_fb_width/draw_fb_height/draw_fb_bpp — query framebuffer properties
int draw_fb_width(void)  { return FB.width; }
int draw_fb_height(void) { return FB.height; }
int draw_fb_bpp(void)    { return FB.bpp; }
uint8_t *draw_fb_pixels(void) { return FB.back; }
size_t draw_fb_stride(void) { return FB.back_stride; }

// -------- Drawing helpers (write to backbuffer) --------
// draw_put_pixel32 — fast path for 32bpp framebuffers
static inline void draw_put_pixel32(int x, int y, uint32_t px){
    if ((unsigned)x >= (unsigned)FB.width || (unsigned)y >= (unsigned)FB.height) return;
    uint32_t *row = (uint32_t *)(FB.back + (size_t)y * FB.back_stride);
    row[x] = px;
}

// draw_put_pixel_generic — handle other draw_pixel sizes
static inline void draw_put_pixel_generic(int x, int y, uint32_t px){
    if ((unsigned)x >= (unsigned)FB.width || (unsigned)y >= (unsigned)FB.height) return;
    int bytes = FB.bpp/8;
    uint8_t *dst = FB.back + (size_t)y * FB.back_stride + (size_t)x * bytes;
    memcpy(dst, &px, bytes);
}

// draw_put_pixel — choose best routine and write one draw_pixel
void draw_put_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = draw_pack_rgb(r,g,b);
    if (FB.bpp == 32) draw_put_pixel32(x,y,px); else draw_put_pixel_generic(x,y,px);
}

// draw_fb_clear_rgb — fill entire backbuffer with given color
void draw_fb_clear_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = draw_pack_rgb(r,g,b);
    if (FB.bpp == 32) {
        size_t total = (size_t)FB.width * (size_t)FB.height;
        uint64_t pat = ((uint64_t)px << 32) | px;
        uint64_t *dst64 = (uint64_t *)FB.back;
        while (total >= 2) { *dst64++ = pat; total -= 2; }
        if (total) *((uint32_t *)dst64) = px;
    } else {
        int bytes = FB.bpp/8;
        for (int y = 0; y < FB.height; ++y) {
            uint8_t *row = FB.back + (size_t)y * FB.back_stride;
            for (int x = 0; x < FB.width; ++x) memcpy(row + (size_t)x*bytes, &px, bytes);
        }
    }
}

// draw_hline_rgb — horizontal draw_line
void draw_hline_rgb(int x, int y, int w, uint8_t r, uint8_t g, uint8_t b){
    if (y < 0 || y >= FB.height || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB.width) { w = FB.width - x; }
    if (w <= 0) return;
    uint32_t px = draw_pack_rgb(r,g,b);
    if (FB.bpp == 32) {
        uint32_t *row = (uint32_t *)(FB.back + (size_t)y * FB.back_stride);
        for (int i = 0; i < w; ++i) row[x + i] = px;
    } else {
        int bytes = FB.bpp/8; uint8_t *dst = FB.back + (size_t)y * FB.back_stride + (size_t)x * bytes;
        for (int i = 0; i < w; ++i) memcpy(dst + (size_t)i * bytes, &px, bytes);
    }
}

// draw_vline_rgb — vertical draw_line
void draw_vline_rgb(int x, int y, int h, uint8_t r, uint8_t g, uint8_t b){
    if (x < 0 || x >= FB.width || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > FB.height) { h = FB.height - y; }
    if (h <= 0) return;
    uint32_t px = draw_pack_rgb(r,g,b);
    if (FB.bpp == 32) {
        for (int i = 0; i < h; ++i) {
            uint32_t *row = (uint32_t *)(FB.back + (size_t)(y + i) * FB.back_stride);
            row[x] = px;
        }
    } else {
        int bytes = FB.bpp/8;
        for (int i = 0; i < h; ++i) {
            uint8_t *dst = FB.back + (size_t)(y + i) * FB.back_stride + (size_t)x * bytes;
            memcpy(dst, &px, bytes);
        }
    }
}

// Bresenham draw_line
void draw_line_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b){
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    uint32_t px = draw_pack_rgb(r,g,b);
    for (;;) {
        if (FB.bpp == 32) draw_put_pixel32(x0,y0,px); else draw_put_pixel_generic(x0,y0,px);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// draw_rect_rgb — rectangle outline
void draw_rect_rgb(int x,int y,int w,int h, uint8_t r,uint8_t g,uint8_t b){
    if (w<=0||h<=0) return;
    draw_hline_rgb(x,y,w,r,g,b);
    draw_hline_rgb(x,y+h-1,w,r,g,b);
    draw_vline_rgb(x,y,h,r,g,b);
    draw_vline_rgb(x+w-1,y,h,r,g,b);
}

// draw_rect_fill_rgb — filled rectangle
void draw_rect_fill_rgb(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b){
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB.width)  w = FB.width - x;
    if (y + h > FB.height) h = FB.height - y;
    if (w <= 0 || h <= 0) return;
    uint32_t px = draw_pack_rgb(r,g,b);
    if (FB.bpp == 32) {
        uint8_t *row0 = FB.back + (size_t)y * FB.back_stride + (size_t)x * 4;
        uint64_t pat = ((uint64_t)px << 32) | px;
        uint64_t *p64 = (uint64_t *)row0;
        int w64 = w / 2;
        for (int i = 0; i < w64; ++i) p64[i] = pat;
        if (w & 1) ((uint32_t *)row0)[w - 1] = px;
        size_t row_bytes = (size_t)w * 4;
        for (int yy = 1; yy < h; ++yy) {
            memcpy(FB.back + (size_t)(y + yy) * FB.back_stride + (size_t)x * 4,
                   row0, row_bytes);
        }
    } else {
        int bytes = FB.bpp/8;
        for (int yy = 0; yy < h; ++yy) {
            uint8_t *dst = FB.back + (size_t)(y + yy) * FB.back_stride + (size_t)x * bytes;
            for (int xx = 0; xx < w; ++xx) memcpy(dst + (size_t)xx * bytes, &px, bytes);
        }
    }
}

// draw_circle_rgb — draw_circle outline (midpoint)
void draw_circle_rgb(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b){
    if (radius <= 0) return;
    int x = radius, y = 0; int err = 1 - x;
    uint32_t px = draw_pack_rgb(r,g,b);
    while (x >= y) {
        if (FB.bpp==32){
            draw_put_pixel32(cx + x, cy + y, px); draw_put_pixel32(cx + y, cy + x, px);
            draw_put_pixel32(cx - y, cy + x, px); draw_put_pixel32(cx - x, cy + y, px);
            draw_put_pixel32(cx - x, cy - y, px); draw_put_pixel32(cx - y, cy - x, px);
            draw_put_pixel32(cx + y, cy - x, px); draw_put_pixel32(cx + x, cy - y, px);
        } else {
            draw_put_pixel_generic(cx + x, cy + y, px); draw_put_pixel_generic(cx + y, cy + x, px);
            draw_put_pixel_generic(cx - y, cy + x, px); draw_put_pixel_generic(cx - x, cy + y, px);
            draw_put_pixel_generic(cx - x, cy - y, px); draw_put_pixel_generic(cx - y, cy - x, px);
            draw_put_pixel_generic(cx + y, cy - x, px); draw_put_pixel_generic(cx + x, cy - y, px);
        }
        y++;
        if (err < 0) { err += 2*y + 1; }
        else { x--; err += 2*(y - x + 1); }
    }
}

// draw_circle_fill_rgb — filled draw_circle via horizontal spans
void draw_circle_fill_rgb(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b){
    if (radius <= 0) return;
    int x = radius, y = 0; int err = 1 - x;
    while (x >= y) {
        draw_hline_rgb(cx - x, cy + y, 2*x + 1, r,g,b);
        draw_hline_rgb(cx - x, cy - y, 2*x + 1, r,g,b);
        draw_hline_rgb(cx - y, cy + x, 2*y + 1, r,g,b);
        draw_hline_rgb(cx - y, cy - x, 2*y + 1, r,g,b);
        y++;
        if (err < 0) { err += 2*y + 1; }
        else { x--; err += 2*(y - x + 1); }
    }
}

// draw_char_rgb — render a single 8x8 glyph
static void draw_char_rgb(int x, int y, unsigned char ch, uint8_t r, uint8_t g, uint8_t b){
    const uint8_t *glyph = draw_font8x8[ch];
    for (int row = 0; row < 8; ++row){
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col){
            if (bits & (1u << col))
                draw_put_pixel(x + col, y + row, r, g, b);
        }
    }
}

// draw_text_rgb — render null-terminated ASCII string
static void draw_text_rgb(int x, int y, const char *s, uint8_t r, uint8_t g, uint8_t b){
    int start_x = x;
    for (; *s; ++s){
        unsigned char ch = (unsigned char)*s;
        if (ch == '\n'){ y += 8; x = start_x; continue; }
        if (ch < 128) draw_char_rgb(x, y, ch, r, g, b);
        x += 8;
    }
}

// draw_fb_present — blit backbuffer to real framebuffer
void draw_fb_present(void){
    if (FB.width == FB.phys_width && FB.height == FB.phys_height) {
        if ((size_t)FB.line_length == FB.back_stride){
            memcpy(FB.fbp, FB.back, FB.back_stride * (size_t)FB.height);
        } else {
            for (int y = 0; y < FB.height; ++y) {
                memcpy(FB.fbp + (size_t)y * (size_t)FB.line_length,
                       FB.back + (size_t)y * FB.back_stride,
                       FB.back_stride);
            }
        }
    } else {
        int bytes = FB.bpp/8;
        for (int y = 0; y < FB.phys_height; ++y) {
            int sy = FB.ymap ? FB.ymap[y] : y;
            const uint8_t *src_row = FB.back + (size_t)sy * FB.back_stride;
            uint8_t *dst_row = FB.fbp + (size_t)y * FB.line_length;
            for (int x = 0; x < FB.phys_width; ++x) {
                int sx = FB.xmap ? FB.xmap[x] : x;
                memcpy(dst_row + (size_t)x * bytes,
                       src_row + (size_t)sx * bytes,
                       bytes);
            }
        }
    }
}

// -------- Beginner-friendly wrapper layer --------

// draw_open — initialize the framebuffer; width/height 0 use physical size
int draw_open(int width, int height){
    return draw_fb_init_res(NULL, width, height);
}

// draw_close — release framebuffer resources
void draw_close(void){
    draw_fb_close();
}

// draw_w/draw_h — query current virtual dimensions
int draw_w(void){ return draw_fb_width(); }
int draw_h(void){ return draw_fb_height(); }

// draw_clear — fill the entire screen with color c
void draw_clear(Color c){ draw_fb_clear_rgb(c.r, c.g, c.b); }

// draw_pixel — draw one draw_pixel at (x,y)
void draw_pixel(int x, int y, Color c){ draw_put_pixel(x, y, c.r, c.g, c.b); }

// draw_hline/draw_vline — draw horizontal or vertical draw_line
void draw_hline(int x, int y, int w, Color c){ draw_hline_rgb(x, y, w, c.r, c.g, c.b); }

void draw_vline(int x, int y, int h, Color c){ draw_vline_rgb(x, y, h, c.r, c.g, c.b); }

// draw_line — generic draw_line from (x0,y0) to (x1,y1)
void draw_line(int x0, int y0, int x1, int y1, Color c){ draw_line_rgb(x0, y0, x1, y1, c.r, c.g, c.b); }

// draw_rect/draw_rect_fill — rectangle outline or filled
void draw_rect(int x, int y, int w, int h, Color c){ draw_rect_rgb(x, y, w, h, c.r, c.g, c.b); }

void draw_rect_fill(int x, int y, int w, int h, Color c){ draw_rect_fill_rgb(x, y, w, h, c.r, c.g, c.b); }

// draw_circle/draw_circle_fill — draw_circle outline or filled
void draw_circle(int cx, int cy, int r, Color c){ draw_circle_rgb(cx, cy, r, c.r, c.g, c.b); }

void draw_circle_fill(int cx, int cy, int r, Color c){ draw_circle_fill_rgb(cx, cy, r, c.r, c.g, c.b); }

// draw_text — draw string using builtin 8x8 font
void draw_text(int x, int y, const char *text, Color c){ draw_text_rgb(x, y, text, c.r, c.g, c.b); }

// draw_present — copy backbuffer to the real framebuffer
void draw_present(void){ draw_fb_present(); }

// draw_pixels/draw_stride — direct access to backbuffer memory
uint8_t *draw_pixels(void){ return draw_fb_pixels(); }

size_t draw_stride(void){ return draw_fb_stride(); }

/* =========================
Build:
  gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 \
      drawdemo.c -lm -o drawdemo   (drawdemo.c includes libdraw.c)
========================= */

