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

// -------- Internal framebuffer state --------
static struct {
    int fbfd;
    uint8_t *fbp;               // mapped framebuffer
    size_t screensize;          // bytes
    int width, height;          // visible resolution
    int bpp;                    // bits per pixel
    int line_length;            // bytes per line in fb
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    // Backbuffer for drawing (malloc'd)
    uint8_t *back;
    size_t back_stride;         // bytes per line in backbuffer
} FB;

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
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

int fb_init(const char *devpath) {
    memset(&FB, 0, sizeof(FB));
    FB.fbfd = open(devpath ? devpath : "/dev/fb0", O_RDWR);
    if (FB.fbfd < 0) { perror("open fb"); return -1; }
    if (ioctl(FB.fbfd, FBIOGET_FSCREENINFO, &FB.finfo) == -1) { perror("FBIOGET_FSCREENINFO"); goto fail; }
    if (ioctl(FB.fbfd, FBIOGET_VSCREENINFO, &FB.vinfo) == -1) { perror("FBIOGET_VSCREENINFO"); goto fail; }

    FB.width  = (int)FB.vinfo.xres;
    FB.height = (int)FB.vinfo.yres;
    FB.bpp    = (int)FB.vinfo.bits_per_pixel;
    FB.line_length = FB.finfo.line_length;
    FB.screensize = (size_t)FB.line_length * FB.height;

    FB.fbp = (uint8_t*)mmap(NULL, FB.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, FB.fbfd, 0);
    if (FB.fbp == MAP_FAILED) { perror("mmap"); goto fail; }

    FB.back_stride = (size_t)FB.width * (size_t)(FB.bpp/8);
    // 64-byte alignment is nice for memcpy; use posix_memalign for portability
    if (posix_memalign((void**)&FB.back, 64, FB.back_stride * (size_t)FB.height) != 0) {
        perror("posix_memalign"); goto fail2; }
    memset(FB.back, 0, FB.back_stride * (size_t)FB.height);
    return 0;

fail2:
    if (FB.fbp && FB.fbp != MAP_FAILED) munmap(FB.fbp, FB.screensize);
fail:
    if (FB.fbfd >= 0) close(FB.fbfd);
    memset(&FB, 0, sizeof(FB));
    return -1;
}

void fb_close(void) {
    if (FB.back) { free(FB.back); FB.back = NULL; }
    if (FB.fbp && FB.fbp != MAP_FAILED) munmap(FB.fbp, FB.screensize);
    if (FB.fbfd >= 0) close(FB.fbfd);
    memset(&FB, 0, sizeof(FB));
}

int fb_width(void)  { return FB.width; }
int fb_height(void) { return FB.height; }
int fb_bpp(void)    { return FB.bpp; }

// -------- Drawing helpers (write to backbuffer) --------
static inline void put_pixel32(int x, int y, uint32_t px){
    if ((unsigned)x >= (unsigned)FB.width || (unsigned)y >= (unsigned)FB.height) return;
    uint32_t *row = (uint32_t *)(FB.back + (size_t)y * FB.back_stride);
    row[x] = px;
}

static inline void put_pixel_generic(int x, int y, uint32_t px){
    if ((unsigned)x >= (unsigned)FB.width || (unsigned)y >= (unsigned)FB.height) return;
    int bytes = FB.bpp/8;
    uint8_t *dst = FB.back + (size_t)y * FB.back_stride + (size_t)x * bytes;
    memcpy(dst, &px, bytes);
}

void put_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = pack_rgb(r,g,b);
    if (FB.bpp == 32) put_pixel32(x,y,px); else put_pixel_generic(x,y,px);
}

void fb_clear_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = pack_rgb(r,g,b);
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

void draw_hline(int x, int y, int w, uint8_t r, uint8_t g, uint8_t b){
    if (y < 0 || y >= FB.height || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB.width) { w = FB.width - x; }
    if (w <= 0) return;
    uint32_t px = pack_rgb(r,g,b);
    if (FB.bpp == 32) {
        uint32_t *row = (uint32_t *)(FB.back + (size_t)y * FB.back_stride);
        for (int i = 0; i < w; ++i) row[x + i] = px;
    } else {
        int bytes = FB.bpp/8; uint8_t *dst = FB.back + (size_t)y * FB.back_stride + (size_t)x * bytes;
        for (int i = 0; i < w; ++i) memcpy(dst + (size_t)i * bytes, &px, bytes);
    }
}

void draw_vline(int x, int y, int h, uint8_t r, uint8_t g, uint8_t b){
    if (x < 0 || x >= FB.width || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > FB.height) { h = FB.height - y; }
    if (h <= 0) return;
    uint32_t px = pack_rgb(r,g,b);
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

// Bresenham line
void draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b){
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    uint32_t px = pack_rgb(r,g,b);
    for (;;) {
        if (FB.bpp == 32) put_pixel32(x0,y0,px); else put_pixel_generic(x0,y0,px);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_rect(int x,int y,int w,int h, uint8_t r,uint8_t g,uint8_t b){
    if (w<=0||h<=0) return;
    draw_hline(x,y,w,r,g,b);
    draw_hline(x,y+h-1,w,r,g,b);
    draw_vline(x,y,h,r,g,b);
    draw_vline(x+w-1,y,h,r,g,b);
}

void fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b){
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB.width)  w = FB.width - x;
    if (y + h > FB.height) h = FB.height - y;
    if (w <= 0 || h <= 0) return;
    uint32_t px = pack_rgb(r,g,b);
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

// Midpoint circle (outline)
void draw_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b){
    if (radius <= 0) return;
    int x = radius, y = 0; int err = 1 - x;
    uint32_t px = pack_rgb(r,g,b);
    while (x >= y) {
        if (FB.bpp==32){
            put_pixel32(cx + x, cy + y, px); put_pixel32(cx + y, cy + x, px);
            put_pixel32(cx - y, cy + x, px); put_pixel32(cx - x, cy + y, px);
            put_pixel32(cx - x, cy - y, px); put_pixel32(cx - y, cy - x, px);
            put_pixel32(cx + y, cy - x, px); put_pixel32(cx + x, cy - y, px);
        } else {
            put_pixel_generic(cx + x, cy + y, px); put_pixel_generic(cx + y, cy + x, px);
            put_pixel_generic(cx - y, cy + x, px); put_pixel_generic(cx - x, cy + y, px);
            put_pixel_generic(cx - x, cy - y, px); put_pixel_generic(cx - y, cy - x, px);
            put_pixel_generic(cx + y, cy - x, px); put_pixel_generic(cx + x, cy - y, px);
        }
        y++;
        if (err < 0) { err += 2*y + 1; }
        else { x--; err += 2*(y - x + 1); }
    }
}

// Filled circle via horizontal spans
void fill_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b){
    if (radius <= 0) return;
    int x = radius, y = 0; int err = 1 - x;
    while (x >= y) {
        draw_hline(cx - x, cy + y, 2*x + 1, r,g,b);
        draw_hline(cx - x, cy - y, 2*x + 1, r,g,b);
        draw_hline(cx - y, cy + x, 2*y + 1, r,g,b);
        draw_hline(cx - y, cy - x, 2*y + 1, r,g,b);
        y++;
        if (err < 0) { err += 2*y + 1; }
        else { x--; err += 2*(y - x + 1); }
    }
}

// Present the backbuffer to the real framebuffer (single blit per frame)
void fb_present(void){
    if ((size_t)FB.line_length == FB.back_stride){
        memcpy(FB.fbp, FB.back, FB.back_stride * (size_t)FB.height);
    } else {
        for (int y = 0; y < FB.height; ++y) {
            memcpy(FB.fbp + (size_t)y * (size_t)FB.line_length,
                   FB.back + (size_t)y * FB.back_stride,
                   FB.back_stride);
        }
    }
}

/* =========================
Build (split files):
  gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 \
      -c libdraw.c -o libdraw.o
  gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 \
      drawdemo.c libdraw.o -lm -o drawdemo
========================= */

