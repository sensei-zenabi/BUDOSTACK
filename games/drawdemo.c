// =========================
// drawdemo.c — demo using the optimized libdraw
// =========================

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <signal.h>

// Forward declarations from libdraw.c (no separate headers used)
int  fb_init(const char *devpath);
void fb_close(void);
int  fb_width(void);
int  fb_height(void);
int  fb_bpp(void);
void fb_clear_rgb(uint8_t r, uint8_t g, uint8_t b);
void put_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void draw_hline(int x, int y, int w, uint8_t r, uint8_t g, uint8_t b);
void draw_vline(int x, int y, int h, uint8_t r, uint8_t g, uint8_t b);
void draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
void fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
void draw_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b);
void fill_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b);
void fb_present(void);

static volatile sig_atomic_t running = 1;
static void on_sigint(int sig){ (void)sig; running = 0; }

static inline uint64_t now_ns(){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void sleep_until_ns(uint64_t target){
    uint64_t t = now_ns();
    if (target <= t) return;
    struct timespec ts;
    uint64_t d = target - t;
    ts.tv_sec = (time_t)(d / 1000000000ull);
    ts.tv_nsec = (long)(d % 1000000000ull);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

int main(void){
    if (fb_init("/dev/fb0") != 0) return 1;
    signal(SIGINT, on_sigint);

    const int W = fb_width();
    const int H = fb_height();

    // Pre-fill background gradient in backbuffer once
    for (int y = 0; y < H; ++y) {
        uint8_t r = (uint8_t)((y * 255) / (H ? H : 1));
        for (int x = 0; x < W; ++x) {
            uint8_t b = (uint8_t)((x * 255) / (W ? W : 1));
            put_pixel(x, y, r, 0, b);
        }
    }
    fb_present();

    // Animated objects
    int bx = W/4, by = H/3, bw = W/8, bh = H/10;     // bouncing rectangle
    int bdx = 4, bdy = 3;

    int cx = (3*W)/4, cy = (2*H)/3, cr = H/12;       // bouncing circle
    int cdx = -3, cdy = -4;

    uint64_t next = now_ns();
    const uint64_t frame_ns = 16666667ull; // ~60Hz
    int t = 0;

    while (running) {
        // Redraw minimal background in areas we will overwrite (cheap clear)
        // For simplicity here, we clear a small band around objects; adjust as needed.
        fill_rect(bx-2, by-2, bw+4, bh+4, 0,0,0);   // overwrite with black band
        fill_circle(cx, cy, cr+2, 0,0,0);           // black ring to erase edge

        // Move objects
        bx += bdx; by += bdy;
        if (bx < 0) { bx = 0; bdx = -bdx; }
        if (by < 0) { by = 0; bdy = -bdy; }
        if (bx + bw >= W) { bx = W - bw - 1; bdx = -bdx; }
        if (by + bh >= H) { by = H - bh - 1; bdy = -bdy; }

        cx += cdx; cy += cdy;
        if (cx - cr < 0) { cx = cr; cdx = -cdx; }
        if (cy - cr < 0) { cy = cr; cdy = -cdy; }
        if (cx + cr >= W) { cx = W - cr - 1; cdx = -cdx; }
        if (cy + cr >= H) { cy = H - cr - 1; cdy = -cdy; }

        // Colors pulsate
        double tt = (double)t;
        uint8_t R = (uint8_t)(128 + 127 * sin(tt * 0.05));
        uint8_t G = (uint8_t)(128 + 127 * sin((tt+40.0) * 0.04));
        uint8_t B = (uint8_t)(128 + 127 * sin((tt+80.0) * 0.03));

        // Draw shapes
        fill_rect(bx, by, bw, bh, 255, 180, 40);
        draw_rect(bx, by, bw, bh, 0, 0, 0);

        fill_circle(cx, cy, cr, R, G, B);
        draw_circle(cx, cy, cr, 0, 0, 0);

        // Spinning crosshair
        int len = H/6;
        double ang = tt * 0.05;
        int x0 = W/2 + (int)(len * cos(ang));
        int y0 = H/2 + (int)(len * sin(ang));
        int x1 = W/2 - (int)(len * cos(ang));
        int y1 = H/2 - (int)(len * sin(ang));
        draw_line(x0,y0,x1,y1,255,255,255);
        draw_line(x0,y1,x1,y0, 80,200,255);

        fb_present();
        t++;
        next += frame_ns;
        sleep_until_ns(next);
    }

    fb_close();
    return 0;
}

/* =========================
Build & Run (from a VT like Ctrl+Alt+F3):

  gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 -c libdraw.c -o libdraw.o
  gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 drawdemo.c libdraw.o -lm -o drawdemo

  sudo ./drawdemo   # if /dev/fb0 permissions require
========================= */

