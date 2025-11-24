#define _POSIX_C_SOURCE 200112L

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdint.h>

/* 
   The following prototypes are added because with _POSIX_C_SOURCE=200112L, 
   the functions dprintf, getline and strdup may not be declared.
*/
int dprintf(int fd, const char *format, ...);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
char *strdup(const char *s);

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT
};

/* Global variables for terminal state and dimensions */
struct termios orig_termios;
int g_term_rows, g_term_cols;
/* 
   g_content_width and g_content_height define the size (in characters) of the slide’s full area,
   i.e. the complete inner region (the terminal minus the border).
   g_content_offset_x and g_content_offset_y mark where this area begins.
*/
int g_content_width, g_content_height;
int g_content_offset_x, g_content_offset_y;

/* Global variable for help mode.
   When g_help_mode is 1, the help screen is being displayed.
*/
int g_help_mode = 0;

/* Global variables for slides */
typedef struct {
    char **lines;      // Array of g_content_height strings (each of length g_content_width)
    char **undo_lines; // Backup copy for undo in edit mode (or NULL if none)
} Slide;

Slide **g_slides = NULL;
int g_slide_count = 0;
int g_current_slide = 0;
const char *g_filename = NULL; // slides file name

/* Global state variables for modes */
int g_edit_mode = 0;  // 0 = presentation mode; 1 = edit mode
int g_quit = 0;       // flag to indicate quitting

/* Global variables to store the editing cursor position between pages */
int g_last_edit_row = 0;
int g_last_edit_col = 0;

/*
   Global clipboard structure for rectangular selection copy/paste.
   When a region is copied (or cut), its contents (rows and cols)
   are stored here and can later be pasted (with CTRL+V) into any slide in edit mode.
*/
typedef struct {
    int rows;
    int cols;
    char **data;
} Clipboard;

Clipboard *g_clipboard = NULL;

static void freeClipboard(void) {
    if (!g_clipboard) return;
    for (int i = 0; i < g_clipboard->rows; i++)
        free(g_clipboard->data[i]);
    free(g_clipboard->data);
    free(g_clipboard);
    g_clipboard = NULL;
}

static const struct {
    unsigned char byte;
    int codepoint;
} g_box_draw_map[] = {
    {0xda, 0x250c}, /* ┌ */
    {0xbf, 0x2510}, /* ┐ */
    {0xc0, 0x2514}, /* └ */
    {0xd9, 0x2518}, /* ┘ */
    {0xc4, 0x2500}, /* ─ */
    {0xb3, 0x2502}, /* │ */
    {0xc3, 0x251c}, /* ├ */
    {0xb4, 0x2524}, /* ┤ */
    {0xc2, 0x252c}, /* ┬ */
    {0xc1, 0x2534}, /* ┴ */
    {0xc5, 0x253c}, /* ┼ */
};

static int isDrawableChar(int ch) {
    return (unsigned int)ch >= 32 && (unsigned int)ch <= 255;
}

static unsigned char glyphFromCodepoint(int cp) {
    if (cp >= 0 && cp <= 255)
        return (unsigned char)cp;
    for (size_t i = 0; i < sizeof(g_box_draw_map) / sizeof(g_box_draw_map[0]); i++) {
        if (g_box_draw_map[i].codepoint == cp)
            return g_box_draw_map[i].byte;
    }
    return '?';
}

static int codepointFromGlyph(unsigned char glyph) {
    for (size_t i = 0; i < sizeof(g_box_draw_map) / sizeof(g_box_draw_map[0]); i++) {
        if (g_box_draw_map[i].byte == glyph)
            return g_box_draw_map[i].codepoint;
    }
    return (int)glyph;
}

static int decodeUtf8Char(const char *s, size_t len, size_t *adv) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *adv = 1;
        return c;
    }
    if ((c & 0xe0) == 0xc0 && len >= 2) {
        unsigned char c1 = (unsigned char)s[1];
        if ((c1 & 0xc0) == 0x80) {
            *adv = 2;
            return ((c & 0x1f) << 6) | (c1 & 0x3f);
        }
    }
    if ((c & 0xf0) == 0xe0 && len >= 3) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        if ((c1 & 0xc0) == 0x80 && (c2 & 0xc0) == 0x80) {
            *adv = 3;
            return ((c & 0x0f) << 12) | ((c1 & 0x3f) << 6) | (c2 & 0x3f);
        }
    }
    if ((c & 0xf8) == 0xf0 && len >= 4) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        unsigned char c3 = (unsigned char)s[3];
        if ((c1 & 0xc0) == 0x80 && (c2 & 0xc0) == 0x80 && (c3 & 0xc0) == 0x80) {
            *adv = 4;
            return ((c & 0x07) << 18) | ((c1 & 0x3f) << 12) | ((c2 & 0x3f) << 6) | (c3 & 0x3f);
        }
    }
    *adv = 1;
    return -1;
}

static int encodeUtf8Char(int cp, char *out) {
    if (cp < 0)
        return 0;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xc0 | ((cp >> 6) & 0x1f));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xe0 | ((cp >> 12) & 0x0f));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    } else if (cp <= 0x10ffff) {
        out[0] = (char)(0xf0 | ((cp >> 18) & 0x07));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[3] = (char)(0x80 | (cp & 0x3f));
        return 4;
    }
    return 0;
}

static char *convertUtf8ToGlyphBytes(const char *text) {
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    size_t in_pos = 0, out_pos = 0;
    while (in_pos < len) {
        size_t adv = 1;
        int cp = decodeUtf8Char(text + in_pos, len - in_pos, &adv);
        if (cp < 0)
            cp = (unsigned char)text[in_pos];
        out[out_pos++] = (char)glyphFromCodepoint(cp);
        in_pos += adv;
    }
    out[out_pos] = '\0';
    return out;
}

static char *convertGlyphBytesToUtf8(const Clipboard *clip, size_t *utf8_len_out) {
    if (!clip || clip->rows <= 0 || clip->cols <= 0)
        return NULL;

    size_t max_len = (size_t)clip->rows * (size_t)clip->cols * 4 + (size_t)clip->rows + 1;
    char *buf = malloc(max_len);
    if (!buf)
        return NULL;

    size_t pos = 0;
    for (int r = 0; r < clip->rows; r++) {
        for (int c = 0; c < clip->cols; c++) {
            int cp = codepointFromGlyph((unsigned char)clip->data[r][c]);
            char tmp[4];
            int w = encodeUtf8Char(cp, tmp);
            if (pos + (size_t)w >= max_len) {
                free(buf);
                return NULL;
            }
            memcpy(buf + pos, tmp, (size_t)w);
            pos += (size_t)w;
        }
        if (r < clip->rows - 1) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    if (utf8_len_out)
        *utf8_len_out = pos;
    return buf;
}

static int systemClipboardWrite(const Clipboard *clip) {
    if (!clip || clip->rows <= 0 || clip->cols <= 0)
        return -1;

    size_t utf8_len = 0;
    char *utf8_buf = convertGlyphBytesToUtf8(clip, &utf8_len);
    if (!utf8_buf)
        return -1;

    FILE *fp = popen("xclip -selection clipboard", "w");
    if (!fp) {
        free(utf8_buf);
        return -1;
    }
    (void)fwrite(utf8_buf, 1, utf8_len, fp);
    pclose(fp);
    free(utf8_buf);
    return 0;
}

static char *systemClipboardRead(void) {
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!fp)
        return NULL;

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }

    char chunk[256];
    size_t nread;
    while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + nread + 1 > cap) {
            size_t new_cap = cap * 2;
            while (len + nread + 1 > new_cap)
                new_cap *= 2;
            char *tmp = realloc(buf, new_cap);
            if (!tmp) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, nread);
        len += nread;
    }
    pclose(fp);

    if (len == 0) {
        free(buf);
        return NULL;
    }

    char *tmp = realloc(buf, len + 1);
    if (!tmp) {
        free(buf);
        return NULL;
    }
    buf = tmp;
    buf[len] = '\0';
    return buf;
}

static Clipboard *clipboardFromText(const char *text) {
    if (!text || text[0] == '\0')
        return NULL;

    char *glyph_text = convertUtf8ToGlyphBytes(text);
    if (!glyph_text)
        return NULL;

    int lines_cap = 8;
    int lines_count = 0;
    char **lines = malloc(sizeof(char*) * lines_cap);
    if (!lines) {
        free(glyph_text);
        return NULL;
    }

    size_t max_cols = 0;
    const char *line_start = glyph_text;
    const char *p = glyph_text;
    while (1) {
        if (*p == '\n' || *p == '\0') {
            size_t line_len = (size_t)(p - line_start);
            if (line_len > 0 && line_start[line_len - 1] == '\r')
                line_len--;
            if (lines_count == lines_cap) {
                lines_cap *= 2;
                char **tmp = realloc(lines, sizeof(char*) * lines_cap);
                if (!tmp) {
                    for (int i = 0; i < lines_count; i++)
                        free(lines[i]);
                    free(lines);
                    free(glyph_text);
                    return NULL;
                }
                lines = tmp;
            }
            lines[lines_count] = malloc(line_len + 1);
            if (!lines[lines_count]) {
                for (int i = 0; i < lines_count; i++)
                    free(lines[i]);
                free(lines);
                free(glyph_text);
                return NULL;
            }
            memcpy(lines[lines_count], line_start, line_len);
            lines[lines_count][line_len] = '\0';
            if (line_len > max_cols)
                max_cols = line_len;
            lines_count++;
            if (*p == '\0')
                break;
            line_start = p + 1;
        }
        p++;
    }

    free(glyph_text);

    if (lines_count == 0) {
        free(lines);
        return NULL;
    }

    if (max_cols == 0)
        max_cols = 1;

    int cols = (int)max_cols;
    if (cols > g_content_width)
        cols = g_content_width;

    int rows = lines_count;
    if (rows > g_content_height)
        rows = g_content_height;

    Clipboard *clip = malloc(sizeof(Clipboard));
    if (!clip) {
        for (int i = 0; i < lines_count; i++)
            free(lines[i]);
        free(lines);
        return NULL;
    }
    clip->rows = rows;
    clip->cols = cols;
    clip->data = malloc(sizeof(char*) * rows);
    if (!clip->data) {
        for (int i = 0; i < lines_count; i++)
            free(lines[i]);
        free(lines);
        free(clip);
        return NULL;
    }

    for (int i = 0; i < rows; i++) {
        clip->data[i] = malloc((size_t)cols + 1);
        if (!clip->data[i]) {
            for (int j = 0; j < i; j++)
                free(clip->data[j]);
            free(clip->data);
            for (int j = 0; j < lines_count; j++)
                free(lines[j]);
            free(lines);
            free(clip);
            return NULL;
        }
        size_t copy_len = strlen(lines[i]);
        if (copy_len > (size_t)cols)
            copy_len = (size_t)cols;
        memcpy(clip->data[i], lines[i], copy_len);
        if ((int)copy_len < cols)
            memset(clip->data[i] + copy_len, ' ', (size_t)(cols - (int)copy_len));
        clip->data[i][cols] = '\0';
    }

    for (int i = 0; i < lines_count; i++)
        free(lines[i]);
    free(lines);

    return clip;
}

static int syncClipboardFromSystem(void) {
    char *text = systemClipboardRead();
    if (!text)
        return 0;
    Clipboard *clip = clipboardFromText(text);
    free(text);
    if (!clip)
        return 0;
    freeClipboard();
    g_clipboard = clip;
    return 1;
}

static void copyRegionToClipboard(int sel_row_start, int sel_col_start, int sel_rows, int sel_cols, int cut_region) {
    freeClipboard();
    g_clipboard = malloc(sizeof(Clipboard));
    if (!g_clipboard)
        return;
    g_clipboard->rows = sel_rows;
    g_clipboard->cols = sel_cols;
    g_clipboard->data = malloc(sizeof(char*) * sel_rows);
    if (!g_clipboard->data) {
        free(g_clipboard);
        g_clipboard = NULL;
        return;
    }
    for (int i = 0; i < sel_rows; i++) {
        g_clipboard->data[i] = malloc(sel_cols + 1);
        if (!g_clipboard->data[i]) {
            for (int j = 0; j < i; j++)
                free(g_clipboard->data[j]);
            free(g_clipboard->data);
            free(g_clipboard);
            g_clipboard = NULL;
            return;
        }
        strncpy(g_clipboard->data[i],
                g_slides[g_current_slide]->lines[sel_row_start + i] + sel_col_start,
                (size_t)sel_cols);
        g_clipboard->data[i][sel_cols] = '\0';
    }
    if (cut_region) {
        for (int i = 0; i < sel_rows; i++) {
            for (int j = 0; j < sel_cols; j++)
                g_slides[g_current_slide]->lines[sel_row_start + i][sel_col_start + j] = ' ';
        }
    }
    systemClipboardWrite(g_clipboard);
}

/************************************
 * Terminal raw mode helper functions
 ************************************/

/* Disable raw mode and restore original terminal settings */
void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Enable raw mode (disables canonical mode, echo, etc.) */
void enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/* Read a key from STDIN (handles escape sequences for arrow keys) */
int readKey(void) {
    char c;
    ssize_t nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        /* if interrupted by signal, try again */
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

/************************************
 * Terminal size and dimensions setup
 ************************************/

/* Get terminal window size */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/*
  Compute dimensions for the program.
  Set the full inner region (editing area and presentation area) to be the whole terminal minus the border.
*/
void computeDimensions(void) {
    if (getWindowSize(&g_term_rows, &g_term_cols) == -1) {
        perror("getWindowSize");
        exit(1);
    }
    int full_width = g_term_cols - 2;   // account for left/right borders
    int full_height = g_term_rows - 2;  // account for top/bottom borders
    g_content_width = full_width;
    g_content_height = full_height;
    g_content_offset_x = 2;
    g_content_offset_y = 2;
}

/************************************
 * Drawing functions (using ANSI escape codes)
 ************************************/

/* Clear the screen and reposition the cursor at the top–left */
void clearScreen(void) {
    if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) {
        perror("write");
    }
    if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) {
        perror("write");
    }
}

/* Draw the outer border covering the whole terminal */
void drawBorder(void) {
    int r, c;
    /* Draw the top border */
    if (write(STDOUT_FILENO, "+", 1) < 0) perror("write");
    for (c = 2; c < g_term_cols; c++) {
        if (write(STDOUT_FILENO, "-", 1) < 0) perror("write");
    }
    if (write(STDOUT_FILENO, "+", 1) < 0) perror("write");

    /* Draw the sides for rows 2 .. g_term_rows-1 */
    char line[g_term_cols + 1];
    for (r = 2; r < g_term_rows; r++) {
        line[0] = '|';
        memset(&line[1], ' ', g_term_cols - 2);
        line[g_term_cols - 1] = '|';
        line[g_term_cols] = '\0';
        dprintf(STDOUT_FILENO, "\x1b[%d;1H%s", r, line);
    }

    /* Draw the bottom border */
    dprintf(STDOUT_FILENO, "\x1b[%d;1H+", g_term_rows);
    for (c = 2; c < g_term_cols; c++) {
        if (write(STDOUT_FILENO, "-", 1) < 0) perror("write");
    }
    if (write(STDOUT_FILENO, "+", 1) < 0) perror("write");
}

/*
 * Draw the full slide content.
 * In both presentation and edit modes, the slide buffer covers the entire inner region.
 */
void drawFullSlideContent(Slide *slide) {
    for (int i = 0; i < g_content_height; i++) {
        dprintf(STDOUT_FILENO, "\x1b[%d;%dH%-*s", 
            g_content_offset_y + i, 
            g_content_offset_x, 
            g_content_width, 
            slide->lines[i]);
    }
}

/* Draw the slide indicator (e.g., "2/5") at bottom–right inside the border */
void drawSlideIndicator(void) {
    char indicator[32];
    snprintf(indicator, sizeof(indicator), "%d/%d", g_current_slide + 1, g_slide_count);
    int len = strlen(indicator);
    int row = g_term_rows;
    int col = g_term_cols - len; // within bottom border
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH%s", row, col, indicator);
}

/* Draw the centered mode banner on row 2 */
void drawModeBanner(void) {
    const char *banner = g_edit_mode ? "EDIT MODE" : "PRESENTATION MODE";
    int len = strlen(banner);
    int col = (g_term_cols - len) / 2 + 1;
    dprintf(STDOUT_FILENO, "\x1b[2;%dH%s", col, banner);
}

/* Draw the cursor position (in edit mode) at the bottom center */
void drawEditStatus(int cur_row, int cur_col) {
    char pos[32];
    snprintf(pos, sizeof(pos), "X:%d Y:%d", cur_col, cur_row);
    int len = strlen(pos);
    int pos_col = (g_term_cols - len) / 2 + 1;
    /* Print the coordinate on the bottom border line, overlaying part of it */
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH%s", g_term_rows, pos_col, pos);
}

/* Overlay the currently selected rectangular region (in toggle mode) using inverse video */
void drawToggleOverlay(int start_row, int start_col, int curr_row, int curr_col) {
    int row1 = (start_row < curr_row ? start_row : curr_row);
    int row2 = (start_row > curr_row ? start_row : curr_row);
    int col1 = (start_col < curr_col ? start_col : curr_col);
    int col2 = (start_col > curr_col ? start_col : curr_col);
    for (int r = row1; r <= row2; r++) {
        for (int c = col1; c <= col2; c++) {
            unsigned char ch = (unsigned char)g_slides[g_current_slide]->lines[r][c];
            dprintf(STDOUT_FILENO, "\x1b[%d;%dH\x1b[7m%c\x1b[0m", g_content_offset_y + r, g_content_offset_x + c, ch);
        }
    }
}

/* Refresh the screen in presentation mode: draw border, full slide content, slide indicator, and mode banner. */
void refreshPresentationScreen(void) {
    clearScreen();
    drawBorder();
    drawFullSlideContent(g_slides[g_current_slide]);
    drawSlideIndicator();
    drawModeBanner();
    /* Move the cursor out of the way */
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH", g_term_rows, g_term_cols);
}

/* Refresh the screen in edit mode and position the editing cursor.
   Here the entire editable area (full inner region) is drawn.
   Additionally, the editing cursor coordinates are shown at the bottom center.
*/
void refreshEditScreen(int cur_row, int cur_col) {
    clearScreen();
    drawBorder();
    drawFullSlideContent(g_slides[g_current_slide]);
    drawModeBanner();
    drawEditStatus(cur_row, cur_col);
    /* Position the cursor in the editing area */
    int term_row = g_content_offset_y + cur_row;
    int term_col = g_content_offset_x + cur_col;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH", term_row, term_col);
}

/************************************
 * Help screen functionality.
 ************************************/

/* Display help information on the screen */
void displayHelp(void) {
    clearScreen();
    drawBorder();
    int row = 4, col = 4;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHHELP MENU - Shortcuts and Instructions", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH--------------------------------------", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+E : Toggle between Presentation and Edit mode", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+Q : Quit the app", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+S : Save slides (in Edit mode)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+Z : Undo changes (in Edit mode)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHARROW KEYS : Navigate slides (Presentation) or editing cursor (Edit)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+N : Add a new slide (after current slide)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+D : Delete the current slide (except first slide)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+H : Toggle Help Menu", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+T : Toggle rectangular selection mode (in Edit mode)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+C : Copy selected region (slides + system clipboard)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+X : Cut selected region (slides + system clipboard)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+V : Paste from slides/system clipboard (in Edit mode)", row, col);
    row += 2;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHPress CTRL+H again to return.", row, col);
}

/* Enter help mode: display the help screen until CTRL+H is pressed again */
void enterHelpMode(void) {
    g_help_mode = 1;
    while (g_help_mode) {
        displayHelp();
        int ch = readKey();
        if (ch == CTRL_KEY('H')) {
            g_help_mode = 0;
        }
    }
    clearScreen();
}

/************************************
 * Slide file load/save functions
 ************************************/

/* Allocate and initialize a new blank slide */
Slide *newBlankSlide(void) {
    int i;
    Slide *s = malloc(sizeof(Slide));
    s->lines = malloc(sizeof(char*) * g_content_height);
    s->undo_lines = NULL;
    for (i = 0; i < g_content_height; i++) {
        s->lines[i] = malloc(g_content_width + 1);
        memset(s->lines[i], ' ', g_content_width);
        s->lines[i][g_content_width] = '\0';
    }
    return s;
}

/* Free a slide from memory */
void freeSlide(Slide *s) {
    int i;
    for (i = 0; i < g_content_height; i++) {
        free(s->lines[i]);
    }
    free(s->lines);
    if (s->undo_lines) {
        for (i = 0; i < g_content_height; i++) {
            free(s->undo_lines[i]);
        }
        free(s->undo_lines);
    }
    free(s);
}

/* Load slides from file */
void loadSlides(const char *filename) {
    FILE *fp = fopen(filename, "r");
    Slide **slides = NULL;
    int slideCount = 0;
    if (!fp) {
        g_slide_count = 1;
        g_slides = malloc(sizeof(Slide *));
        g_slides[0] = newBlankSlide();
        return;
    }
    
    char *line = NULL;
    size_t len = 0;
    char *buffer[g_content_height];
    int bufCount = 0;
    while (getline(&line, &len, fp) != -1) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strcmp(line, "----") == 0) {
            Slide *s = malloc(sizeof(Slide));
            s->lines = malloc(sizeof(char*) * g_content_height);
            s->undo_lines = NULL;
            for (int i = 0; i < g_content_height; i++) {
                s->lines[i] = malloc(g_content_width + 1);
                if (i < bufCount) {
                    int copyLen = strlen(buffer[i]);
                    if (copyLen > g_content_width) copyLen = g_content_width;
                    memcpy(s->lines[i], buffer[i], copyLen);
                    if (copyLen < g_content_width) {
                        memset(s->lines[i] + copyLen, ' ', g_content_width - copyLen);
                    }
                } else {
                    memset(s->lines[i], ' ', g_content_width);
                }
                s->lines[i][g_content_width] = '\0';
            }
            for (int i = 0; i < bufCount; i++) free(buffer[i]);
            bufCount = 0;
            slides = realloc(slides, sizeof(Slide*) * (slideCount + 1));
            slides[slideCount++] = s;
        } else {
            if (bufCount < g_content_height)
                buffer[bufCount++] = strdup(line);
        }
    }
    free(line);
    fclose(fp);
    
    if (bufCount > 0) {
        Slide *s = malloc(sizeof(Slide));
        s->lines = malloc(sizeof(char*) * g_content_height);
        s->undo_lines = NULL;
        for (int i = 0; i < g_content_height; i++) {
            s->lines[i] = malloc(g_content_width + 1);
            if (i < bufCount) {
                int copyLen = strlen(buffer[i]);
                if (copyLen > g_content_width) copyLen = g_content_width;
                memcpy(s->lines[i], buffer[i], copyLen);
                if (copyLen < g_content_width) {
                    memset(s->lines[i] + copyLen, ' ', g_content_width - copyLen);
                }
            } else {
                memset(s->lines[i], ' ', g_content_width);
            }
            s->lines[i][g_content_width] = '\0';
        }
        for (int i = 0; i < bufCount; i++) free(buffer[i]);
        slides = realloc(slides, sizeof(Slide*) * (slideCount + 1));
        slides[slideCount++] = s;
    }
    if (slideCount == 0) {
        slideCount = 1;
        slides = malloc(sizeof(Slide*));
        slides[0] = newBlankSlide();
    }
    g_slides = slides;
    g_slide_count = slideCount;
}

/* Save all slides to the file */
void saveSlides(void) {
    FILE *fp = fopen(g_filename, "w");
    if (!fp) return;
    for (int s = 0; s < g_slide_count; s++) {
        for (int i = 0; i < g_content_height; i++) {
            fprintf(fp, "%.*s\n", g_content_width, g_slides[s]->lines[i]);
        }
        if (s < g_slide_count - 1)
            fprintf(fp, "----\n");
    }
    fclose(fp);
}

/************************************
 * Edit mode functionality.
 ************************************/
void enterEditMode(void) {
    g_edit_mode = 1;
    Slide *slide = g_slides[g_current_slide];
    int cur_row = g_last_edit_row;
    int cur_col = g_last_edit_col;
    int ch, i;
    
    /* Backup for undo */
    if (slide->undo_lines) {
        for (i = 0; i < g_content_height; i++)
            free(slide->undo_lines[i]);
        free(slide->undo_lines);
    }
    slide->undo_lines = malloc(sizeof(char*) * g_content_height);
    for (i = 0; i < g_content_height; i++)
        slide->undo_lines[i] = strdup(slide->lines[i]);
    
    while (1) {
        refreshEditScreen(cur_row, cur_col);
        ch = readKey();
        if (ch == CTRL_KEY('S')) {
            saveSlides();
            dprintf(STDOUT_FILENO, "\x1b[%d;2HSlideset saved!", g_term_rows - 1);
            fsync(STDOUT_FILENO);
            sleep(3);
        } else if (ch == CTRL_KEY('Z')) {
            for (i = 0; i < g_content_height; i++)
                strncpy(slide->lines[i], slide->undo_lines[i], g_content_width);
        } else if (ch == CTRL_KEY('E') || ch == 27) {
            break;
        } else if (ch == CTRL_KEY('Q')) {
            g_quit = 1;
            break;
        } else if (ch == ARROW_UP) {
            if (cur_row > 0) cur_row--;
        } else if (ch == ARROW_DOWN) {
            if (cur_row < g_content_height - 1) cur_row++;
        } else if (ch == ARROW_LEFT) {
            if (cur_col > 0) cur_col--;
        } else if (ch == ARROW_RIGHT) {
            if (cur_col < g_content_width - 1) cur_col++;
        } else if (ch == ' ') {
            // Insert space at cursor: shift rest of line right
            char *line = slide->lines[cur_row];
            if (cur_col < g_content_width - 1) {
                for (i = g_content_width - 1; i > cur_col; i--) {
                    line[i] = line[i - 1];
                }
                line[cur_col] = ' ';
                cur_col++;
            }
        } else if (ch == 127 || ch == CTRL_KEY('H')) {
            // Backspace: delete preceding char (shift rest of line left)
            char *line = slide->lines[cur_row];
            if (cur_col > 0) {
                cur_col--;
                for (i = cur_col; i < g_content_width - 1; i++) {
                    line[i] = line[i + 1];
                }
                line[g_content_width - 1] = ' ';
            } else if (cur_row > 0) {
                cur_row--;
                cur_col = g_content_width - 1;
                line = slide->lines[cur_row];
                for (i = cur_col; i < g_content_width - 1; i++) {
                    line[i] = line[i + 1];
                }
                line[g_content_width - 1] = ' ';
            }
        } else if (isDrawableChar(ch)) {
            slide->lines[cur_row][cur_col] = (char)ch;
            if (cur_col < g_content_width - 1)
                cur_col++;
            else if (cur_row < g_content_height - 1) {
                cur_col = 0;
                cur_row++;
            }
        } else if (ch == CTRL_KEY('T')) {
            int toggle_start_row = cur_row, toggle_start_col = cur_col;
            int toggle_row = cur_row, toggle_col = cur_col;
            int toggle_ch;
            while (1) {
                refreshEditScreen(toggle_row, toggle_col);
                drawToggleOverlay(toggle_start_row, toggle_start_col, toggle_row, toggle_col);
                toggle_ch = readKey();
                if (toggle_ch == CTRL_KEY('T')) {
                    cur_row = toggle_row;
                    cur_col = toggle_col;
                    break;
                } else if (toggle_ch == CTRL_KEY('X')) {
                    int sel_row_start = (toggle_start_row < toggle_row ? toggle_start_row : toggle_row);
                    int sel_row_end   = (toggle_start_row > toggle_row ? toggle_start_row : toggle_row);
                    int sel_col_start = (toggle_start_col < toggle_col ? toggle_start_col : toggle_col);
                    int sel_col_end   = (toggle_start_col > toggle_col ? toggle_start_col : toggle_col);
                    int sel_rows = sel_row_end - sel_row_start + 1;
                    int sel_cols = sel_col_end - sel_col_start + 1;
                    copyRegionToClipboard(sel_row_start, sel_col_start, sel_rows, sel_cols, 1);
                    if (g_clipboard) {
                        dprintf(STDOUT_FILENO, "\x1b[%d;2HRegion cut!", g_term_rows - 1);
                        fsync(STDOUT_FILENO);
                        sleep(1);
                    }
                    cur_row = toggle_row;
                    cur_col = toggle_col;
                    break;
                } else if (toggle_ch == CTRL_KEY('C')) {
                    int sel_row_start = (toggle_start_row < toggle_row ? toggle_start_row : toggle_row);
                    int sel_row_end   = (toggle_start_row > toggle_row ? toggle_start_row : toggle_row);
                    int sel_col_start = (toggle_start_col < toggle_col ? toggle_start_col : toggle_col);
                    int sel_col_end   = (toggle_start_col > toggle_col ? toggle_start_col : toggle_col);
                    int sel_rows = sel_row_end - sel_row_start + 1;
                    int sel_cols = sel_col_end - sel_col_start + 1;
                    copyRegionToClipboard(sel_row_start, sel_col_start, sel_rows, sel_cols, 0);
                    if (g_clipboard) {
                        dprintf(STDOUT_FILENO, "\x1b[%d;2HRegion copied!", g_term_rows - 1);
                        fsync(STDOUT_FILENO);
                        sleep(1);
                    }
                } else if (toggle_ch == ARROW_UP) {
                    if (toggle_row > 0) toggle_row--;
                } else if (toggle_ch == ARROW_DOWN) {
                    if (toggle_row < g_content_height - 1) toggle_row++;
                } else if (toggle_ch == ARROW_LEFT) {
                    if (toggle_col > 0) toggle_col--;
                } else if (toggle_ch == ARROW_RIGHT) {
                    if (toggle_col < g_content_width - 1) toggle_col++;
                } else if (toggle_ch == CTRL_KEY('Q')) {
                    g_quit = 1;
                    return;
                }
            }
        } else if (ch == CTRL_KEY('V')) {
            syncClipboardFromSystem();
            if (g_clipboard) {
                int r, c;
                for (r = 0; r < g_clipboard->rows; r++) {
                    if (cur_row + r >= g_content_height) break;
                    for (c = 0; c < g_clipboard->cols; c++) {
                        if (cur_col + c >= g_content_width) break;
                        slide->lines[cur_row + r][cur_col + c] = g_clipboard->data[r][c];
                    }
                }
            }
        }
    }
    
    g_last_edit_row = cur_row;
    g_last_edit_col = cur_col;
    for (i = 0; i < g_content_height; i++)
        free(slide->undo_lines[i]);
    free(slide->undo_lines);
    slide->undo_lines = NULL;
    clearScreen();
    g_edit_mode = 0;
}

/************************************
 * Main presentation loop
 ************************************/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s slides_file\n", argv[0]);
        exit(1);
    }
    g_filename = argv[1];
    computeDimensions();
    loadSlides(g_filename);
    enableRawMode();
    
    while (!g_quit) {
        if (!g_edit_mode)
            refreshPresentationScreen();
        
        int c = readKey();
        if (!g_edit_mode && c == CTRL_KEY('H')) {
            enterHelpMode();
            continue;
        }
        if (c == CTRL_KEY('Q')) {
            g_quit = 1;
            break;
        } else if (c == CTRL_KEY('E')) {
            enterEditMode();
            if (g_quit) break;
        } else if (c == ARROW_RIGHT) {
            if (g_current_slide < g_slide_count - 1)
                g_current_slide++;
        } else if (c == ARROW_LEFT) {
            if (g_current_slide > 0)
                g_current_slide--;
        } else if (c == CTRL_KEY('N')) {
            Slide *new_slide = newBlankSlide();
            g_slides = realloc(g_slides, sizeof(Slide*) * (g_slide_count + 1));
            for (int i = g_slide_count; i > g_current_slide + 1; i--) {
                g_slides[i] = g_slides[i - 1];
            }
            g_slides[g_current_slide + 1] = new_slide;
            g_slide_count++;
            g_current_slide++;
        } else if (c == CTRL_KEY('D')) {
            if (g_current_slide > 0) {
                freeSlide(g_slides[g_current_slide]);
                for (int i = g_current_slide; i < g_slide_count - 1; i++) {
                    g_slides[i] = g_slides[i + 1];
                }
                g_slide_count--;
                g_slides = realloc(g_slides, sizeof(Slide*) * g_slide_count);
                if (g_current_slide >= g_slide_count)
                    g_current_slide = g_slide_count - 1;
            }
        }
    }
    
    clearScreen();
    disableRawMode();
    for (int i = 0; i < g_slide_count; i++)
        freeSlide(g_slides[i]);
    free(g_slides);
    return 0;
}
