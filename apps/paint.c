/*
 * paint.c — keyboard-only terminal pixel editor (ASCII), single-file C
 * Features:
 *  - New / Load / Save (BMP 24-bit uncompressed; optional PPM P6)
 *  - Undo (Ctrl+Z), Redo (Ctrl+Y)
 *  - Arrow-keys move cursor, auto-scrolling viewport
 *  - A–Z paints with 26-color palette; Backspace/Delete erases
 *  - Ctrl+F then color floods a region using 8-direction adjacency
 *  - Ctrl+1..Ctrl+5 cycle palette brightness (3 = default)
 *  - Max resolution 320x200
 *  - Works in a terminal using raw mode (termios) + ANSI escapes
 *
 * Build:   gcc paint.c -o paint -Wall
 * Run:     ./paint
 *
 * Notes:
 *  - PNG is not implemented (needs external libs). Use BMP or PPM.
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
    raw.c_cc[VTIME] = 1; // 100ms
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    // Hide cursor
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

static void clear_screen(void) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        *rows = 24;
        *cols = 80;
        return;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

/* -------- Image data -------- */

static int img_w = 64, img_h = 48;
static uint8_t pixels[MAX_W * MAX_H]; // Each = 0..TOTAL_COLORS-1 color index, or EMPTY

static int cursor_x = 0, cursor_y = 0;
static int view_x = 0, view_y = 0;
static int dirty = 0; // unsaved changes
static int fill_color_pending = 0;

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
    int level = (v * 5 + 127) / 255;
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    return level;
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

static uint8_t apply_brightness(uint8_t value, float factor) {
    int adjusted = (int)(value * factor + 0.5f);
    return clamp_u8(adjusted);
}

static void init_palettes(void) {
    static int initialized = 0;
    if (initialized) return;
    const float factors[PALETTE_VARIANTS] = {0.6f, 0.8f, 1.0f, 1.2f, 1.4f};
    for (int variant = 0; variant < PALETTE_VARIANTS; variant++) {
        for (int i = 0; i < PALETTE_COLORS; i++) {
            Color c = base_palette[i];
            if (variant == 2) {
                palettes[variant][i] = c;
                continue;
            }
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

    // Read bottom-up
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int b = fgetc(f), g = fgetc(f), r = fgetc(f);
            if (b==EOF || g==EOF || r==EOF) { fclose(f); return -1; }
            // Map to nearest palette index
            int best = -1;
            int bestd = 1<<30;
            for (int i=0;i<TOTAL_COLORS;i++){
                const Color *c = color_from_index((uint8_t)i);
                if (!c) continue;
                int dr = (int)r - (int)c->r;
                int dg = (int)g - (int)c->g;
                int db = (int)b - (int)c->b;
                int d = dr*dr + dg*dg + db*db;
                if (d < bestd) { bestd = d; best = i; }
            }
            if (best < 0) best = 0;
            pixels[y*w + x] = (uint8_t)best;
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

/* -------- Input handling -------- */

enum {
    KEY_NONE=0, KEY_ESC=27,
    KEY_UP=1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_BACKSPACE=127, KEY_DELETE=1005
};

static int read_key(void){
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    if (n == 0) return KEY_NONE;
    unsigned char uc = (unsigned char)c;
    if (uc == 27) { // ESC sequence?
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return KEY_ESC;
        if (seq[0] == '[') {
            if (seq[1] >= 'A' && seq[1] <= 'D') {
                switch (seq[1]) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                }
            } else if (seq[1] == '3') {
                char t;
                if (read(STDIN_FILENO, &t, 1) && t=='~') return KEY_DELETE;
            }
        }
        return KEY_NONE;
    }
    // Map Ctrl+key
    if (uc <= 31) return uc; // Ctrl-A..Ctrl-_
    if (uc == 127) return KEY_BACKSPACE;
    return uc; // regular ASCII
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

static void set_color_ansi(const Color *color){
#if USE_ANSI_COLOR
    if (!color) { write(STDOUT_FILENO, "\x1b[39m", 5); return; }
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[38;5;%dm", color->term256);
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
    int n = snprintf(seq, sizeof(seq), "\x1b[48;5;%dm", color->term256);
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
    if (idx == EMPTY) {
        ch = '.';
        set_bg_color_ansi(NULL);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    } else if (idx < TOTAL_COLORS) {
        const Color *color = color_from_index(idx);
#if USE_ANSI_COLOR
        ch = ' ';
#else
        ch = color ? color->letter : '?';
#endif
        set_bg_color_ansi(color);
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
        write(STDOUT_FILENO, "\x1b[97m", 5);
        write(STDOUT_FILENO, &cursor, 1);
        reset_ansi_colors();
        return;
    }
#else
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

static void render(void){
    int rows, cols;
    get_terminal_size(&rows, &cols);
    if (rows < 5 || cols < 10) return;

    int draw_rows = rows - 3; // palette line + two status/help lines
    int draw_cols = cols;

    // Clamp view to ensure cursor visible
    if (cursor_x < view_x) view_x = cursor_x;
    if (cursor_y < view_y) view_y = cursor_y;
    if (cursor_x >= view_x + draw_cols) view_x = cursor_x - draw_cols + 1;
    if (cursor_y >= view_y + draw_rows) view_y = cursor_y - draw_rows + 1;

    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;

    int max_view_x = img_w - draw_cols;
    if (max_view_x < 0) max_view_x = 0;
    if (view_x > max_view_x) view_x = max_view_x;

    int max_view_y = img_h - draw_rows;
    if (max_view_y < 0) max_view_y = 0;
    if (view_y > max_view_y) view_y = max_view_y;

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
    char path[512];
    prompt("Save as (.bmp / .ppm): ", path, sizeof(path));
    if (path[0]=='\0') return;
    if (save_image(path)==0) dirty=0;
}

static void load_dialog(void){
    char path[512];
    prompt("Load BMP file: ", path, sizeof(path));
    if (path[0]=='\0') return;
    if (load_bmp(path)!=0){
        // message on status line briefly
    }
}

static void paint_at_cursor(uint8_t color_idx){
    if (cursor_x<0||cursor_x>=img_w||cursor_y<0||cursor_y>=img_h) return;
    if (color_idx != EMPTY && color_idx >= TOTAL_COLORS) return;
    uint8_t *p = &pixels[cursor_y*img_w + cursor_x];
    if (*p == color_idx) return; // no-op
    push_change(cursor_x, cursor_y, *p, color_idx);
    *p = color_idx;
    dirty = 1;
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
    while (sp > 0) {
        Point p = stack[--sp];
        int idx = p.y * img_w + p.x;
        if (pixels[idx] != target) continue;

        push_change((uint16_t)p.x, (uint16_t)p.y, pixels[idx], color_idx);
        pixels[idx] = color_idx;
        changed_count++;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = p.x + dx;
                int ny = p.y + dy;
                if (nx < 0 || ny < 0 || nx >= img_w || ny >= img_h) continue;
                int nidx = ny * img_w + nx;
                if (visited[nidx]) continue;
                if (pixels[nidx] != target) continue;
                visited[nidx] = 1;
                stack[sp++] = (Point){nx, ny};
            }
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

int main(void){
    init_palettes();
    set_current_palette_variant(2);

    // Initialize default image
    for (int i=0;i<MAX_W*MAX_H;i++) pixels[i]=EMPTY;

    enable_raw_mode();
    clear_screen();

    // New image prompt at start
    new_image_dialog();

    int running = 1;
    while (running){
        render();
        int key = read_key();
        if (key == KEY_NONE) continue;

        if (fill_color_pending) {
            if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')) {
                char up = (char)toupper(key);
                int idx = up - 'A';
                if (idx >=0 && idx < PALETTE_COLORS){
                    uint8_t color_idx = (uint8_t)(current_palette_variant * PALETTE_COLORS + idx);
                    flood_fill_at_cursor(color_idx);
                }
                fill_color_pending = 0;
                continue;
            }
            fill_color_pending = 0;
        }

        switch (key) {
            case KEY_UP:    if (cursor_y > 0) cursor_y--; break;
            case KEY_DOWN:  if (cursor_y < img_h-1) cursor_y++; break;
            case KEY_LEFT:  if (cursor_x > 0) cursor_x--; break;
            case KEY_RIGHT: if (cursor_x < img_w-1) cursor_x++; break;

            case KEY_BACKSPACE:
            case KEY_DELETE:
                paint_at_cursor(EMPTY);
                break;

            case '1': set_current_palette_variant(0); break;
            case '2': set_current_palette_variant(1); break;
            case '3': set_current_palette_variant(2); break;
            case '4': set_current_palette_variant(3); break;
            case '5': set_current_palette_variant(4); break;

            // Ctrl shortcuts
            case 19: /* Ctrl+S */ save_dialog(); break;
            case 15: /* Ctrl+O */ load_dialog(); break;
            case 14: /* Ctrl+N */ new_image_dialog(); break;
            case 18: /* Ctrl+R */ resize_canvas_dialog(); break;
            case 26: /* Ctrl+Z */ undo_action(); break;
            case 25: /* Ctrl+Y */ redo_action(); break;
            case 6:  /* Ctrl+F */ fill_color_pending = 1; break;
            case 17: /* Ctrl+Q */
                if (dirty){
                    char ans[8];
                    prompt("Unsaved changes. Save? (y/n) ", ans, sizeof(ans));
                    if (ans[0]=='y' || ans[0]=='Y') save_dialog();
                }
                running = 0; break;

            default:
                // Letters A-Z map to palette indices 0..25 within the active brightness set
                if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')){
                    char up = (char)toupper(key);
                    int idx = up - 'A';
                    if (idx >=0 && idx < PALETTE_COLORS){
                        uint8_t color_idx = (uint8_t)(current_palette_variant * PALETTE_COLORS + idx);
                        paint_at_cursor(color_idx);
                    }
                }
                break;
        }
    }

    clear_screen();
    // Leave the cursor visible (atexit will also restore)
    return 0;
}
