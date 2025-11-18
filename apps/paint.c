/*
 * paint.c — keyboard-only terminal pixel editor (ASCII), single-file C
 * Features:
 *  - New / Load / Save (PNG with transparency, BMP 24-bit uncompressed; optional PPM P6)
 *  - Undo (Ctrl+Z), Redo (Ctrl+Y)
 *  - Arrow-keys move cursor, auto-scrolling viewport
 *  - A–Z paints with 26-color palette; Backspace/Delete erases
 *  - Ctrl+F then color floods a region using 4-direction adjacency
 *  - Ctrl+1..Ctrl+5 cycle palette brightness (3 = default)
 *  - Max resolution 320x200
 *  - Works in a terminal using raw mode (termios) + ANSI escapes
 *
 * Build:   gcc paint.c -o paint -Wall
 * Run:     ./paint
 *
 * Notes:
 *  - BMP reader/writer supports 24-bit uncompressed, bottom-up, BI_RGB.
 *  - ASCII display prints the letter for the color; '.' means empty.
 *  - If your terminal supports 256 colors, colors are hinted via ANSI.
 */

#include <strings.h>  // for strcasecmp

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <poll.h>
#include <math.h>
#include <limits.h>

#include "../lib/terminal_layout.h"
#define STBI_ONLY_PNG
#include "../lib/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "../lib/stb_image_write.h"

#define MAX_W 320
#define MAX_H 200
#define EMPTY 255
#define USE_ANSI_COLOR 1   /* set to 0 for strictly ASCII (no colors) */

#define PALETTE_VARIANTS 5
#define PALETTE_COLORS 26
#define TOTAL_COLORS (PALETTE_VARIANTS * PALETTE_COLORS)

static struct termios orig_termios;

static void die(const char *msg) {
    // Restore terminal before exiting
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    // Show cursor
    write(STDOUT_FILENO, "\x1b[?25h", 6);
    // Clear attributes
    write(STDOUT_FILENO, "\x1b[0m", 4);
    perror(msg);
    exit(1);
}

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    // Hide cursor
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

static void clear_screen(void) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        if (rows) {
            *rows = BUDOSTACK_TARGET_ROWS;
        }
        if (cols) {
            *cols = BUDOSTACK_TARGET_COLS;
        }
        return;
    }
    if (rows) {
        *rows = ws.ws_row;
    }
    if (cols) {
        *cols = ws.ws_col;
    }
    budostack_clamp_terminal_size(rows, cols);
}

/* -------- Image data -------- */

static int img_w = BUDOSTACK_TARGET_COLS, img_h = BUDOSTACK_TARGET_ROWS;
static uint8_t pixels[MAX_W * MAX_H]; // Each = 0..TOTAL_COLORS-1 color index, or EMPTY

static int cursor_x = 0, cursor_y = 0;
static int view_x = 0, view_y = 0;
static int dirty = 0; // unsaved changes
static int fill_color_pending = 0;
static char current_file_path[512];

static void set_current_file_path(const char *path) {
    if (!path || !*path) {
        current_file_path[0] = '\0';
        return;
    }
    int written = snprintf(current_file_path, sizeof(current_file_path), "%s", path);
    if (written < 0 || written >= (int)sizeof(current_file_path)) {
        current_file_path[sizeof(current_file_path) - 1] = '\0';
    }
}

// Forward declarations for functions defined later in the file
static int paint_at_cursor(uint8_t color_idx);
static void flood_fill_at_cursor(uint8_t color_idx);
static void save_dialog(void);
static void load_dialog(void);
static void new_image_dialog(void);
static void resize_canvas_dialog(void);
static int undo_action(void);
static int redo_action(void);
static void prompt(const char *msg, char *out, size_t cap);
static int refresh_cursor_cell_partial(void);
static int update_cursor_partial(int old_x, int old_y);

/* Palette: 26 entries mapped to letters A..Z (RGB 0..255) */
typedef struct { uint8_t r,g,b; char letter; const char *name; int term256; } Color;
static const Color base_palette[PALETTE_COLORS] = {
    {  0,  0,  0,'A',"Black",      16},
    {255,255,255,'B',"White",      231},
    {128,128,128,'C',"Gray",       244},
    {255,  0,  0,'D',"Red",        196},
    {  0,255,  0,'E',"Lime",       46},
    {  0,  0,255,'F',"Blue",       21},
    {  0,255,255,'G',"Cyan",       51},
    {255,  0,255,'H',"Magenta",    201},
    {255,255,  0,'I',"Yellow",     226},
    {255,165,  0,'J',"Orange",     214},
    {165, 42, 42,'K',"Brown",      94},
    {128,  0,128,'L',"Purple",     129},
    {255,192,203,'M',"Pink",       218},
    {135,206,235,'N',"Sky",        117},
    {144,238,144,'O',"LightGreen", 120},
    {139,  0,  0,'P',"DarkRed",    88},
    {  0,100,  0,'Q',"DarkGreen",  22},
    {  0,  0,139,'R',"DarkBlue",   19},
    {  0,128,128,'S',"Teal",       30},
    {128,128,  0,'T',"Olive",      58},
    {  0,  0, 75,'U',"Navy-ish",   17},
    {210,105, 30,'V',"Chocolate",  166},
    {173,216,230,'W',"LightBlue",  153},
    { 75,  0,130,'X',"Indigo",     55},
    { 47, 79, 79,'Y',"DarkCyan",   23},
    {112,128,144,'Z',"SlateGray",  102}
};

static Color palettes[PALETTE_VARIANTS][PALETTE_COLORS];
static int current_palette_variant = 2; /* 0-based; palette 3 is default */

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static int component_to_level(uint8_t v) {
    static const uint8_t steps[6] = {0, 95, 135, 175, 215, 255};
    int best_level = 0;
    int best_diff = 256;
    for (int i = 0; i < 6; i++) {
        int diff = (int)v - (int)steps[i];
        if (diff < 0) diff = -diff;
        if (diff < best_diff || (diff == best_diff && i < best_level)) {
            best_level = i;
            best_diff = diff;
        }
    }
    return best_level;
}

static int rgb_to_ansi256(uint8_t r, uint8_t g, uint8_t b) {
    if (r == g && g == b) {
        if (r < 8) return 16;
        if (r > 248) return 231;
        int gray = (r - 8) / 10;
        if (gray > 23) gray = 23;
        return 232 + gray;
    }
    int ri = component_to_level(r);
    int gi = component_to_level(g);
    int bi = component_to_level(b);
    return 16 + 36 * ri + 6 * gi + bi;
}

// Drop-in replacement (sRGB ≈ 2.2 gamma)
static uint8_t apply_brightness(uint8_t value, float factor) {
    float x = value / 255.0f;
    // sRGB to linear
    float lin = powf(x, 2.2f);
    // scale in linear light
    lin = fminf(fmaxf(lin * factor, 0.0f), 1.0f);
    // linear back to sRGB
    float srgb = powf(lin, 1.0f/2.2f);
    int out = (int)(srgb * 255.0f + 0.5f);
    return clamp_u8(out);
}

static void init_palettes(void) {
    static int initialized = 0;
    if (initialized) return;
    const float factors[PALETTE_VARIANTS] = {0.15f, 0.3f, 0.45f, 0.8f, 1.25};
    for (int variant = 0; variant < PALETTE_VARIANTS; variant++) {
        for (int i = 0; i < PALETTE_COLORS; i++) {
            Color c = base_palette[i];
            // Always apply factor; palette 3 uses 1.00 so it's identical to base
            c.r = apply_brightness(base_palette[i].r, factors[variant]);
            c.g = apply_brightness(base_palette[i].g, factors[variant]);
            c.b = apply_brightness(base_palette[i].b, factors[variant]);
            c.term256 = rgb_to_ansi256(c.r, c.g, c.b);
            palettes[variant][i] = c;
        }
    }
    initialized = 1;
}


static inline const Color *color_from_variant(int variant, int color_index) {
    init_palettes();
    if (variant < 0 || variant >= PALETTE_VARIANTS) return NULL;
    if (color_index < 0 || color_index >= PALETTE_COLORS) return NULL;
    return &palettes[variant][color_index];
}

static inline const Color *color_from_index(uint8_t idx) {
    init_palettes();
    if (idx >= TOTAL_COLORS) return NULL;
    int variant = idx / PALETTE_COLORS;
    int color_index = idx % PALETTE_COLORS;
    return color_from_variant(variant, color_index);
}

static uint8_t nearest_palette_index(uint8_t r, uint8_t g, uint8_t b) {
    init_palettes();
    int best_index = 0;
    int best_dist = INT_MAX;
    for (int i = 0; i < TOTAL_COLORS; i++) {
        const Color *c = color_from_index((uint8_t)i);
        if (!c) continue;
        int dr = (int)r - (int)c->r;
        int dg = (int)g - (int)c->g;
        int db = (int)b - (int)c->b;
        int dist = dr*dr + dg*dg + db*db;
        if (dist < best_dist) {
            best_dist = dist;
            best_index = i;
            if (dist == 0) break;
        }
    }
    return (uint8_t)best_index;
}

static void set_current_palette_variant(int variant) {
    init_palettes();
    if (variant < 0) variant = 0;
    if (variant >= PALETTE_VARIANTS) variant = PALETTE_VARIANTS - 1;
    current_palette_variant = variant;
}

/* -------- Undo/Redo -------- */

typedef struct {
    uint16_t x, y;
    uint8_t before, after;
} Change;

#define UNDO_MAX 200000
#define CHANGE_SENTINEL 0xFFFF
static Change undo_stack[UNDO_MAX];
static Change redo_stack[UNDO_MAX];
static int undo_top = 0;
static int redo_top = 0;

static inline void push_stack(Change *stack, int *top, Change c) {
    if (*top < UNDO_MAX) {
        stack[(*top)++] = c;
    } else {
        memmove(stack, stack + 1, (UNDO_MAX - 1) * sizeof(Change));
        stack[UNDO_MAX - 1] = c;
    }
}

static inline int change_is_marker(Change c) {
    return c.x == CHANGE_SENTINEL && c.y == CHANGE_SENTINEL;
}

static inline Change change_marker_from_count(size_t count) {
    if (count > 0xFFFF) count = 0xFFFF;
    return (Change){CHANGE_SENTINEL, CHANGE_SENTINEL,
                    (uint8_t)((count >> 8) & 0xFF), (uint8_t)(count & 0xFF)};
}

static inline size_t change_marker_count(Change c) {
    return ((size_t)c.before << 8) | c.after;
}

static inline void push_change_marker(size_t count) {
    if (count == 0) return;
    push_stack(undo_stack, &undo_top, change_marker_from_count(count));
    redo_top = 0;
}

static inline void push_change(uint16_t x,uint16_t y,uint8_t before,uint8_t after){
    push_stack(undo_stack, &undo_top, (Change){x,y,before,after});
    // clear redo on new change
    redo_top = 0;
}

static void apply_change(Change c, int reverse) {
    // reverse=0 -> apply 'after', reverse=1 -> apply 'before'
    uint8_t *p = &pixels[c.y*img_w + c.x];
    *p = reverse ? c.before : c.after;
}

static int undo_action(void) {
    if (undo_top <= 0) return 0;
    Change c = undo_stack[--undo_top];
    if (change_is_marker(c)) {
        size_t count = change_marker_count(c);
        size_t undone = 0;
        while (undone < count && undo_top > 0) {
            Change step = undo_stack[--undo_top];
            if (change_is_marker(step)) {
                continue;
            }
            uint8_t cur = pixels[step.y*img_w + step.x];
            apply_change(step, 1);
            push_stack(redo_stack, &redo_top,
                       (Change){step.x, step.y, cur, pixels[step.y*img_w + step.x]});
            undone++;
        }
        if (undone > 0) {
            push_stack(redo_stack, &redo_top, change_marker_from_count(undone));
            dirty = 1;
            return 1;
        }
        return 0;
    }

    uint8_t cur = pixels[c.y*img_w + c.x];
    apply_change(c, 1);
    push_stack(redo_stack, &redo_top,
               (Change){c.x,c.y,cur, pixels[c.y*img_w + c.x]});
    dirty = 1;
    return 1;
}

static int redo_action(void) {
    if (redo_top <= 0) return 0;
    Change c = redo_stack[--redo_top];
    if (change_is_marker(c)) {
        size_t count = change_marker_count(c);
        size_t redone = 0;
        while (redone < count && redo_top > 0) {
            Change step = redo_stack[--redo_top];
            if (change_is_marker(step)) {
                continue;
            }
            uint8_t cur = pixels[step.y*img_w + step.x];
            apply_change(step, 0);
            push_stack(undo_stack, &undo_top,
                       (Change){step.x, step.y, cur, pixels[step.y*img_w + step.x]});
            redone++;
        }
        if (redone > 0) {
            push_stack(undo_stack, &undo_top, change_marker_from_count(redone));
            dirty = 1;
            return 1;
        }
        return 0;
    }

    uint8_t cur = pixels[c.y*img_w + c.x];
    apply_change(c, 0);
    push_stack(undo_stack, &undo_top,
               (Change){c.x,c.y,cur, pixels[c.y*img_w + c.x]});
    dirty = 1;
    return 1;
}

/* -------- File I/O: BMP (24-bit BI_RGB) + PPM (P6) -------- */

#pragma pack(push,1)
typedef struct {
    uint16_t bfType;      // 'BM'
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFILEHEADER;

typedef struct {
    uint32_t biSize;      // 40
    int32_t  biWidth;
    int32_t  biHeight;    // positive = bottom-up
    uint16_t biPlanes;    // 1
    uint16_t biBitCount;  // 24
    uint32_t biCompression; // 0 = BI_RGB
    uint32_t biSizeImage; // may be 0 for BI_RGB
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPINFOHEADER;
#pragma pack(pop)

static int save_ppm(const char *path){
    init_palettes();
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", img_w, img_h);
    for (int y=0; y<img_h; y++){
        for (int x=0; x<img_w; x++){
            uint8_t idx = pixels[y*img_w + x];
            uint8_t r=0,g=0,b=0;
            if (idx != EMPTY) {
                const Color *c = color_from_index(idx);
                if (c) { r = c->r; g = c->g; b = c->b; }
            }
            fputc(r,f); fputc(g,f); fputc(b,f);
        }
    }
    fclose(f);
    return 0;
}

static int save_png(const char *path){
    init_palettes();
    if (img_w <= 0 || img_h <= 0) return -1;
    if ((size_t)img_w > SIZE_MAX / (size_t)img_h) return -1;
    size_t total = (size_t)img_w * (size_t)img_h;
    if (total > SIZE_MAX / 4U) return -1;
    uint8_t *rgba = malloc(total * 4U);
    if (!rgba) return -1;
    for (int y = 0; y < img_h; y++) {
        for (int x = 0; x < img_w; x++) {
            size_t idx = (size_t)y * (size_t)img_w + (size_t)x;
            size_t base = idx * 4U;
            uint8_t cell = pixels[idx];
            if (cell == EMPTY) {
                rgba[base + 0] = 0;
                rgba[base + 1] = 0;
                rgba[base + 2] = 0;
                rgba[base + 3] = 0;
            } else {
                const Color *c = color_from_index(cell);
                uint8_t r = 0, g = 0, b = 0;
                if (c) { r = c->r; g = c->g; b = c->b; }
                rgba[base + 0] = r;
                rgba[base + 1] = g;
                rgba[base + 2] = b;
                rgba[base + 3] = 255;
            }
        }
    }
    int stride = img_w * 4;
    int ok = stbi_write_png(path, img_w, img_h, 4, rgba, stride);
    free(rgba);
    return ok ? 0 : -1;
}

static int save_bmp(const char *path){
    init_palettes();
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int w = img_w, h = img_h;
    int row_bytes = w * 3;
    int padding = (4 - (row_bytes % 4)) & 3;
    int imgsize = (row_bytes + padding) * h;

    BMPFILEHEADER bfh;
    BMPINFOHEADER bih;
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BMPFILEHEADER) + sizeof(BMPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imgsize;
    bfh.bfReserved1 = bfh.bfReserved2 = 0;

    bih.biSize = sizeof(BMPINFOHEADER);
    bih.biWidth = w;
    bih.biHeight = h; // bottom-up
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = 0;
    bih.biSizeImage = imgsize;
    bih.biXPelsPerMeter = 2835; // ~72 DPI
    bih.biYPelsPerMeter = 2835;
    bih.biClrUsed = 0;
    bih.biClrImportant = 0;

    if (fwrite(&bfh, sizeof(bfh), 1, f) != 1) { fclose(f); return -1; }
    if (fwrite(&bih, sizeof(bih), 1, f) != 1) { fclose(f); return -1; }

    uint8_t pad[3] = {0,0,0};
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint8_t idx = pixels[y*w + x];
            uint8_t r=0,g=0,b=0;
            if (idx != EMPTY) {
                const Color *c = color_from_index(idx);
                if (c) { r = c->r; g = c->g; b = c->b; }
            }
            // BMP is BGR
            fputc(b, f); fputc(g, f); fputc(r, f);
        }
        fwrite(pad, 1, padding, f);
    }
    fclose(f);
    return 0;
}

static int save_image(const char *path){
    size_t n = strlen(path);
    if (n>=4 && strcasecmp(path+n-4, ".png")==0) return save_png(path);
    if (n>=4 && strcasecmp(path+n-4, ".bmp")==0) return save_bmp(path);
    if (n>=4 && strcasecmp(path+n-4, ".ppm")==0) return save_ppm(path);
    // default to BMP if unknown extension
    return save_bmp(path);
}

static int load_bmp(const char *path){
    init_palettes();
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    BMPFILEHEADER bfh;
    BMPINFOHEADER bih;
    if (fread(&bfh, sizeof(bfh), 1, f) != 1) { fclose(f); return -1; }
    if (bfh.bfType != 0x4D42) { fclose(f); return -1; }
    if (fread(&bih, sizeof(bih), 1, f) != 1) { fclose(f); return -1; }
    if (bih.biBitCount != 24 || bih.biCompression != 0 || bih.biPlanes != 1) {
        fclose(f); return -1;
    }
    int w = bih.biWidth;
    int h = bih.biHeight;
    if (w <= 0 || h <= 0 || w > MAX_W || h > MAX_H) { fclose(f); return -1; }
    if (fseek(f, bfh.bfOffBits, SEEK_SET) != 0) { fclose(f); return -1; }

    int row_bytes = w * 3;
    int padding = (4 - (row_bytes % 4)) & 3;

    memset(pixels, EMPTY, sizeof(pixels));
    // Read bottom-up
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int b = fgetc(f), g = fgetc(f), r = fgetc(f);
            if (b==EOF || g==EOF || r==EOF) { fclose(f); return -1; }
            pixels[y*w + x] = nearest_palette_index((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        for (int p=0;p<padding;p++) fgetc(f);
    }
    fclose(f);
    img_w = w; img_h = h;
    cursor_x = cursor_y = view_x = view_y = 0;
    undo_top = redo_top = 0;
    dirty = 0;
    return 0;
}

static int load_png(const char *path){
    init_palettes();
    int w = 0, h = 0, comp = 0;
    unsigned char *data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) return -1;
    if (w <= 0 || h <= 0 || w > MAX_W || h > MAX_H) {
        stbi_image_free(data);
        return -1;
    }
    memset(pixels, EMPTY, sizeof(pixels));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            size_t base = idx * 4U;
            uint8_t r = data[base + 0];
            uint8_t g = data[base + 1];
            uint8_t b = data[base + 2];
            uint8_t a = data[base + 3];
            if (a == 0) {
                pixels[y*w + x] = EMPTY;
                continue;
            }
            if (a < 255) {
                r = (uint8_t)((((uint32_t)r) * (uint32_t)a + 127U) / 255U);
                g = (uint8_t)((((uint32_t)g) * (uint32_t)a + 127U) / 255U);
                b = (uint8_t)((((uint32_t)b) * (uint32_t)a + 127U) / 255U);
            }
            pixels[y*w + x] = nearest_palette_index(r, g, b);
        }
    }
    stbi_image_free(data);
    img_w = w;
    img_h = h;
    cursor_x = cursor_y = view_x = view_y = 0;
    undo_top = redo_top = 0;
    dirty = 0;
    return 0;
}

static int load_image(const char *path){
    size_t n = strlen(path);
    if (n >= 4 && strcasecmp(path+n-4, ".png") == 0) {
        if (load_png(path) == 0) return 0;
        return -1;
    }
    if (n >= 4 && strcasecmp(path+n-4, ".bmp") == 0) {
        if (load_bmp(path) == 0) return 0;
        return -1;
    }
    if (load_png(path) == 0) return 0;
    return load_bmp(path);
}

/* -------- Input handling -------- */

enum {
    KEY_NONE=0, KEY_ESC=27,
    KEY_UP=1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_BACKSPACE=127, KEY_DELETE=1005
};

static int decode_pending_key(const unsigned char *buf, size_t len, size_t *consumed) {
    if (len == 0) {
        *consumed = 0;
        return KEY_NONE;
    }

    unsigned char c = buf[0];
    if (c == 27) {
        if (len == 1) {
            int available = 0;
            if (ioctl(STDIN_FILENO, FIONREAD, &available) == -1) {
                available = 0;
            }
            if (available == 0) {
                *consumed = 1;
                return KEY_ESC;
            }
            *consumed = 0;
            return KEY_NONE;
        }
        if (buf[1] != '[') {
            *consumed = 1;
            return KEY_ESC;
        }
        if (len < 3) {
            *consumed = 0;
            return KEY_NONE;
        }
        switch (buf[2]) {
            case 'A':
                *consumed = 3;
                return KEY_UP;
            case 'B':
                *consumed = 3;
                return KEY_DOWN;
            case 'C':
                *consumed = 3;
                return KEY_RIGHT;
            case 'D':
                *consumed = 3;
                return KEY_LEFT;
            case '3':
                if (len >= 4 && buf[3] == '~') {
                    *consumed = 4;
                    return KEY_DELETE;
                }
                break;
            default:
                break;
        }
        // Unknown or incomplete sequence; consume the ESC to avoid stalling.
        *consumed = 1;
        return KEY_NONE;
    }

    *consumed = 1;
    if (c <= 31) return c; // Ctrl-A..Ctrl-_
    if (c == 127) return KEY_BACKSPACE;
    return c;
}

static int read_key(void) {
    static unsigned char pending[64];
    static size_t pending_len = 0;

    for (;;) {
        size_t consumed = 0;
        int key = decode_pending_key(pending, pending_len, &consumed);
        if (consumed > 0) {
            if (consumed < pending_len) {
                memmove(pending, pending + consumed, pending_len - consumed);
            }
            pending_len -= consumed;
            if (key != KEY_NONE) {
                return key;
            }
            continue;
        }

        if (pending_len == sizeof(pending)) {
            pending_len = 0;
        }

        ssize_t n = read(STDIN_FILENO, pending + pending_len, sizeof(pending) - pending_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return KEY_NONE;
            }
            die("read");
        }
        if (n == 0) {
            return KEY_NONE;
        }
        pending_len += (size_t)n;
    }
}

static int handle_key_event(int key, int *running) {
    if (key == KEY_NONE) {
        return 0;
    }

    int need_render = 0;

    if (fill_color_pending) {
        int require_full = 0;
        if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')) {
            char up = (char)toupper(key);
            int idx = up - 'A';
            if (idx >= 0 && idx < PALETTE_COLORS) {
                uint8_t color_idx = (uint8_t)(current_palette_variant * PALETTE_COLORS + idx);
                flood_fill_at_cursor(color_idx);
                require_full = 1;
            }
        }
        fill_color_pending = 0;
        if (require_full) {
            return 1;
        }
        if (!refresh_cursor_cell_partial()) {
            return 1;
        }
        return 0;
    }

    switch (key) {
        case KEY_UP:
            if (cursor_y > 0) {
                int old_x = cursor_x;
                int old_y = cursor_y;
                cursor_y--;
                if (!update_cursor_partial(old_x, old_y)) {
                    need_render = 1;
                }
            }
            break;
        case KEY_DOWN:
            if (cursor_y < img_h - 1) {
                int old_x = cursor_x;
                int old_y = cursor_y;
                cursor_y++;
                if (!update_cursor_partial(old_x, old_y)) {
                    need_render = 1;
                }
            }
            break;
        case KEY_LEFT:
            if (cursor_x > 0) {
                int old_x = cursor_x;
                int old_y = cursor_y;
                cursor_x--;
                if (!update_cursor_partial(old_x, old_y)) {
                    need_render = 1;
                }
            }
            break;
        case KEY_RIGHT:
            if (cursor_x < img_w - 1) {
                int old_x = cursor_x;
                int old_y = cursor_y;
                cursor_x++;
                if (!update_cursor_partial(old_x, old_y)) {
                    need_render = 1;
                }
            }
            break;

        case KEY_BACKSPACE:
        case KEY_DELETE:
            if (paint_at_cursor(EMPTY)) {
                if (!refresh_cursor_cell_partial()) {
                    need_render = 1;
                }
            }
            break;

        case '1':
            set_current_palette_variant(0);
            need_render = 1;
            break;
        case '2':
            set_current_palette_variant(1);
            need_render = 1;
            break;
        case '3':
            set_current_palette_variant(2);
            need_render = 1;
            break;
        case '4':
            set_current_palette_variant(3);
            need_render = 1;
            break;
        case '5':
            set_current_palette_variant(4);
            need_render = 1;
            break;

        case 19: /* Ctrl+S */
            save_dialog();
            need_render = 1;
            break;
        case 15: /* Ctrl+O */
            load_dialog();
            need_render = 1;
            break;
        case 14: /* Ctrl+N */
            new_image_dialog();
            need_render = 1;
            break;
        case 18: /* Ctrl+R */
            resize_canvas_dialog();
            need_render = 1;
            break;
        case 26: /* Ctrl+Z */
            undo_action();
            need_render = 1;
            break;
        case 25: /* Ctrl+Y */
            redo_action();
            need_render = 1;
            break;
        case 6:  /* Ctrl+F */
            fill_color_pending = 1;
            if (!refresh_cursor_cell_partial()) {
                need_render = 1;
            }
            break;
        case 17: /* Ctrl+Q */
            if (dirty) {
                char ans[8];
                prompt("Unsaved changes. Save? (y/n) ", ans, sizeof(ans));
                if (ans[0] == 'y' || ans[0] == 'Y') {
                    save_dialog();
                }
            }
            *running = 0;
            break;

        default:
            if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')) {
                char up = (char)toupper(key);
                int idx = up - 'A';
                if (idx >= 0 && idx < PALETTE_COLORS) {
                    uint8_t color_idx = (uint8_t)(current_palette_variant * PALETTE_COLORS + idx);
                    if (paint_at_cursor(color_idx)) {
                        if (!refresh_cursor_cell_partial()) {
                            need_render = 1;
                        }
                    }
                }
            }
            break;
    }

    return need_render;
}

/* -------- UI / Rendering -------- */

static void append_hint(char *line, size_t cap, int *len, const char *hint) {
    if (*len >= (int)cap - 1) return;
    if (*len > 0) {
        if (line[*len - 1] != ' ' && *len < (int)cap - 1) {
            line[(*len)++] = ' ';
        }
        if (*len < (int)cap - 1) {
            line[(*len)++] = ' ';
        }
    }
    for (const char *p = hint; *p && *len < (int)cap - 1; ++p) {
        line[(*len)++] = *p;
    }
    line[*len] = '\0';
}

static void draw_status_lines(int cols) {
    char line1[256];
    char line2[256];
    const char *fill_msg = fill_color_pending ? "  Fill:Pick color" : "";
    snprintf(line1, sizeof(line1),
        " %dx%d  Cursor:%d,%d  View:%d,%d  Palette:^1-^5:%d  %s%s",
        img_w, img_h, cursor_x, cursor_y, view_x, view_y,
        current_palette_variant + 1, dirty ? "Dirty" : "Saved", fill_msg);
    line1[sizeof(line1) - 1] = '\0';
    int len1 = (int)strlen(line1);

    line2[0] = '\0';
    int len2 = 0;

    const char *shortcuts[] = {
        "Draw:A-Z",
        "Fill:^F+Color",
        "Brightness:^1-^5",
        "Erase:Backspace/Delete",
        "Resize:^R",
        "Undo:^Z",
        "Redo:^Y",
        "Save:^S",
        "Load:^O",
        "New:^N",
        "Quit:^Q"
    };
    size_t shortcut_count = sizeof(shortcuts) / sizeof(shortcuts[0]);
    size_t split = (shortcut_count + 1) / 2;
    for (size_t i = 0; i < shortcut_count; ++i) {
        if (i < split) {
            int prev_len = len1;
            append_hint(line1, sizeof(line1), &len1, shortcuts[i]);
            if (cols > 0 && len1 > cols - 1) {
                len1 = prev_len;
                line1[len1] = '\0';
                append_hint(line2, sizeof(line2), &len2, shortcuts[i]);
            }
        } else {
            append_hint(line2, sizeof(line2), &len2, shortcuts[i]);
        }
    }

    int max_len = cols > 0 ? cols - 1 : 0;
    int write_len1 = len1;
    int write_len2 = len2;
    if (write_len1 > max_len) write_len1 = max_len;
    if (write_len2 > max_len) write_len2 = max_len;

    write(STDOUT_FILENO, "\x1b[7m", 4);
    write(STDOUT_FILENO, line1, write_len1);
    for (int i = write_len1; i < max_len; i++) write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\x1b[0m", 4);

    write(STDOUT_FILENO, "\r\n", 2);

    write(STDOUT_FILENO, "\x1b[7m", 4);
    write(STDOUT_FILENO, line2, write_len2);
    for (int i = write_len2; i < max_len; i++) write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\x1b[0m", 4);
}

static int ensure_cursor_visible_for_area(int draw_rows, int draw_cols) {
    int old_view_x = view_x;
    int old_view_y = view_y;

    if (cursor_x < view_x) view_x = cursor_x;
    if (cursor_y < view_y) view_y = cursor_y;

    if (draw_cols > 0 && cursor_x >= view_x + draw_cols) {
        view_x = cursor_x - draw_cols + 1;
    }
    if (draw_rows > 0 && cursor_y >= view_y + draw_rows) {
        view_y = cursor_y - draw_rows + 1;
    }

    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;

    int max_view_x = img_w - draw_cols;
    if (max_view_x < 0) max_view_x = 0;
    if (view_x > max_view_x) view_x = max_view_x;

    int max_view_y = img_h - draw_rows;
    if (max_view_y < 0) max_view_y = 0;
    if (view_y > max_view_y) view_y = max_view_y;

    return (view_x != old_view_x) || (view_y != old_view_y);
}

static void set_color_ansi(const Color *color){
#if USE_ANSI_COLOR
    if (!color) {
        write(STDOUT_FILENO, "\x1b[39m", 5);
        return;
    }
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[38;2;%u;%u;%um",
                     (unsigned int)color->r,
                     (unsigned int)color->g,
                     (unsigned int)color->b);
    write(STDOUT_FILENO, seq, n);
#else
    (void)color;
#endif
}

#if USE_ANSI_COLOR
static void set_bg_color_ansi(const Color *color){
    if (!color) {
        write(STDOUT_FILENO, "\x1b[49m", 5);
        return;
    }
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[48;2;%u;%u;%um",
                     (unsigned int)color->r,
                     (unsigned int)color->g,
                     (unsigned int)color->b);
    write(STDOUT_FILENO, seq, n);
}

static void reset_ansi_colors(void){
    write(STDOUT_FILENO, "\x1b[39m", 5);
    write(STDOUT_FILENO, "\x1b[49m", 5);
}
#else
static void set_bg_color_ansi(const Color *color){ (void)color; }
static void reset_ansi_colors(void){}
#endif

static void draw_cell(uint8_t idx, int highlight){
    char ch;
    const Color *cell_color = NULL;
    if (idx == EMPTY) {
        ch = '.';
        set_bg_color_ansi(NULL);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    } else if (idx < TOTAL_COLORS) {
        cell_color = color_from_index(idx);
#if USE_ANSI_COLOR
        ch = ' ';
#else
        ch = cell_color ? cell_color->letter : '?';
#endif
        set_bg_color_ansi(cell_color);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    } else {
        ch = '?';
        set_bg_color_ansi(NULL);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    }

#if USE_ANSI_COLOR
    if (highlight) {
        char cursor = '+';
        if (cell_color) {
            int luminance = (int)cell_color->r * 299 + (int)cell_color->g * 587 + (int)cell_color->b * 114;
            const char *fg_seq = (luminance > 128000) ? "\x1b[30m" : "\x1b[97m";
            write(STDOUT_FILENO, fg_seq, 5);
        } else {
            write(STDOUT_FILENO, "\x1b[97m", 5);
        }
        write(STDOUT_FILENO, &cursor, 1);
        reset_ansi_colors();
        return;
    }
#else
    (void)cell_color;
    if (highlight) {
        write(STDOUT_FILENO, "\x1b[7m", 4);
    }
#endif
    write(STDOUT_FILENO, &ch, 1);
#if !USE_ANSI_COLOR
    if (highlight) {
        write(STDOUT_FILENO, "\x1b[0m", 4);
    }
#endif
    reset_ansi_colors();
}

typedef struct {
    int rows;
    int cols;
    int draw_rows;
    int draw_cols;
} ScreenLayout;

static int compute_screen_layout(ScreenLayout *layout) {
    get_terminal_size(&layout->rows, &layout->cols);
    if (layout->rows < 5 || layout->cols <= 0) {
        return 0;
    }
    layout->draw_rows = layout->rows - 3;
    layout->draw_cols = layout->cols;
    if (layout->draw_rows <= 0 || layout->draw_cols <= 0) {
        return 0;
    }
    return 1;
}

static int cell_visible_in_layout(const ScreenLayout *layout, int x, int y) {
    if (layout->draw_cols <= 0 || layout->draw_rows <= 0) {
        return 0;
    }
    return x >= view_x && x < view_x + layout->draw_cols &&
           y >= view_y && y < view_y + layout->draw_rows;
}

static void move_terminal_to(int row, int col) {
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\x1b[%d;%dH", row, col);
    write(STDOUT_FILENO, seq, len);
}

static void draw_cell_at_layout(int x, int y, int highlight) {
    int term_row = (y - view_y) + 2;
    int term_col = (x - view_x) + 1;
    move_terminal_to(term_row, term_col);
    uint8_t idx = pixels[y * img_w + x];
    draw_cell(idx, highlight);
}

static void redraw_status_from_layout(const ScreenLayout *layout) {
    move_terminal_to(layout->draw_rows + 2, 1);
    draw_status_lines(layout->cols);
}

static int refresh_cursor_cell_partial(void) {
    ScreenLayout layout;
    if (!compute_screen_layout(&layout)) {
        return 0;
    }
    if (ensure_cursor_visible_for_area(layout.draw_rows, layout.draw_cols)) {
        return 0;
    }
    if (!cell_visible_in_layout(&layout, cursor_x, cursor_y)) {
        return 0;
    }
    draw_cell_at_layout(cursor_x, cursor_y, 1);
    redraw_status_from_layout(&layout);
    return 1;
}

static int update_cursor_partial(int old_x, int old_y) {
    ScreenLayout layout;
    if (!compute_screen_layout(&layout)) {
        return 0;
    }
    if (ensure_cursor_visible_for_area(layout.draw_rows, layout.draw_cols)) {
        return 0;
    }
    if (!cell_visible_in_layout(&layout, old_x, old_y) ||
        !cell_visible_in_layout(&layout, cursor_x, cursor_y)) {
        return 0;
    }
    draw_cell_at_layout(old_x, old_y, 0);
    draw_cell_at_layout(cursor_x, cursor_y, 1);
    redraw_status_from_layout(&layout);
    return 1;
}

static void render(void){
    int rows, cols;
    get_terminal_size(&rows, &cols);
    if (rows < 5 || cols < 10) return;

    int draw_rows = rows - 3; // palette line + two status/help lines
    int draw_cols = cols;

    ensure_cursor_visible_for_area(draw_rows, draw_cols);

    // Clear & move home
    write(STDOUT_FILENO, "\x1b[H", 3);

    // Palette line
    write(STDOUT_FILENO, " Palette: ", 10);
    for (int i=0;i<PALETTE_COLORS;i++){
        const Color *c = color_from_variant(current_palette_variant, i);
        set_color_ansi(c);
        char ch = c ? c->letter : '?';
        write(STDOUT_FILENO, &ch, 1);
        write(STDOUT_FILENO, " ", 1);
    }
    reset_ansi_colors();
    // Fill to end
    int curcol = 10 + 2*PALETTE_COLORS;
    int cols_now; get_terminal_size(&rows,&cols_now);
    for (int i=curcol;i<cols_now;i++) write(STDOUT_FILENO, " ", 1);

    // Draw viewport
    for (int ry = 0; ry < draw_rows; ry++) {
        write(STDOUT_FILENO, "\r\n", 2);
        int y = view_y + ry;

        for (int rx = 0; rx < draw_cols; rx++) {
            int x = view_x + rx;
            int in_bounds = (x >= 0 && x < img_w && y >= 0 && y < img_h);

            if (!in_bounds) {
                // Outside the image area: print space (no dot, no color)
                write(STDOUT_FILENO, " ", 1);
                continue;
            }

            uint8_t idx = pixels[y * img_w + x];
            draw_cell(idx, x == cursor_x && y == cursor_y);
        }
    }

    write(STDOUT_FILENO, "\r\n", 2);
    // Status + help lines
    draw_status_lines(cols_now);
}

/* -------- Prompts (line input in raw mode) -------- */

static void prompt(const char *msg, char *out, size_t cap){
    // Temporarily show cursor & enter a simple line mode
    write(STDOUT_FILENO, "\x1b[?25h", 6);
    int rows, cols; get_terminal_size(&rows,&cols);
    // Place prompt on last line
    char clrline[32]; snprintf(clrline, sizeof(clrline), "\x1b[%d;1H\x1b[2K", rows);
    write(STDOUT_FILENO, clrline, strlen(clrline));
    write(STDOUT_FILENO, msg, strlen(msg));
    size_t len = 0; out[0] = '\0';
    while (1){
        char c;
        int n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) continue;
        unsigned char uc = (unsigned char)c;
        if (uc == '\r' || uc == '\n') break;
        if (uc == 127 || uc == 8) { // backspace
            if (len>0){
                len--; out[len]='\0';
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        if (isprint(uc) && len+1 < cap){
            out[len++] = uc; out[len] = '\0';
            write(STDOUT_FILENO, &uc, 1);
        }
    }
    // Hide cursor again
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

static int prompt_int(const char *msg, int def, int minv, int maxv){
    char buf[64];
    char promptmsg[128];
    snprintf(promptmsg, sizeof(promptmsg), "%s [%d]: ", msg, def);
    prompt(promptmsg, buf, sizeof(buf));
    if (buf[0] == '\0') return def;
    int v = atoi(buf);
    if (v < minv) v = minv;
    if (v > maxv) v = maxv;
    return v;
}

/* -------- Core actions -------- */

static void new_image_dialog(void){
    int w = prompt_int("Width", img_w, 1, MAX_W);
    int h = prompt_int("Height", img_h, 1, MAX_H);
    img_w = w; img_h = h;
    for (int i=0;i<img_w*img_h;i++) pixels[i]=EMPTY;
    cursor_x = cursor_y = view_x = view_y = 0;
    undo_top = redo_top = 0;
    dirty = 0;
    set_current_file_path(NULL);
}

static void resize_canvas_dialog(void){
    int old_w = img_w;
    int old_h = img_h;
    int w = prompt_int("New width", img_w, 1, MAX_W);
    int h = prompt_int("New height", img_h, 1, MAX_H);
    if (w == old_w && h == old_h) return;

    uint8_t temp[MAX_W * MAX_H];
    memset(temp, EMPTY, sizeof(temp));

    int copy_w = (w < old_w) ? w : old_w;
    int copy_h = (h < old_h) ? h : old_h;
    for (int y = 0; y < copy_h; y++) {
        memcpy(&temp[y * w], &pixels[y * old_w], copy_w);
    }

    memset(pixels, EMPTY, sizeof(pixels));
    img_w = w;
    img_h = h;
    for (int y = 0; y < img_h; y++) {
        memcpy(&pixels[y * img_w], &temp[y * img_w], img_w);
    }

    if (cursor_x >= img_w) cursor_x = img_w - 1;
    if (cursor_y >= img_h) cursor_y = img_h - 1;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (view_x > cursor_x) view_x = cursor_x;
    if (view_y > cursor_y) view_y = cursor_y;
    if (view_x >= img_w) view_x = img_w - 1;
    if (view_y >= img_h) view_y = img_h - 1;
    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;

    undo_top = redo_top = 0;
    dirty = 1;
}

static void save_dialog(void){
    if (current_file_path[0] != '\0') {
        if (save_image(current_file_path) == 0) {
            dirty = 0;
        }
        return;
    }

    char path[512];
    prompt("Save as (.png / .bmp / .ppm): ", path, sizeof(path));
    if (path[0]=='\0') return;
    if (save_image(path)==0) {
        set_current_file_path(path);
        dirty=0;
    }
}

static void load_dialog(void){
    char path[512];
    prompt("Load image (.png / .bmp): ", path, sizeof(path));
    if (path[0]=='\0') return;
    if (load_image(path)!=0){
        // message on status line briefly
    } else {
        set_current_file_path(path);
    }
}

static int paint_at_cursor(uint8_t color_idx){
    if (cursor_x<0||cursor_x>=img_w||cursor_y<0||cursor_y>=img_h) return 0;
    if (color_idx != EMPTY && color_idx >= TOTAL_COLORS) return 0;
    uint8_t *p = &pixels[cursor_y*img_w + cursor_x];
    if (*p == color_idx) return 0; // no-op
    push_change(cursor_x, cursor_y, *p, color_idx);
    *p = color_idx;
    dirty = 1;
    return 1;
}

static void flood_fill_at_cursor(uint8_t color_idx){
    if (cursor_x<0||cursor_x>=img_w||cursor_y<0||cursor_y>=img_h) return;
    if (color_idx != EMPTY && color_idx >= TOTAL_COLORS) return;

    uint8_t target = pixels[cursor_y*img_w + cursor_x];
    if (target == color_idx) return;

    size_t total = (size_t)img_w * (size_t)img_h;
    if (total == 0) return;
    typedef struct { int x; int y; } Point;
    Point *stack = malloc(total * sizeof(Point));
    uint8_t *visited = calloc(total, sizeof(uint8_t));
    if (!stack || !visited) {
        free(stack);
        free(visited);
        return;
    }

    size_t sp = 0;
    int start_idx = cursor_y * img_w + cursor_x;
    visited[start_idx] = 1;
    stack[sp++] = (Point){cursor_x, cursor_y};

    size_t changed_count = 0;
    const int neighbor_offsets[4][2] = {
        { 1, 0 },
        {-1, 0 },
        { 0, 1 },
        { 0,-1 }
    };
    while (sp > 0) {
        Point p = stack[--sp];
        int idx = p.y * img_w + p.x;
        if (pixels[idx] != target) continue;

        push_change((uint16_t)p.x, (uint16_t)p.y, pixels[idx], color_idx);
        pixels[idx] = color_idx;
        changed_count++;

        for (int i = 0; i < 4; i++) {
            int nx = p.x + neighbor_offsets[i][0];
            int ny = p.y + neighbor_offsets[i][1];
            if (nx < 0 || ny < 0 || nx >= img_w || ny >= img_h) continue;
            int nidx = ny * img_w + nx;
            if (visited[nidx]) continue;
            if (pixels[nidx] != target) continue;
            visited[nidx] = 1;
            stack[sp++] = (Point){nx, ny};
        }
    }

    free(stack);
    free(visited);
    if (changed_count > 0) {
        push_change_marker(changed_count);
        dirty = 1;
    }
}

/* -------- Main -------- */

int main(int argc, char **argv){
    init_palettes();
    set_current_palette_variant(2);

    // Initialize default image
    for (int i=0;i<MAX_W*MAX_H;i++) pixels[i]=EMPTY;

    int loaded_from_arg = 0;
    if (argc > 1) {
        if (load_image(argv[1]) == 0) {
            loaded_from_arg = 1;
            set_current_file_path(argv[1]);
        } else {
            fprintf(stderr, "Failed to load image: %s\n", argv[1]);
        }
    }

    enable_raw_mode();
    clear_screen();

    // New image prompt at start unless an image was supplied
    if (!loaded_from_arg) {
        new_image_dialog();
    }

    int running = 1;
    int need_render = 1;
    while (running) {
        if (need_render) {
            render();
            need_render = 0;
        }

        int key = read_key();
        if (key == KEY_NONE) {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int ret = poll(&pfd, 1, 16);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                die("poll");
            }
            continue;
        }

        do {
            if (handle_key_event(key, &running)) {
                need_render = 1;
            }
            if (!running) {
                break;
            }
            key = read_key();
        } while (key != KEY_NONE);
    }

    clear_screen();
    // Leave the cursor visible (atexit will also restore)
    return 0;
}
