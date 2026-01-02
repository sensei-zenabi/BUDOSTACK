#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "../lib/terminal_layout.h"

#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

/*
 * Design principles and notes:
 * - Plain C using -std=c11 and standard C libraries with POSIX-compliant functions.
 * - No separate header files; full code is in one file.
 * - Append Buffer Implementation: Instead of multiple write() calls,
 *   output is accumulated in a dynamic buffer (struct abuf) and then flushed with one write() call.
 * - TAB Support: The TAB key now inserts two spaces into the text.
 * - Added a case-insensitive search helper to support CTRL+F searches that ignore case.
 */

/*** Append Buffer Implementation ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new_b = realloc(ab->b, ab->len + len);
    if (new_b == NULL)
        return;
    memcpy(&new_b[ab->len], s, len);
    ab->b = new_b;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** Editor Definitions ***/
#define EDITOR_VERSION "0.1-micro-like"
#define EDITOR_TAB_WIDTH 2

#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127
#define DEL_KEY 1004  // Key code for Delete

/* Prototype for the syntax highlighter from libedit.c */
char *highlight_c_line(const char *line, int hl_in_comment);
char *highlight_other_line(const char *line);
int libedit_is_plain_text(const char *filename);

/* Enumeration for editor keys.
   New keys added:
     HOME_KEY: Home key (beginning of line).
     END_KEY: End key (end of line).
     PGUP_KEY: Page Up key.
     PGDN_KEY: Page Down key.
*/
enum EditorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,    // 1001
    ARROW_UP,       // 1002
    ARROW_DOWN,     // 1003
    HOME_KEY = 1005, // explicitly set to 1005
    END_KEY,        // 1006
    PGUP_KEY,       // 1007
    PGDN_KEY        // 1008
};

/* Data structure for a text line */
typedef struct {
    int size;
    char *chars;
    int modified;
    int hl_in_comment;  // new field to store multi-line comment state for syntax highlighting
} EditorLine;

/* Global clipboard for cut/copy/paste functionality */
char *clipboard = NULL;
size_t clipboard_len = 0;

/* Global editor state */
struct EditorConfig {
    int cx, cy;           // cursor position
    int screenrows;       // terminal rows
    int screencols;       // terminal columns
    int rowoff;           // vertical scroll offset
    int coloff;           // horizontal scroll offset
    int numrows;          // number of rows in the file
    EditorLine *row;      // array of rows
    char *filename;
    int dirty;            // unsaved changes flag
    struct termios orig_termios; // original terminal settings

    char status_message[80];
    int textrows;         // text area rows (screenrows - 3 because of three bars)

    /* Selection fields:
       CTRL+T toggles selection mode.
       When active, the selection anchor is stored in sel_anchor_x, sel_anchor_y;
       the current cursor is the selection end.
    */
    int selecting;
    int sel_anchor_x;
    int sel_anchor_y;

    /* Preferred horizontal (desired) column */
    int preferred_cx;
    int syntax_dirty;
} E;

/* Undo state structure */
typedef struct {
    int cx, cy;
    int numrows;
    EditorLine *row;
    int *modified;
} UndoState;

UndoState *undo_history[100];
int undo_history_len = 0;

/* Global flag to control auto-indent.
   When set to 1 (default), pressing Enter auto-indents the new line.
   When disabled (e.g. during paste), newlines are inserted verbatim.
*/
static int auto_indent_enabled = 1;
/* Global flag to indicate bracketed paste mode.
   When true, auto-indent is temporarily disabled.
*/
static int in_paste_mode = 0;

/*** New Helper Function for Case-Insensitive Search ***/
/*
 * strcasestr_custom:
 *   A simple implementation of a case-insensitive substring search.
 *   It returns a pointer to the first occurrence of needle in haystack ignoring case,
 *   or NULL if not found.
 */
char *strcasestr_custom(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n)
            return (char *)haystack;
    }
    return NULL;
}

/* Expand tabs in the input string to spaces based on a fixed tab size.
   Returns a newly allocated string with TAB characters replaced by the proper
   number of space characters.
*/
char *expand_tabs(const char *s) {
    int tab_size = EDITOR_TAB_WIDTH;
    int col = 0;
    int new_len = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\t') {
            int spaces = tab_size - (col % tab_size);
            new_len += spaces;
            col += spaces;
        } else {
            new_len++;
            col++;
        }
    }
    char *result = malloc(new_len + 1);
    if (!result)
        return NULL;
    col = 0;
    int idx = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\t') {
            int spaces = tab_size - (col % tab_size);
            for (int i = 0; i < spaces; i++) {
                result[idx++] = ' ';
                col++;
            }
        } else {
            result[idx++] = *p;
            col++;
        }
    }
    result[idx] = '\0';
    return result;
}

/* --- Function Prototypes --- */
void die(const char *s);
void disableRawMode(void);
void enableRawMode(void);
int editorReadKey(void);
int getWindowSize(int *rows, int *cols);
void editorRefreshScreen(void);
int getRowNumWidth(void);
int editorDisplayWidth(const char *s);
int editorRowCxToByteIndex(EditorLine *row, int cx);
int editorRowByteIndexToCx(EditorLine *row, int byte_index);
void editorRenderRow(EditorLine *row, int avail, struct abuf *ab);
void editorRenderRowWithSelection(EditorLine *row, int file_row, int avail, struct abuf *ab);
void abAppendHighlighted(struct abuf *ab, const char *s, int coloff, int avail);
void editorDrawRows(struct abuf *ab, int rn_width);
void editorDrawTopBar(struct abuf *ab); 
void editorDrawStatusBar(struct abuf *ab);
void editorDrawShortcutBar(struct abuf *ab);
void editorProcessKeypress(void);
void editorOpen(const char *filename);
void editorSave(void);
void editorAppendLine(char *s, size_t len);
void editorInsertChar(int c);
void editorInsertUTF8(const char *s, int len);
void editorInsertNewline(void);
void editorDelChar(void);
void push_undo_state(void);
void free_undo_state(UndoState *state);
void pop_undo_state(void);
void editorCopySelection(void);
void editorCutSelection(void);
void editorPasteClipboard(void);
void editorInsertString(const char *s);
void systemClipboardWrite(const char *s);
char *systemClipboardRead(void);

/* New function prototypes */
void editorDeleteSelection(void);
void editorDelCharAtCursor(void);
void editorSearch(void);
void editorReplace(void);

/* --- New Function: update_syntax ---
   Updates each line’s multi-line comment state.
   This simple state machine (which does not consider string/char literals) scans
   through all lines so that the highlighter can know whether a line starts in a comment.
*/
void update_syntax(void) {
    int in_comment = 0;
    for (int i = 0; i < E.numrows; i++) {
        E.row[i].hl_in_comment = in_comment;
        char *line = E.row[i].chars;
        int j = 0;
        // Simple scan for multi-line comment delimiters (ignoring strings/chars)
        while (j < E.row[i].size) {
            if (!in_comment && j + 1 < E.row[i].size && line[j] == '/' && line[j + 1] == '*') {
                in_comment = 1;
                j += 2;
                continue;
            }
            if (in_comment && j + 1 < E.row[i].size && line[j] == '*' && line[j + 1] == '/') {
                in_comment = 0;
                j += 2;
                continue;
            }
            j++;
        }
    }
}

int is_plain_text_file(void) {
    return libedit_is_plain_text(E.filename);
}

/* Returns nonzero when the current file should use C-style highlighting.
   Markdown files opt out and receive the generic highlighter. */
int is_c_source(void) {
    if (is_plain_text_file())
        return 0;

    if (!E.filename)
        return 1;

    const char *ext = strrchr(E.filename, '.');
    if (!ext)
        return 1;

    if (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0)
        return 0;

    return 1;
}

int is_other_source(void) {
    if (is_plain_text_file())
        return 0;

    return !is_c_source();
}

int getRowNumWidth(void) {
    int n = E.numrows, digits = 1;
    while (n >= 10) { n /= 10; digits++; }
    return digits + 1;
}

int editorDisplayWidth(const char *s) {
    int width = 0;
    size_t bytes;
    wchar_t wc;
    while (*s) {
        bytes = mbrtowc(&wc, s, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) { bytes = 1; wc = *s; }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        width += w; s += bytes;
    }
    return width;
}

int editorRowCxToByteIndex(EditorLine *row, int cx) {
    int cur_width = 0, index = 0;
    size_t bytes;
    wchar_t wc;
    while (index < row->size) {
        bytes = mbrtowc(&wc, row->chars + index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) { bytes = 1; wc = row->chars[index]; }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (cur_width + w > cx)
            break;
        cur_width += w; index += bytes;
    }
    return index;
}

int editorRowByteIndexToCx(EditorLine *row, int byte_index) {
    int cx = 0;
    int index = 0;
    size_t bytes;
    wchar_t wc;
    while (index < row->size && index < byte_index) {
        bytes = mbrtowc(&wc, row->chars + index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            bytes = 1;
            wc = row->chars[index];
        }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        cx += w;
        index += bytes;
    }
    return cx;
}

/* Append a highlighted line to the buffer, respecting the available width and
   the current horizontal offset. Escape sequences (starting with '\x1b') do
   not consume display width. */
void abAppendHighlighted(struct abuf *ab, const char *s, int coloff, int avail) {
    int width = 0;
    int display_col = 0;
    const char *p = s;
    int started = (coloff == 0);
    int limit_reached = 0;
    char active_color[64];
    int color_len = 0;
    int color_active = 0;

    while (*p) {
        if (*p == '\x1b') {
            const char *start = p++;
            if (*p) {
                /* Skip the first character after ESC (e.g. '[' in CSI sequences). */
                p++;
                while (*p && !(*p >= '@' && *p <= '~'))
                    p++;
                if (*p)
                    p++;
            }
            size_t esc_len = (size_t)(p - start);
            if (!started) {
                if (esc_len >= 3 && start[1] == '[' && start[esc_len - 1] == 'm') {
                    if (esc_len == 4 && start[2] == '0') {
                        color_active = 0;
                        color_len = 0;
                    } else {
                        color_active = 1;
                        size_t copy_len = esc_len;
                        if (copy_len > sizeof(active_color))
                            copy_len = sizeof(active_color);
                        memcpy(active_color, start, copy_len);
                        color_len = (int)copy_len;
                    }
                }
            } else {
                abAppend(ab, start, p - start);
            }
        } else {
            wchar_t wc;
            size_t bytes = mbrtowc(&wc, p, MB_CUR_MAX, NULL);
            if (bytes == (size_t)-1 || bytes == (size_t)-2) {
                bytes = 1;
                wc = (unsigned char)*p;
            }
            int w = wcwidth(wc);
            if (w < 0)
                w = 0;

            if (!started) {
                if (display_col + w <= coloff) {
                    display_col += w;
                    p += bytes;
                    continue;
                }
                started = 1;
                if (color_active && color_len > 0)
                    abAppend(ab, active_color, color_len);
            }

            display_col += w;
            if (!limit_reached && width + w <= avail) {
                abAppend(ab, p, bytes);
                width += w;
            } else {
                limit_reached = 1;
            }
            p += bytes;
        }
    }
}

/*** Modified Drawing Functions Using Append Buffer ***/
void editorRenderRow(EditorLine *row, int avail, struct abuf *ab) {
    int logical_width = 0;
    int byte_index = editorRowCxToByteIndex(row, E.coloff);
    char buffer[1024];
    int buf_index = 0;
    size_t bytes;
    wchar_t wc;
    while (byte_index < row->size && logical_width < avail) {
        bytes = mbrtowc(&wc, row->chars + byte_index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2)
            bytes = 1;
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (logical_width + w > avail)
            break;
        for (int j = 0; j < (int)bytes; j++) {
            if (buf_index < (int)sizeof(buffer) - 1)
                buffer[buf_index++] = row->chars[byte_index + j];
        }
        logical_width += w; byte_index += bytes;
    }
    abAppend(ab, buffer, buf_index);
}

void editorRenderRowWithSelection(EditorLine *row, int file_row, int avail, struct abuf *ab) {
    int logical_width = 0;
    int byte_index = editorRowCxToByteIndex(row, E.coloff);
    size_t bytes;
    wchar_t wc;
    int current_disp = 0;
    int selection_active_for_row = 0, sel_local_start = 0, sel_local_end = 0;
    if (E.selecting) {
        int start_line = (E.sel_anchor_y < E.cy ? E.sel_anchor_y : E.cy);
        int end_line = (E.sel_anchor_y > E.cy ? E.sel_anchor_y : E.cy);
        if (file_row >= start_line && file_row <= end_line) {
            selection_active_for_row = 1;
            if (start_line == end_line) {
                sel_local_start = (E.sel_anchor_x < E.cx ? E.sel_anchor_x : E.cx);
                sel_local_end   = (E.sel_anchor_x < E.cx ? E.cx   : E.sel_anchor_x);
            } else if (file_row == start_line) {
                if (E.sel_anchor_y < E.cy) {
                    sel_local_start = E.sel_anchor_x;
                    sel_local_end = editorDisplayWidth(row->chars);
                } else {
                    sel_local_start = E.cx;
                    sel_local_end = editorDisplayWidth(row->chars);
                }
            } else if (file_row == end_line) {
                if (E.sel_anchor_y < E.cy) {
                    sel_local_start = 0;
                    sel_local_end = E.cx;
                } else {
                    sel_local_start = 0;
                    sel_local_end = E.sel_anchor_x;
                }
            } else {
                sel_local_start = 0;
                sel_local_end = editorDisplayWidth(row->chars);
            }
        }
    }
    int eff_sel_start = 0, eff_sel_end = 0;
    if (selection_active_for_row) {
        eff_sel_start = (sel_local_start < E.coloff ? 0 : sel_local_start - E.coloff);
        eff_sel_end = sel_local_end - E.coloff;
        if (eff_sel_end < 0) eff_sel_end = 0;
        if (eff_sel_start < 0) eff_sel_start = 0;
        if (eff_sel_end > avail) eff_sel_end = avail;
    }
    int in_selection = 0;
    while (byte_index < row->size && logical_width < avail) {
        bytes = mbrtowc(&wc, row->chars + byte_index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2)
            bytes = 1;
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (selection_active_for_row && current_disp >= eff_sel_start && current_disp < eff_sel_end) {
            if (!in_selection) { abAppend(ab, "\x1b[7m", 4); in_selection = 1; }
        } else {
            if (in_selection) { abAppend(ab, "\x1b[0m", 4); in_selection = 0; }
        }
        if (logical_width + w > avail)
            break;
        abAppend(ab, row->chars + byte_index, bytes);
        logical_width += w; current_disp += w; byte_index += bytes;
    }
    if (in_selection)
        abAppend(ab, "\x1b[0m", 4);
}

void editorDrawRows(struct abuf *ab, int rn_width) {
    int text_width = E.screencols - rn_width - 1;
    int skip_highlight = is_plain_text_file();
    char numbuf[16];
    for (int y = 0; y < E.textrows; y++) {
        int file_row = E.rowoff + y;
        if (file_row < E.numrows) {
            int rn = file_row + 1;
            int num_len = snprintf(numbuf, sizeof(numbuf), "%*d ", rn_width - 1, rn);
            abAppend(ab, numbuf, num_len);
            
            if (E.selecting) {
                editorRenderRowWithSelection(&E.row[file_row], file_row, text_width, ab);
            } else if (skip_highlight) {
                editorRenderRow(&E.row[file_row], text_width, ab);
            } else if (is_c_source()) {
                /* Pass the current multi-line comment state for this line */
                char *highlighted = highlight_c_line(E.row[file_row].chars, E.row[file_row].hl_in_comment);
                if (highlighted) {
                    abAppendHighlighted(ab, highlighted, E.coloff, text_width);
                    free(highlighted);
                }
            } else {
                // Use this for all other files
                char *highlighted = highlight_other_line(E.row[file_row].chars);
                if (highlighted) {
                    abAppendHighlighted(ab, highlighted, E.coloff, text_width);
                    free(highlighted);
                }
            }
            
            int printed_width = editorDisplayWidth(E.row[file_row].chars) - E.coloff;
            if (printed_width < 0) printed_width = 0;
            if (printed_width > text_width) printed_width = text_width;
            for (int i = printed_width; i < text_width; i++)
                abAppend(ab, " ", 1);
            if (E.row[file_row].modified)
                abAppend(ab, "\x1b[41m \x1b[0m", 10);
            else
                abAppend(ab, " ", 1);
        } else {
            for (int i = 0; i < rn_width; i++)
                abAppend(ab, " ", 1);
            abAppend(ab, "~", 1);
        }
        abAppend(ab, "\x1b[K", 3);
        if (y < E.textrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorDrawTopBar(struct abuf *ab) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    abAppend(ab, "\x1b[2m", 4);
    int len = strlen(buf);
    if (len > E.screencols) len = E.screencols;
    int padding = (E.screencols - len) / 2;
    for (int i = 0; i < padding; i++)
        abAppend(ab, " ", 1);
    abAppend(ab, buf, len);
    for (int i = padding + len; i < E.screencols; i++)
        abAppend(ab, " ", 1);
    abAppend(ab, "\x1b[0m", 4);
}

void editorDrawStatusBar(struct abuf *ab) {
    char status[80];
    char rstatus[32];
    int len = snprintf(status, sizeof(status), "%.20s%s",
                       E.filename ? E.filename : "[No Name]",
                       E.dirty ? " (modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "Ln %d, Col %d",
                        E.cy + 1, E.cx + 1);
    if (len > E.screencols)
        len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen)
            break;
        abAppend(ab, " ", 1);
        len++;
    }
    abAppend(ab, rstatus, rlen);
}

void editorDrawShortcutBar(struct abuf *ab) {
    abAppend(ab, "\x1b[2m", 4);
    char menu[256];
    int menu_len = snprintf(menu, sizeof(menu),
        "|^Q QUIT|^S SAVE|^Z UNDO|^X CUT|^C COPY|^V PASTE|^T SELECT|^A ALL|^F FND|^R REP|");
    if (menu_len > E.screencols) menu_len = E.screencols;
    abAppend(ab, menu, menu_len);
    for (int i = menu_len; i < E.screencols; i++)
        abAppend(ab, " ", 1);
    abAppend(ab, "\x1b[0m", 4);
}

/*** Modified Screen Refresh Routine ***/
void editorRefreshScreen(void) {
    if (is_c_source() && E.syntax_dirty) {
        update_syntax();
        E.syntax_dirty = 0;
    }
    struct abuf ab = ABUF_INIT;
    int rn_width = getRowNumWidth();
    E.textrows = E.screenrows - 3;                // space for top bar + two bottom bars
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    editorDrawTopBar(&ab);                        // draw time/date
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[2;1H");  // start text area on line 2
    abAppend(&ab, buf, len);
    editorDrawRows(&ab, rn_width);
    len = snprintf(buf, sizeof(buf), "\x1b[%d;1H", E.textrows + 2);
    abAppend(&ab, buf, len);
    abAppend(&ab, "\x1b[2m", 4);
    editorDrawStatusBar(&ab);
    abAppend(&ab, "\x1b[0m", 4);
    len = snprintf(buf, sizeof(buf), "\x1b[%d;1H", E.screenrows);
    abAppend(&ab, buf, len);
    editorDrawShortcutBar(&ab);
    int cursor_y = (E.cy - E.rowoff) + 2;         // cursor is below top bar
    int cursor_x = rn_width + (E.cx - E.coloff) + 1;
    if (cursor_y < 2) cursor_y = 2;
    if (cursor_x < 1) cursor_x = 1;
    len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
    abAppend(&ab, buf, len);
    abAppend(&ab, "\x1b[?25h", 6);
    if (write(STDOUT_FILENO, ab.b, ab.len) < 0) {
        perror("write");
    }
    abFree(&ab);
}

/* --- Input Function --- */
/*
   This implementation follows the original approach:
   - If a non-ESC character is read, it is returned.
   - If an ESC is read, two characters are read.
     * If the sequence is ESC [ followed by a digit and '~', the appropriate key is returned.
     * If the sequence is ESC [ followed by a letter, arrow keys and Home/End are handled.
   - Additionally, if the sequence matches a bracketed paste start ([200~) or end ([201~)
     the global flag in_paste_mode is set or cleared accordingly and the sequence is discarded.
*/
int editorReadKey(void) {
    char c; int nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) == 0)
        ;
    if (nread == -1 && errno != EAGAIN)
        die("read");

    if (c == '\x1b') {
        char seq[6];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] != '[')
            return '\x1b';

        // Check for bracketed paste sequences.
        if (seq[1] == '2') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1)
                return '\x1b';
            if (seq[2] == '0') {
                if (read(STDIN_FILENO, &seq[3], 1) != 1)
                    return '\x1b';
                if (read(STDIN_FILENO, &seq[4], 1) != 1)
                    return '\x1b';
                if (seq[3] == '0' && seq[4] == '~') {
                    in_paste_mode = 1;
                    return editorReadKey(); // Discard paste start sequence.
                }
                if (seq[3] == '1' && seq[4] == '~') {
                    in_paste_mode = 0;
                    return editorReadKey(); // Discard paste end sequence.
                }
            }
        }
        // If the second character is a digit, handle numeric escape sequences.
        if (seq[1] >= '0' && seq[1] <= '9') {
            char seq3;
            if (read(STDIN_FILENO, &seq3, 1) != 1)
                return '\x1b';
            if (seq3 == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PGUP_KEY;
                    case '6': return PGDN_KEY;
                    default: return '\x1b';
                }
            }
        } else {
            // Otherwise, handle arrow keys and similar.
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                default: return '\x1b';
            }
        }
    }
    return c;
}

int getWindowSize(int *rows, int *cols) {
    if (rows == NULL || cols == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        *rows = budostack_get_target_rows();
        *cols = budostack_get_target_cols();
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }

    budostack_clamp_terminal_size(rows, cols);
    return 0;
}

/*** Undo Functions ***/
void push_undo_state(void) {
    UndoState *state = malloc(sizeof(UndoState));
    if (!state) die("malloc undo state");
    state->cx = E.cx; state->cy = E.cy; state->numrows = E.numrows;
    state->row = malloc(sizeof(EditorLine) * E.numrows);
    if (!state->row) die("malloc undo rows");
    state->modified = malloc(sizeof(int) * E.numrows);
    if (!state->modified) die("malloc undo modified");
    for (int i = 0; i < E.numrows; i++) {
        state->row[i].size = E.row[i].size;
        state->row[i].chars = malloc(E.row[i].size + 1);
        if (!state->row[i].chars) die("malloc undo row char");
        memcpy(state->row[i].chars, E.row[i].chars, E.row[i].size);
        state->row[i].chars[E.row[i].size] = '\0';
        state->modified[i] = E.row[i].modified;
    }
    if (undo_history_len == 100) {
        free_undo_state(undo_history[0]);
        for (int i = 1; i < 100; i++)
            undo_history[i - 1] = undo_history[i];
        undo_history_len--;
    }
    undo_history[undo_history_len++] = state;
}

void free_undo_state(UndoState *state) {
    for (int i = 0; i < state->numrows; i++)
        free(state->row[i].chars);
    free(state->row);
    free(state->modified);
    free(state);
}

void pop_undo_state(void) {
    if (undo_history_len == 0)
        return;
    UndoState *state = undo_history[undo_history_len - 1];
    undo_history_len--;
    for (int i = 0; i < E.numrows; i++)
        free(E.row[i].chars);
    free(E.row);
    E.numrows = state->numrows;
    E.row = malloc(sizeof(EditorLine) * E.numrows);
    if (!E.row) die("malloc restore rows");
    for (int i = 0; i < E.numrows; i++) {
        E.row[i].size = state->row[i].size;
        E.row[i].chars = malloc(E.row[i].size + 1);
        if (!E.row[i].chars) die("malloc restore row char");
        memcpy(E.row[i].chars, state->row[i].chars, E.row[i].size);
        E.row[i].chars[E.row[i].size] = '\0';
        E.row[i].modified = state->modified[i];
    }
    E.cx = state->cx; E.cy = state->cy;
    free_undo_state(state);
}

/*** Terminal Setup Functions ***/
void die(const char *s) {
    if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) perror("write");
    if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) perror("write");
    perror(s);
    exit(1);
}

void disableRawMode(void) {
    // Disable bracketed paste mode before restoring terminal settings.
    if (write(STDOUT_FILENO, "\x1b[?2004l", 9) < 0) perror("write");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
    // Enable bracketed paste mode.
    if (write(STDOUT_FILENO, "\x1b[?2004h", 9) < 0) perror("write");
}

/*** Input Processing ***/
void editorProcessKeypress(void) {
    int c = editorReadKey();
    static int last_key_was_vertical = 0;

    if (c == CTRL_KEY('t')) {
        if (E.selecting) {
            E.selecting = 0;
            snprintf(E.status_message, sizeof(E.status_message), "Selection canceled");
        } else {
            E.selecting = 1;
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            snprintf(E.status_message, sizeof(E.status_message), "Selection started");
        }
        last_key_was_vertical = 0;
        return;
    }
    if (c == CTRL_KEY('a')) {
        if (E.numrows > 0) {
            E.selecting = 1;
            E.sel_anchor_x = 0;
            E.sel_anchor_y = 0;
            E.cy = E.numrows - 1;
            E.cx = editorDisplayWidth(E.row[E.cy].chars);
            snprintf(E.status_message, sizeof(E.status_message), "Selected all text");
        }
        last_key_was_vertical = 0;
        return;
    }
    if ((c == CTRL_KEY('h') || c == BACKSPACE) && E.selecting) {
        push_undo_state();
        editorDeleteSelection();
        last_key_was_vertical = 0;
        return;
    }
    if (c == DEL_KEY && E.selecting) {
        push_undo_state();
        editorDeleteSelection();
        last_key_was_vertical = 0;
        return;
    }
    switch (c) {
        case CTRL_KEY('q'):
            if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) perror("write");
            if (write(STDOUT_FILENO, "\x1b[H", 3) < 0) perror("write");
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('z'):
            pop_undo_state();
            break;
        case CTRL_KEY('x'):
            push_undo_state();
            editorCutSelection();
            break;
        case CTRL_KEY('c'):
            push_undo_state();
            editorCopySelection();
            E.selecting = 0;
            break;
        case CTRL_KEY('v'):
            push_undo_state();
            /* Disable auto-indent during paste initiated by CTRL+V */
            {
                int old_auto_indent = auto_indent_enabled;
                auto_indent_enabled = 0;
                editorPasteClipboard();
                auto_indent_enabled = old_auto_indent;
            }
            break;
        case DEL_KEY:
            push_undo_state();
            editorDelCharAtCursor();
            break;
        case HOME_KEY:
            E.cx = 0;
            E.preferred_cx = E.cx;
            last_key_was_vertical = 0;
            break;
        case END_KEY:
            E.cx = editorDisplayWidth(E.row[E.cy].chars);
            E.preferred_cx = E.cx;
            last_key_was_vertical = 0;
            break;
        case PGUP_KEY:
            E.cy -= E.textrows;
            if (E.cy < 0)
                E.cy = 0;
            last_key_was_vertical = 0;
            break;
        case PGDN_KEY:
            E.cy += E.textrows;
            if (E.cy >= E.numrows)
                E.cy = E.numrows - 1;
            last_key_was_vertical = 0;
            break;
        case CTRL_KEY('f'):
            push_undo_state();
            editorSearch();
            last_key_was_vertical = 0;
            break;
        case CTRL_KEY('r'):
                    push_undo_state();
                    editorReplace();
                    last_key_was_vertical = 0;
                    break;
        case '\r':
        case '\n': // FIX: multiline paste
            push_undo_state();
            editorInsertNewline();
            last_key_was_vertical = 0;
            break;
        case '\t':
            /* TAB support: insert spaces based on EDITOR_TAB_WIDTH */
            push_undo_state();
            for (int i = 0; i < EDITOR_TAB_WIDTH; i++)
                editorInsertChar(' ');
            last_key_was_vertical = 0;
            break;
        case CTRL_KEY('h'):
        case BACKSPACE:
            push_undo_state();
            editorDelChar();
            last_key_was_vertical = 0;
            break;
        case ARROW_UP:
            if (!last_key_was_vertical)
                E.preferred_cx = E.cx;
            last_key_was_vertical = 1;
            if (E.cy > 0) {
                E.cy--;
                int row_width = editorDisplayWidth(E.row[E.cy].chars);
                if (E.preferred_cx > row_width)
                    E.cx = row_width;
                else
                    E.cx = E.preferred_cx;
            }
            break;
        case ARROW_DOWN:
            if (!last_key_was_vertical)
                E.preferred_cx = E.cx;
            last_key_was_vertical = 1;
            if (E.cy < E.numrows - 1) {
                E.cy++;
                int row_width = editorDisplayWidth(E.row[E.cy].chars);
                if (E.preferred_cx > row_width)
                    E.cx = row_width;
                else
                    E.cx = E.preferred_cx;
            }
            break;
        case ARROW_LEFT:
            if (E.cx > 0)
                E.cx--;
            else if (E.cy > 0) {
                E.cy--;
                E.cx = editorDisplayWidth(E.row[E.cy].chars);
            }
            E.preferred_cx = E.cx;
            last_key_was_vertical = 0;
            break;
        case ARROW_RIGHT: {
            int roww = editorDisplayWidth(E.row[E.cy].chars);
            if (E.cx < roww)
                E.cx++;
            else if (E.cy < E.numrows - 1) {
                E.cy++;
                E.cx = 0;
            }
            E.preferred_cx = E.cx;
            last_key_was_vertical = 0;
            break;
        }
        default:
            if (!iscntrl(c)) {
                push_undo_state();
                if ((unsigned char)c < 0x80) {
                    editorInsertChar(c);
                } else {
                    int utf8_len = 0;
                    unsigned char uc = (unsigned char)c;
                    if ((uc & 0xE0) == 0xC0)
                        utf8_len = 2;
                    else if ((uc & 0xF0) == 0xE0)
                        utf8_len = 3;
                    else if ((uc & 0xF8) == 0xF0)
                        utf8_len = 4;
                    else
                        utf8_len = 1;
                    char utf8_buf[5];
                    utf8_buf[0] = c;
                    for (int i = 1; i < utf8_len; i++)
                        utf8_buf[i] = editorReadKey();
                    utf8_buf[utf8_len] = '\0';
                    editorInsertUTF8(utf8_buf, utf8_len);
                }
            }
            last_key_was_vertical = 0;
            break;
    }
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.textrows)
        E.rowoff = E.cy - E.textrows + 1;

    int rn_width = getRowNumWidth();
    int text_width = E.screencols - rn_width - 1;
    if (E.cx < E.coloff)
        E.coloff = E.cx;
    if (E.cx >= E.coloff + text_width)
        E.coloff = E.cx - text_width + 1;
}

/*** New Functions for Selection Deletion and Delete Key ***/
void editorDeleteSelection(void) {
    if (!E.selecting)
        return;
    int start_line = (E.sel_anchor_y < E.cy ? E.sel_anchor_y : E.cy);
    int end_line = (E.sel_anchor_y > E.cy ? E.sel_anchor_y : E.cy);
    int anchor_x = (E.sel_anchor_y <= E.cy ? E.sel_anchor_x : E.cx);
    int current_x = (E.sel_anchor_y <= E.cy ? E.cx : E.sel_anchor_x);
    for (int i = start_line; i <= end_line; i++) {
        int line_width = editorDisplayWidth(E.row[i].chars);
        int sel_start, sel_end;
        if (start_line == end_line) {
            sel_start = (anchor_x < current_x ? anchor_x : current_x);
            sel_end   = (anchor_x < current_x ? current_x : anchor_x);
        } else if (i == start_line) {
            sel_start = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : 0);
            sel_end = line_width;
        } else if (i == end_line) {
            sel_start = 0;
            sel_end = (E.sel_anchor_y < E.cy ? E.cx : E.sel_anchor_x);
        } else {
            sel_start = 0;
            sel_end = line_width;
        }
        int start_byte = editorRowCxToByteIndex(&E.row[i], sel_start);
        int end_byte = editorRowCxToByteIndex(&E.row[i], sel_end);
        if (i == start_line && i == end_line) {
            int new_size = E.row[i].size - (end_byte - start_byte);
            memmove(E.row[i].chars + start_byte,
                    E.row[i].chars + end_byte,
                    E.row[i].size - end_byte + 1);
            E.row[i].size = new_size;
        } else if (i == start_line) {
            E.row[i].chars[start_byte] = '\0';
            E.row[i].size = start_byte;
        } else if (i == end_line) {
            char *new_last = strdup(E.row[i].chars + end_byte);
            free(E.row[i].chars);
            E.row[i].chars = new_last;
            E.row[i].size = strlen(new_last);
        } else {
            E.row[i].size = 0;
            free(E.row[i].chars);
            E.row[i].chars = strdup("");
        }
    }
    if (start_line != end_line) {
        EditorLine *first_line = &E.row[start_line];
        EditorLine *last_line = &E.row[end_line];
        int new_size = first_line->size + last_line->size;
        first_line->chars = realloc(first_line->chars, new_size + 1);
        memcpy(first_line->chars + first_line->size, last_line->chars, last_line->size + 1);
        first_line->size = new_size;
        for (int i = end_line; i > start_line; i--) {
            free(E.row[i].chars);
            for (int j = i; j < E.numrows - 1; j++)
                E.row[j] = E.row[j + 1];
            E.numrows--;
        }
    }
    E.cx = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : E.cx);
    E.cy = start_line;
    E.selecting = 0;
    E.dirty = 1;
    E.syntax_dirty = 1;
    snprintf(E.status_message, sizeof(E.status_message), "Deleted selection");
}

void editorDelCharAtCursor(void) {
    if (E.cy == E.numrows)
        return;
    EditorLine *line = &E.row[E.cy];
    int row_display_width = editorDisplayWidth(line->chars);
    if (E.cx < row_display_width) {
       int index = editorRowCxToByteIndex(line, E.cx);
       int next_index = editorRowCxToByteIndex(line, E.cx + 1);
       memmove(&line->chars[index], &line->chars[next_index], line->size - next_index + 1);
       line->size -= (next_index - index);
       line->modified = 1;
       E.dirty = 1;
       E.syntax_dirty = 1;
    } else if (E.cx == row_display_width && E.cy < E.numrows - 1) {
       EditorLine *next_line = &E.row[E.cy+1];
       line->chars = realloc(line->chars, line->size + next_line->size + 1);
       if (!line->chars) die("realloc");
       memcpy(line->chars + line->size, next_line->chars, next_line->size);
       line->size += next_line->size;
       line->chars[line->size] = '\0';
       line->modified = 1;
       free(next_line->chars);
       for (int j = E.cy+1; j < E.numrows - 1; j++)
           E.row[j] = E.row[j+1];
       E.numrows--;
       E.dirty = 1;
       E.syntax_dirty = 1;
    }
}

/*** Modified Search Function ***/
void editorSearch(void) {
    char query[256] = "";
    int from_selection = E.selecting;
    if (from_selection) {
        int start_line = (E.sel_anchor_y < E.cy ? E.sel_anchor_y : E.cy);
        int end_line   = (E.sel_anchor_y > E.cy ? E.sel_anchor_y : E.cy);
        int anchor_x   = (E.sel_anchor_y <= E.cy ? E.sel_anchor_x : E.cx);
        int current_x  = (E.sel_anchor_y <= E.cy ? E.cx : E.sel_anchor_x);
        int pos = 0;
        for (int i = start_line; i <= end_line && pos < (int)sizeof(query) - 1; i++) {
            EditorLine *row = &E.row[i];
            int line_width = editorDisplayWidth(row->chars);
            int sel_start, sel_end;
            if (start_line == end_line) {
                sel_start = (anchor_x < current_x ? anchor_x : current_x);
                sel_end   = (anchor_x < current_x ? current_x  : anchor_x);
            } else if (i == start_line) {
                sel_start = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : 0);
                sel_end   = line_width;
            } else if (i == end_line) {
                sel_start = 0;
                sel_end   = (E.sel_anchor_y < E.cy ? E.cx : E.sel_anchor_x);
            } else {
                sel_start = 0;
                sel_end   = line_width;
            }
            int start_byte = editorRowCxToByteIndex(row, sel_start);
            int end_byte   = editorRowCxToByteIndex(row, sel_end);
            int chunk = end_byte - start_byte;
            if (pos + chunk >= (int)sizeof(query) - 1) 
                chunk = sizeof(query) - 1 - pos;
            memcpy(query + pos, row->chars + start_byte, chunk);
            pos += chunk;
            if (i != end_line && pos < (int)sizeof(query) - 1)
                query[pos++] = '\n';
        }
        query[pos] = '\0';
        E.selecting = 0;
    }

    /* Switch to the alternate screen buffer so the search UI doesn't overlay the editor */
    printf("\033[?1049h");
    printf("\033[H"); /* ensure prompt starts at the top */
    fflush(stdout);

    /* Temporarily disable raw mode to get query input if not from selection */
    disableRawMode();
    if (!from_selection) {
        printf("\rSearch: ");
        fflush(stdout);
        if (fgets(query, sizeof(query), stdin) == NULL) {
            enableRawMode();
            printf("\033[?1049l");
            fflush(stdout);
            return;
        }
        query[strcspn(query, "\n")] = '\0';
    }
    enableRawMode();

    /* Get terminal size */
    int rows = (E.screenrows > 0) ? E.screenrows : budostack_get_target_rows();
    int cols = (E.screencols > 0) ? E.screencols : budostack_get_target_cols();
    if (getWindowSize(&rows, &cols) == -1) {
        rows = (E.screenrows > 0) ? E.screenrows : budostack_get_target_rows();
        cols = (E.screencols > 0) ? E.screencols : budostack_get_target_cols();
    }
    budostack_clamp_terminal_size(&rows, &cols);

    /* Build list of matching line indices using case-insensitive search */
    int *matches = malloc(E.numrows * sizeof(int));
    if (!matches) {
        snprintf(E.status_message, sizeof(E.status_message), "Search: malloc failed");
        printf("\033[?1049l");
        fflush(stdout);
        return;
    }
    int match_count = 0;
    for (int i = 0; i < E.numrows; i++) {
        if (strcasestr_custom(E.row[i].chars, query) != NULL)
            matches[match_count++] = i;
    }
    if (match_count == 0) {
        free(matches);
        snprintf(E.status_message, sizeof(E.status_message), "No matches found");
        printf("\033[?1049l");
        fflush(stdout);
        return;
    }

    /* Full-screen menu UI unchanged */
    int active = 0;
    int menu_start = 0;
    int menu_height = rows - 4;
    if (menu_height < 1)
        menu_height = 1;
    if (menu_height > match_count)
        menu_height = match_count;

    while (1) {
        if (menu_height > 0) {
            if (active < menu_start)
                menu_start = active;
            if (active >= menu_start + menu_height)
                menu_start = active - menu_height + 1;
            int max_start = match_count - menu_height;
            if (max_start < 0)
                max_start = 0;
            if (menu_start > max_start)
                menu_start = max_start;
        }
        printf("\033[2J\033[H");
        printf("Search results for: \"%s\"\n", query);
        printf("\r--------------------------------------------------\n");

        int end = menu_start + menu_height;
        if (end > match_count)
            end = match_count;
        for (int i = menu_start; i < end; i++) {
            if (i == active)
                printf("\033[7m");
            int preview_cols = cols - 12;
            if (preview_cols < 16)
                preview_cols = 16;
            const char *text = E.row[matches[i]].chars;
            size_t text_len = strlen(text);
            int to_copy = preview_cols;
            char preview[preview_cols + 1];
            if ((int)text_len > preview_cols) {
                to_copy = preview_cols - 3;
                if (to_copy < 0)
                    to_copy = 0;
                memcpy(preview, text, (size_t)to_copy);
                memcpy(preview + to_copy, "...", 3);
                preview[to_copy + 3] = '\0';
            } else {
                memcpy(preview, text, text_len + 1);
            }
            printf("\rLine %d: %s", matches[i] + 1, preview);
            printf("\033[0m\n");
        }
        printf("\r--------------------------------------------------\n");
        printf("\rUse Up/Down arrows to select, Enter to jump, 'q' to cancel.\n");
        fflush(stdout);

        int c = editorReadKey();
        if (c == 'q') { active = -1; break; }
        else if (c == '\r') { break; }
        else if (c == ARROW_UP) {
            if (active > 0) {
                active--;
            }
        } else if (c == ARROW_DOWN) {
            if (active < match_count - 1) {
                active++;
            }
        } else if (c == PGUP_KEY) {
            active -= menu_height;
            if (active < 0)
                active = 0;
        } else if (c == PGDN_KEY) {
            active += menu_height;
            if (active > match_count - 1)
                active = match_count - 1;
        }
    }
    int result = -1;
    if (active != -1)
        result = matches[active];
    free(matches);

    printf("\033[?1049l");
    fflush(stdout);

    if (result != -1) {
        E.cy = result;
        E.rowoff = E.cy; /* place found line at the top of the screen */
        char *posp = strcasestr_custom(E.row[result].chars, query);
        if (posp) {
            int col = 0;
            for (char *p = E.row[result].chars; p < posp; p++) col++;
            E.cx = col;
        } else {
            E.cx = 0;
        }
        snprintf(E.status_message, sizeof(E.status_message),
                 "Jumped to match on line %d", result + 1);
    } else {
        snprintf(E.status_message, sizeof(E.status_message), "Search canceled");
    }
}

void editorReplace(void) {
    char search[256] = "";
    char replace[256] = "";

    printf("\033[?1049h");
    printf("\033[H");
    fflush(stdout);

    disableRawMode();
    printf("\rSearch string: ");
    fflush(stdout);
    if (fgets(search, sizeof(search), stdin) == NULL) {
        enableRawMode();
        printf("\033[?1049l");
        fflush(stdout);
        return;
    }
    search[strcspn(search, "\n")] = '\0';

    printf("Replace with: ");
    fflush(stdout);
    if (fgets(replace, sizeof(replace), stdin) == NULL) {
        enableRawMode();
        printf("\033[?1049l");
        fflush(stdout);
        return;
    }
    replace[strcspn(replace, "\n")] = '\0';
    enableRawMode();

    printf("\033[?1049l");
    fflush(stdout);

    int search_len = strlen(search);
    if (search_len == 0) {
        snprintf(E.status_message, sizeof(E.status_message), "Empty search string");
        return;
    }

    int replace_len = strlen(replace);
    int replace_count = 0;

    int saved_selecting = E.selecting;
    int saved_anchor_x = E.sel_anchor_x;
    int saved_anchor_y = E.sel_anchor_y;
    E.selecting = 0;

    for (int i = 0; i < E.numrows; i++) {
        EditorLine *row = &E.row[i];
        int start_byte = 0;
        while (1) {
            char *match = strcasestr_custom(row->chars + start_byte, search);
            if (!match)
                break;
            int index = match - row->chars;
            int cx_start = editorRowByteIndexToCx(row, index);
            int cx_end = cx_start + editorDisplayWidth(search);

            E.sel_anchor_x = cx_start;
            E.sel_anchor_y = i;
            E.cx = cx_end;
            E.cy = i;
            if (i < E.rowoff || i >= E.rowoff + E.textrows) {
                int offset = E.textrows / 2 - 1;
                if (offset < 0) offset = 0;
                int new_rowoff = i - offset;
                if (new_rowoff < 0) new_rowoff = 0;
                if (new_rowoff > E.numrows - E.textrows)
                    new_rowoff = E.numrows - E.textrows;
                if (new_rowoff < 0) new_rowoff = 0;
                E.rowoff = new_rowoff;
            }
            E.selecting = 1;
            editorRefreshScreen();
            snprintf(E.status_message, sizeof(E.status_message),
                     "Replace? Enter=Yes, ESC=Quit");

            int c = editorReadKey();
            E.selecting = 0;
            if (c == 27) { /* ESC */
                E.selecting = saved_selecting;
                E.sel_anchor_x = saved_anchor_x;
                E.sel_anchor_y = saved_anchor_y;
                snprintf(E.status_message, sizeof(E.status_message), "Replace canceled");
                return;
            }
            if (c == '\r') {
                char *new_chars = malloc(row->size - search_len + replace_len + 1);
                if (!new_chars)
                    return;
                memcpy(new_chars, row->chars, index);
                memcpy(new_chars + index, replace, replace_len);
                strcpy(new_chars + index + replace_len, row->chars + index + search_len);
                free(row->chars);
                row->chars = new_chars;
                row->size = strlen(new_chars);
                row->modified = 1;
                E.dirty = 1;
                replace_count++;
                start_byte = index + replace_len;
            } else {
                start_byte = index + search_len;
            }
        }
    }

    E.selecting = saved_selecting;
    E.sel_anchor_x = saved_anchor_x;
    E.sel_anchor_y = saved_anchor_y;
    snprintf(E.status_message, sizeof(E.status_message), "Replaced %d occurrence(s)", replace_count);
}

/*** Clipboard and Selection Functions ***/
void editorCopySelection(void) {
    if (!E.selecting)
        return;
    int start_line = (E.sel_anchor_y < E.cy ? E.sel_anchor_y : E.cy);
    int end_line = (E.sel_anchor_y > E.cy ? E.sel_anchor_y : E.cy);
    int anchor_x = (E.sel_anchor_y <= E.cy ? E.sel_anchor_x : E.cx);
    int current_x = (E.sel_anchor_y <= E.cy ? E.cx : E.sel_anchor_x);
    size_t bufsize = 1024;
    char *buf = malloc(bufsize);
    if (!buf) die("malloc clipboard");
    buf[0] = '\0';
    size_t len = 0;
    for (int i = start_line; i <= end_line; i++) {
        int line_width = editorDisplayWidth(E.row[i].chars);
        int sel_start, sel_end;
        if (start_line == end_line) {
            sel_start = (anchor_x < current_x ? anchor_x : current_x);
            sel_end   = (anchor_x < current_x ? current_x : anchor_x);
        } else if (i == start_line) {
            sel_start = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : 0);
            sel_end = line_width;
        } else if (i == end_line) {
            sel_start = 0;
            sel_end = (E.sel_anchor_y < E.cy ? E.cx : E.sel_anchor_x);
        } else {
            sel_start = 0;
            sel_end = line_width;
        }
        int start_byte = editorRowCxToByteIndex(&E.row[i], sel_start);
        int end_byte = editorRowCxToByteIndex(&E.row[i], sel_end);
        int chunk_len = end_byte - start_byte;
        if (len + chunk_len + 2 > bufsize) {
            bufsize *= 2;
            buf = realloc(buf, bufsize);
            if (!buf) die("realloc clipboard");
        }
        memcpy(buf + len, E.row[i].chars + start_byte, chunk_len);
        len += chunk_len;
        if (i != end_line)
            buf[len++] = '\n';
    }
    buf[len] = '\0';
    free(clipboard);
    clipboard = buf;
    clipboard_len = len;
    systemClipboardWrite(clipboard);
    snprintf(E.status_message, sizeof(E.status_message),
             "Copied selection (%zu bytes)", clipboard_len);
}

void editorCutSelection(void) {
    if (!E.selecting)
        return;
    editorCopySelection();
    int start_line = (E.sel_anchor_y < E.cy ? E.sel_anchor_y : E.cy);
    int end_line = (E.sel_anchor_y > E.cy ? E.sel_anchor_y : E.cy);
    int anchor_x = (E.sel_anchor_y <= E.cy ? E.sel_anchor_x : E.cx);
    int current_x = (E.sel_anchor_y <= E.cy ? E.cx : E.sel_anchor_x);
    if (start_line == end_line) {
        int sel_start = (anchor_x < current_x ? anchor_x : current_x);
        int sel_end   = (anchor_x < current_x ? current_x : anchor_x);
        int start_byte = editorRowCxToByteIndex(&E.row[start_line], sel_start);
        int end_byte = editorRowCxToByteIndex(&E.row[start_line], sel_end);
        int new_size = E.row[start_line].size - (end_byte - start_byte);
        memmove(E.row[start_line].chars + start_byte,
                E.row[start_line].chars + end_byte,
                E.row[start_line].size - end_byte + 1);
        E.row[start_line].size = new_size;
    } else {
        int first_sel_start = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : 0);
        int last_sel_end = (E.sel_anchor_y < E.cy ? E.cx : E.sel_anchor_x);
        int first_byte = editorRowCxToByteIndex(&E.row[start_line], first_sel_start);
        E.row[start_line].chars[first_byte] = '\0';
        E.row[start_line].size = first_byte;
        int last_byte = editorRowCxToByteIndex(&E.row[end_line], last_sel_end);
        char *new_last = strdup(E.row[end_line].chars + last_byte);
        free(E.row[end_line].chars);
        E.row[end_line].chars = new_last;
        E.row[end_line].size = strlen(new_last);
        int new_size = E.row[start_line].size + E.row[end_line].size;
        E.row[start_line].chars = realloc(E.row[start_line].chars, new_size + 1);
        memcpy(E.row[start_line].chars + E.row[start_line].size,
               E.row[end_line].chars,
               E.row[end_line].size + 1);
        E.row[start_line].size = new_size;
        for (int i = end_line; i > start_line; i--) {
            free(E.row[i].chars);
            for (int j = i; j < E.numrows - 1; j++)
                E.row[j] = E.row[j + 1];
            E.numrows--;
        }
    }
    E.cx = (E.sel_anchor_y < E.cy ? E.sel_anchor_x : E.cx);
    E.cy = start_line;
    E.selecting = 0;
    E.dirty = 1;
    E.syntax_dirty = 1;
    snprintf(E.status_message, sizeof(E.status_message), "Cut selection");
}

void editorPasteClipboard(void) {
    char *sys = systemClipboardRead();
    if (sys) {
        free(clipboard);
        clipboard = sys;
        clipboard_len = strlen(clipboard);
    }
    if (!clipboard)
        return;
    push_undo_state();
    editorInsertString(clipboard);
    snprintf(E.status_message, sizeof(E.status_message),
             "Pasted clipboard (%zu bytes)", clipboard_len);
}

void editorInsertString(const char *s) {
    while (*s) {
        if (*s == '\n')
            editorInsertNewline();
        else
            editorInsertChar(*s);
        s++;
    }
}

void systemClipboardWrite(const char *s) {
    if (!s)
        return;
    FILE *fp = popen("xclip -selection clipboard", "w");
    if (!fp)
        return;
    if (fwrite(s, 1, strlen(s), fp) != strlen(s)) {
        perror("fwrite");
    }
    pclose(fp);
}

char *systemClipboardRead(void) {
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!fp)
        return NULL;
    size_t cap = 256;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }
    size_t len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

/*** Modified editorInsertNewline with auto-indent ***
   When auto_indent_enabled is true and not in paste mode, the new line is pre-filled with the current line's leading whitespace.
   When disabled (or in paste mode), newlines are inserted without auto-indent.
*/
void editorInsertNewline(void) {
    EditorLine *line = &E.row[E.cy];
    char *indent = "";
    if (auto_indent_enabled && !in_paste_mode) {
        int pos = 0;
        while (pos < line->size && (line->chars[pos] == ' ' || line->chars[pos] == '\t'))
            pos++;
        indent = malloc(pos + 1);
        if (!indent) die("malloc");
        memcpy(indent, line->chars, pos);
        indent[pos] = '\0';
    }
    int index = editorRowCxToByteIndex(line, E.cx);
    char *remainder = malloc(line->size - index + 1);
    if (!remainder) die("malloc");
    memcpy(remainder, &line->chars[index], line->size - index);
    remainder[line->size - index] = '\0';
    line->size = index;
    line->chars[index] = '\0';
    char *new_content;
    if (auto_indent_enabled && !in_paste_mode) {
        size_t indent_len = strlen(indent);
        size_t rem_len = strlen(remainder);
        new_content = malloc(indent_len + rem_len + 1);
        if (!new_content) die("malloc");
        memcpy(new_content, indent, indent_len);
        memcpy(new_content + indent_len, remainder, rem_len + 1);
        E.cx = indent_len;
        free(indent);
    } else {
        new_content = strdup(remainder);
        E.cx = 0;
    }
    free(remainder);
    EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
    if (!new_row) die("realloc");
    E.row = new_row;
    for (int j = E.numrows; j > E.cy; j--)
        E.row[j] = E.row[j - 1];
    E.numrows++;
    E.row[E.cy + 1].chars = new_content;
    E.row[E.cy + 1].size = strlen(new_content);
    E.row[E.cy + 1].modified = 1;
    E.row[E.cy + 1].hl_in_comment = 0;
    E.cy++;
    E.preferred_cx = E.cx;
    E.dirty = 1;
    E.syntax_dirty = 1;
}

void editorDelChar(void) {
    if (E.cy == E.numrows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;
    EditorLine *line = &E.row[E.cy];
    if (E.cx == 0) {
        EditorLine *prev_line = &E.row[E.cy - 1];
        int prev_size = prev_line->size;
        prev_line->chars = realloc(prev_line->chars, prev_size + line->size + 1);
        if (prev_line->chars == NULL)
            die("realloc");
        memcpy(prev_line->chars + prev_size, line->chars, line->size);
        prev_line->chars[prev_size + line->size] = '\0';
        prev_line->size = prev_size + line->size; prev_line->modified = 1;
        free(line->chars);
        for (int j = E.cy; j < E.numrows - 1; j++)
            E.row[j] = E.row[j + 1];
        E.numrows--; E.cy--;
        E.cx = editorDisplayWidth(prev_line->chars);
        E.preferred_cx = E.cx;
    } else {
        int index = editorRowCxToByteIndex(line, E.cx);
        int prev_index = editorRowCxToByteIndex(line, E.cx - 1);
        memmove(&line->chars[prev_index], &line->chars[index], line->size - index + 1);
        line->size -= (index - prev_index);
        E.cx -= 1;
        E.preferred_cx = E.cx;
        line->modified = 1;
    }
    E.dirty = 1;
    E.syntax_dirty = 1;
}

/*** File/Buffer Operations ***/
void editorOpen(const char *filename) {
    free(E.filename);
    E.filename = strdup(filename);
    if (E.filename == NULL)
        die("strdup");
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (errno == ENOENT) {
            editorAppendLine("", 0);
            E.dirty = 0;
            E.syntax_dirty = 1;
            return;
        } else {
            die("fopen");
        }
    }
    char *line = NULL; size_t linecap = 0; ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }
        char *expanded = expand_tabs(line);
        if (expanded) {
            editorAppendLine(expanded, strlen(expanded));
            free(expanded);
        } else {
            editorAppendLine(line, linelen);
        }
    }
    free(line); fclose(fp);
    E.dirty = 0;
    if (E.numrows == 0)
        editorAppendLine("", 0);
    E.syntax_dirty = 1;
}

void editorSave(void) {
    if (E.filename == NULL)
        return;
    int total_len = 0;
    for (int j = 0; j < E.numrows; j++)
        total_len += E.row[j].size + 1;
    char *buf = malloc(total_len);
    if (buf == NULL)
        die("malloc");
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        if (write(fd, buf, total_len) == total_len) {
            close(fd); free(buf);
            E.dirty = 0;
            for (int i = 0; i < E.numrows; i++)
                E.row[i].modified = 0;
            return;
        }
        close(fd);
    }
    free(buf);
    die("write");
}

void editorAppendLine(char *s, size_t len) {
    EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
    if (new_row == NULL)
        die("realloc");
    E.row = new_row;
    E.row[E.numrows].chars = malloc(len + 1);
    if (E.row[E.numrows].chars == NULL)
        die("malloc");
    memcpy(E.row[E.numrows].chars, s, len);
    E.row[E.numrows].chars[len] = '\0';
    E.row[E.numrows].size = (int)len;
    E.row[E.numrows].modified = 0;
    E.row[E.numrows].hl_in_comment = 0;
    E.numrows++;
}

void editorInsertChar(int c) {
    if (E.cy == E.numrows)
        editorAppendLine("", 0);
    EditorLine *line = &E.row[E.cy];
    if (E.cx > editorDisplayWidth(line->chars))
        E.cx = editorDisplayWidth(line->chars);
    int index = editorRowCxToByteIndex(line, E.cx);
    char *new_chars = realloc(line->chars, line->size + 2);
    if (new_chars == NULL)
        die("realloc");
    line->chars = new_chars;
    memmove(&line->chars[index + 1], &line->chars[index], line->size - index + 1);
    line->chars[index] = c;
    line->size++;
    E.cx++;
    E.preferred_cx = E.cx;
    line->modified = 1; E.dirty = 1;
    E.syntax_dirty = 1;
}

void editorInsertUTF8(const char *s, int len) {
    if (E.cy == E.numrows)
        editorAppendLine("", 0);
    EditorLine *line = &E.row[E.cy];
    if (E.cx > editorDisplayWidth(line->chars))
        E.cx = editorDisplayWidth(line->chars);
    int index = editorRowCxToByteIndex(line, E.cx);
    char *new_chars = realloc(line->chars, line->size + len + 1);
    if (new_chars == NULL)
        die("realloc");
    line->chars = new_chars;
    memmove(&line->chars[index + len], &line->chars[index], line->size - index + 1);
    memcpy(&line->chars[index], s, len);
    line->size += len;
    wchar_t wc;
    size_t bytes = mbrtowc(&wc, s, len, NULL);
    int width = (bytes == (size_t)-1 || bytes == (size_t)-2) ? 1 : wcwidth(wc);
    if (width < 0) width = 1;
    E.cx += width;
    E.preferred_cx = E.cx;
    line->modified = 1; E.dirty = 1;
    E.syntax_dirty = 1;
}

/*** Modified Main ***/
int main(int argc, char *argv[]) {
    setlocale(LC_CTYPE, "");
    E.cx = 0; E.cy = 0;
    E.rowoff = 0; E.coloff = 0;
    E.numrows = 0; E.row = NULL;
    E.filename = NULL; E.dirty = 0;
    E.status_message[0] = '\0';
    E.selecting = 0; E.sel_anchor_x = 0; E.sel_anchor_y = 0;
    E.preferred_cx = 0;
    E.syntax_dirty = 1;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        E.screenrows = budostack_get_target_rows();
        E.screencols = budostack_get_target_cols();
    }
    E.textrows = E.screenrows - 3;

    if (argc >= 2)
        editorOpen(argv[1]);
    else {
        editorAppendLine("", 0);
        E.dirty = 0;
    }

    enableRawMode();
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret == -1)
            die("select");

        if (ret > 0) {
            /*
             * Process all pending input before refreshing the screen.  This
             * drastically speeds up bracketed paste operations because the
             * display is redrawn only once after the whole chunk is inserted.
             */
            do {
                editorProcessKeypress();
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
            } while (ret > 0);
        }

        editorRefreshScreen();
    }
    return 0;
}
