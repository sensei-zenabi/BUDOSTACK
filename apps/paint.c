/*
 * paint.c — keyboard-only terminal pixel editor (ASCII), single-file C
 * Features:
 *  - New / Load / Save (BMP 24-bit uncompressed; optional PPM P6)
 *  - Undo (Ctrl+Z), Redo (Ctrl+Y)
 *  - Arrow-keys move cursor, auto-scrolling viewport
 *  - A–Z paints with 26-color palette; Backspace/Delete erases
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
static uint8_t pixels[MAX_W * MAX_H]; // Each = 0..25 color index, or EMPTY

static int cursor_x = 0, cursor_y = 0;
static int view_x = 0, view_y = 0;
static int dirty = 0; // unsaved changes

/* Palette: 26 entries mapped to letters A..Z (RGB 0..255) */
typedef struct { uint8_t r,g,b; char letter; const char *name; int term256; } Color;
static Color palette[26] = {
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

/* -------- Undo/Redo -------- */

typedef struct {
    uint16_t x, y;
    uint8_t before, after;
} Change;

#define UNDO_MAX 200000
static Change undo_stack[UNDO_MAX];
static Change redo_stack[UNDO_MAX];
static int undo_top = 0;
static int redo_top = 0;

static inline void push_change(uint16_t x,uint16_t y,uint8_t before,uint8_t after){
    if (undo_top < UNDO_MAX) {
        undo_stack[undo_top++] = (Change){x,y,before,after};
    } else {
        // simple drop oldest: shift left (O(n)); acceptable for this scale
        memmove(undo_stack, undo_stack+1, (UNDO_MAX-1)*sizeof(Change));
        undo_stack[UNDO_MAX-1] = (Change){x,y,before,after};
    }
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
    // Apply reverse
    uint8_t cur = pixels[c.y*img_w + c.x];
    apply_change(c, 1);
    // For redo, we need inverse of what we just did (swap before/after)
    if (redo_top < UNDO_MAX) {
        redo_stack[redo_top++] = (Change){c.x,c.y,cur, pixels[c.y*img_w + c.x]};
    }
    dirty = 1;
    return 1;
}

static int redo_action(void) {
    if (redo_top <= 0) return 0;
    Change c = redo_stack[--redo_top];
    uint8_t cur = pixels[c.y*img_w + c.x];
    apply_change(c, 0);
    if (undo_top < UNDO_MAX) {
        undo_stack[undo_top++] = (Change){c.x,c.y,cur, pixels[c.y*img_w + c.x]};
    }
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
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", img_w, img_h);
    for (int y=0; y<img_h; y++){
        for (int x=0; x<img_w; x++){
            uint8_t idx = pixels[y*img_w + x];
            uint8_t r=0,g=0,b=0;
            if (idx != EMPTY) { r=palette[idx].r; g=palette[idx].g; b=palette[idx].b; }
            fputc(r,f); fputc(g,f); fputc(b,f);
        }
    }
    fclose(f);
    return 0;
}

static int save_bmp(const char *path){
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
            if (idx != EMPTY) { r=palette[idx].r; g=palette[idx].g; b=palette[idx].b; }
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
            for (int i=0;i<26;i++){
                int dr = (int)r - (int)palette[i].r;
                int dg = (int)g - (int)palette[i].g;
                int db = (int)b - (int)palette[i].b;
                int d = dr*dr + dg*dg + db*db;
                if (d < bestd) { bestd = d; best = i; }
            }
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

static void draw_status_line(int cols) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        " %dx%d  Cursor:%d,%d  Color:A-Z  Erase:Backspace  Undo:^Z  Redo:^Y  Save:^S  Load:^O  New:^N  Quit:^Q %s",
        img_w, img_h, cursor_x, cursor_y, dirty ? "[*]" : "   ");
    if (len > cols-1) len = cols-1;
    write(STDOUT_FILENO, "\x1b[7m", 4); // inverse
    write(STDIN_FILENO, "", 0); // no-op to silence unused warning on some compilers
    write(STDOUT_FILENO, buf, len);
    for (int i=len;i<cols-1;i++) write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, "\x1b[0m", 4);
}

static void set_color_ansi(uint8_t idx){
#if USE_ANSI_COLOR
    if (idx==EMPTY){ write(STDOUT_FILENO, "\x1b[39m", 5); return; }
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[38;5;%dm", palette[idx].term256);
    write(STDOUT_FILENO, seq, n);
#else
    (void)idx;
#endif
}

#if USE_ANSI_COLOR
static void set_bg_color_ansi(uint8_t idx){
    if (idx == EMPTY) {
        write(STDOUT_FILENO, "\x1b[49m", 5);
        return;
    }
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[48;5;%dm", palette[idx].term256);
    write(STDOUT_FILENO, seq, n);
}

static void reset_ansi_colors(void){
    write(STDOUT_FILENO, "\x1b[39m", 5);
    write(STDOUT_FILENO, "\x1b[49m", 5);
}
#else
static void set_bg_color_ansi(uint8_t idx){ (void)idx; }
static void reset_ansi_colors(void){}
#endif

static void draw_cell(uint8_t idx, int highlight){
    char ch;
    if (idx == EMPTY) {
        ch = '.';
        set_bg_color_ansi(EMPTY);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    } else if (idx < 26) {
        ch = palette[idx].letter;
        set_bg_color_ansi(idx);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    } else {
        ch = '?';
        set_bg_color_ansi(EMPTY);
#if USE_ANSI_COLOR
        write(STDOUT_FILENO, "\x1b[39m", 5);
#endif
    }

    if (highlight) {
        write(STDOUT_FILENO, "\x1b[7m", 4);
    }
    write(STDOUT_FILENO, &ch, 1);
    if (highlight) {
        write(STDOUT_FILENO, "\x1b[0m", 4);
    }
    reset_ansi_colors();
}

static void render(void){
    int rows, cols;
    get_terminal_size(&rows, &cols);
    if (rows < 5 || cols < 10) return;

    int draw_rows = rows - 2; // one for status, one for palette line
    int draw_cols = cols;

    // Clamp view to ensure cursor visible
    if (cursor_x < view_x) view_x = cursor_x;
    if (cursor_y < view_y) view_y = cursor_y;
    if (cursor_x >= view_x + draw_cols) view_x = cursor_x - draw_cols + 1;
    if (cursor_y >= view_y + draw_rows) view_y = cursor_y - draw_rows + 1;
    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;
    if (view_x > img_w - 1) view_x = img_w - 1;
    if (view_y > img_h - 1) view_y = img_h - 1;

    // Clear & move home
    write(STDOUT_FILENO, "\x1b[H", 3);

    // Palette line
    write(STDOUT_FILENO, " Palette: ", 10);
    for (int i=0;i<26;i++){
        set_color_ansi(i);
        char ch = palette[i].letter;
        write(STDOUT_FILENO, &ch, 1);
        write(STDOUT_FILENO, " ", 1);
    }
    reset_ansi_colors();
    // Fill to end
    int curcol = 10 + 2*26;
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
    // Status
    draw_status_line(cols_now);
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
    uint8_t *p = &pixels[cursor_y*img_w + cursor_x];
    if (*p == color_idx) return; // no-op
    push_change(cursor_x, cursor_y, *p, color_idx);
    *p = color_idx;
    dirty = 1;
}

/* -------- Main -------- */

int main(void){
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

        switch (key) {
            case KEY_UP:    if (cursor_y > 0) cursor_y--; break;
            case KEY_DOWN:  if (cursor_y < img_h-1) cursor_y++; break;
            case KEY_LEFT:  if (cursor_x > 0) cursor_x--; break;
            case KEY_RIGHT: if (cursor_x < img_w-1) cursor_x++; break;

            case KEY_BACKSPACE:
            case KEY_DELETE:
                paint_at_cursor(EMPTY);
                break;

            // Ctrl shortcuts
            case 19: /* Ctrl+S */ save_dialog(); break;
            case 15: /* Ctrl+O */ load_dialog(); break;
            case 14: /* Ctrl+N */ new_image_dialog(); break;
            case 26: /* Ctrl+Z */ undo_action(); break;
            case 25: /* Ctrl+Y */ redo_action(); break;
            case 17: /* Ctrl+Q */
                if (dirty){
                    char ans[8];
                    prompt("Unsaved changes. Save? (y/n) ", ans, sizeof(ans));
                    if (ans[0]=='y' || ans[0]=='Y') save_dialog();
                }
                running = 0; break;

            default:
                // Letters A-Z map to palette indices 0..25
                if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')){
                    char up = (char)toupper(key);
                    int idx = up - 'A';
                    if (idx >=0 && idx < 26){
                        paint_at_cursor((uint8_t)idx);
                    }
                }
                break;
        }
    }

    clear_screen();
    // Leave the cursor visible (atexit will also restore)
    return 0;
}
