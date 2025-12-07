#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define BUDOSTACK_TARGET_COLS 79
#define BUDOSTACK_TARGET_ROWS 44
#include "../lib/terminal_layout.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef BOOK_TARGET_COLS
#define BOOK_TARGET_COLS BUDOSTACK_TARGET_COLS
#endif

#ifndef BOOK_TARGET_ROWS
#define BOOK_TARGET_ROWS BUDOSTACK_TARGET_ROWS
#endif

#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127
#define DEL_KEY 1000
#define ARROW_LEFT 1001
#define ARROW_RIGHT 1002
#define ARROW_UP 1003
#define ARROW_DOWN 1004
#define HOME_KEY 1005
#define END_KEY 1006
#define PAGE_UP 1007
#define PAGE_DOWN 1008

struct abuf {
    char *b;
    size_t len;
};

#define ABUF_INIT {NULL, 0}

static void abAppend(struct abuf *ab, const char *s, size_t len) {
    char *new_b = realloc(ab->b, ab->len + len);
    if (new_b == NULL)
        return;
    memcpy(&new_b[ab->len], s, len);
    ab->b = new_b;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

enum PageSize {
    PAGE_A4 = 0,
    PAGE_A5,
    PAGE_A6,
};

struct Document {
    char **rows;
    size_t *rowlen;
    int numrows;
};

struct EditorState {
    int cx;
    int cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int textrows;
    int margin_left;
    int page_width;
    enum PageSize page_size;
    int page_height;

    char *filename;
    bool dirty;

    char status_message[128];
    time_t status_time;

    struct termios orig_termios;
    struct Document doc;
    bool running;
};

static struct EditorState E;
static char *clipboard;
static size_t clipboard_len;

static void editorInsertRow(int at, const char *s, size_t len);
static size_t editorCursorOffset(void);
static void editorRestoreCursor(size_t offset);

static int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

static void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

static void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        exit(1);
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        exit(1);
}

static int editorReadKey(void) {
    char c;
    while (true) {
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread == -1 && errno != EAGAIN)
            exit(1);
        if (nread == 0)
            continue;
        break;
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        }

        return '\x1b';
    }

    return c;
}

static void editorSetStatus(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_message, sizeof(E.status_message), fmt, ap);
    va_end(ap);
    E.status_time = time(NULL);
}

static void editorUpdateLayout(void) {
    if (E.screencols < 10)
        E.screencols = 10;
    if (E.screenrows < 5)
        E.screenrows = 5;

    E.textrows = E.screenrows - 2;
    int target_width = E.screencols - 6;
    int target_height = E.textrows - 2;

    switch (E.page_size) {
        case PAGE_A4:
            E.page_width = target_width > 66 ? 66 : target_width;
            E.page_height = target_height > 40 ? 40 : target_height;
            break;
        case PAGE_A5:
            E.page_width = target_width > 54 ? 54 : target_width;
            E.page_height = target_height > 32 ? 32 : target_height;
            break;
        case PAGE_A6:
            E.page_width = target_width > 42 ? 42 : target_width;
            E.page_height = target_height > 24 ? 24 : target_height;
            break;
    }

    if (E.page_width < 20)
        E.page_width = 20;
    if (E.page_height < 10)
        E.page_height = 10;

    E.margin_left = (E.screencols - E.page_width) / 2;
    if (E.margin_left < 0)
        E.margin_left = 0;
}

static void editorWrapLine(int row) {
    while (row < E.doc.numrows) {
        size_t len = E.doc.rowlen[row];
        if ((int)len <= E.page_width || len == 0)
            return;

        int wrap = E.page_width;
        for (int i = wrap; i > 0; i--) {
            if (E.doc.rows[row][(size_t)i - 1] == ' ') {
                wrap = i;
                break;
            }
        }

        int split = wrap;
        while (split > 0 && E.doc.rows[row][(size_t)split - 1] == ' ')
            split--;

        size_t new_start = (size_t)wrap;
        while (new_start < len && E.doc.rows[row][new_start] == ' ')
            new_start++;

        size_t tail_len = len - new_start;
        char *tail = malloc(tail_len + 1);
        if (tail == NULL)
            return;
        memcpy(tail, &E.doc.rows[row][new_start], tail_len);
        tail[tail_len] = '\0';

        E.doc.rowlen[row] = (size_t)split;
        E.doc.rows[row][split] = '\0';

        editorInsertRow(row + 1, tail, tail_len);
        free(tail);
        E.dirty = true;

        if (E.cy == row) {
            if (E.cx > split) {
                int shift = (int)new_start;
                E.cy = row + 1;
                E.cx = E.cx - shift;
                if (E.cx < 0)
                    E.cx = 0;
                if (E.cx > (int)E.doc.rowlen[E.cy])
                    E.cx = (int)E.doc.rowlen[E.cy];
            }
        } else if (E.cy > row) {
            E.cy++;
        }

        row++;
    }
}

static void editorWrapDocument(void) {
    size_t offset = editorCursorOffset();
    for (int i = 0; i < E.doc.numrows; i++)
        editorWrapLine(i);
    editorRestoreCursor(offset);
    E.coloff = 0;
}

static void systemClipboardWrite(const char *s) {
    if (s == NULL)
        return;
    FILE *fp = popen("xclip -selection clipboard", "w");
    if (fp == NULL)
        return;
    size_t len = strlen(s);
    if (len > 0 && fwrite(s, 1, len, fp) != len)
        perror("fwrite");
    pclose(fp);
}

static char *systemClipboardRead(void) {
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (fp == NULL)
        return NULL;

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        pclose(fp);
        return NULL;
    }

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (tmp == NULL) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

static size_t editorCursorOffset(void) {
    size_t offset = 0;
    for (int i = 0; i < E.cy && i < E.doc.numrows; i++)
        offset += E.doc.rowlen[i] + 1;
    offset += (size_t)E.cx;
    return offset;
}

static void editorRestoreCursor(size_t offset) {
    int row = 0;
    while (row < E.doc.numrows) {
        size_t row_span = E.doc.rowlen[row] + 1;
        if (offset < row_span) {
            E.cy = row;
            if (offset > E.doc.rowlen[row])
                offset = E.doc.rowlen[row];
            E.cx = (int)offset;
            return;
        }
        offset -= row_span;
        row++;
    }
    E.cy = E.doc.numrows ? E.doc.numrows - 1 : 0;
    E.cx = (E.cy < E.doc.numrows) ? (int)E.doc.rowlen[E.cy] : 0;
}

static void editorScroll(void) {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.textrows) {
        E.rowoff = E.cy - E.textrows + 1;
    }

    E.coloff = 0;
}

static void editorRefreshScreen(void) {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    /* Top bar */
    char top[256];
    snprintf(top, sizeof(top), "Book|C-N New C-O Open C-S Save C-G SaveAs C-F Find/Rpl C-C Copy C-V Paste|Pg %s",
             (E.page_size == PAGE_A4 ? "A4" : (E.page_size == PAGE_A5 ? "A5" : "A6")));
    int top_len = (int)strlen(top);
    if (top_len > E.screencols)
        top_len = E.screencols;
    abAppend(&ab, top, (size_t)top_len);
    if (top_len < E.screencols) {
        for (int i = top_len; i < E.screencols; i++)
            abAppend(&ab, " ", 1);
    }
    abAppend(&ab, "\r\n", 2);

    for (int y = 0; y < E.textrows; y++) {
        int file_row = E.rowoff + y;
        bool draw_split = (file_row > 0 && file_row % E.page_height == 0);
        if (draw_split) {
            for (int i = 0; i < E.margin_left; i++)
                abAppend(&ab, " ", 1);
            for (int i = 0; i < E.page_width; i++)
                abAppend(&ab, "-", 1);
            abAppend(&ab, "\x1b[K\r\n", 6);
            continue;
        }

        if (file_row < E.doc.numrows) {
            int len = (int)E.doc.rowlen[file_row] - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.page_width)
                len = E.page_width;

            for (int i = 0; i < E.margin_left; i++)
                abAppend(&ab, " ", 1);

            abAppend(&ab, &E.doc.rows[file_row][E.coloff], (size_t)len);
            if (len < E.page_width) {
                int padding = E.page_width - len;
                for (int i = 0; i < padding; i++)
                    abAppend(&ab, " ", 1);
            }
        } else {
            for (int i = 0; i < E.margin_left + E.page_width; i++)
                abAppend(&ab, " ", 1);
        }
        abAppend(&ab, "\x1b[K\r\n", 6);
    }

    /* Bottom bar */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm);

    long words = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        const char *row = E.doc.rows[i];
        bool in_word = false;
        for (size_t j = 0; j < E.doc.rowlen[i]; j++) {
            if (isspace((unsigned char)row[j])) {
                if (in_word)
                    words++;
                in_word = false;
            } else {
                in_word = true;
            }
        }
        if (in_word)
            words++;
    }

    char bottom[256];
    const char *name = E.filename ? E.filename : "[new book]";
    char status_info[64];
    snprintf(status_info, sizeof(status_info), "%s | %s | %ld words",
             name, timestr, words);

    if (E.status_message[0] != '\0' && time(NULL) - E.status_time < 5) {
        snprintf(bottom, sizeof(bottom), "%s | %s", status_info, E.status_message);
    } else {
        snprintf(bottom, sizeof(bottom), "%s", status_info);
    }

    int bottom_len = (int)strlen(bottom);
    if (bottom_len > E.screencols)
        bottom_len = E.screencols;
    abAppend(&ab, bottom, (size_t)bottom_len);
    if (bottom_len < E.screencols) {
        for (int i = bottom_len; i < E.screencols; i++)
            abAppend(&ab, " ", 1);
    }

    int cursor_x = E.margin_left + (E.cx - E.coloff) + 1;
    int cursor_y = (E.cy - E.rowoff) + 2;
    abAppend(&ab, "\x1b[H", 3);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

static void editorInsertRow(int at, const char *s, size_t len) {
    if (at < 0 || at > E.doc.numrows)
        return;

    char **new_rows = realloc(E.doc.rows, sizeof(char *) * (size_t)(E.doc.numrows + 1));
    size_t *new_rowlen = realloc(E.doc.rowlen, sizeof(size_t) * (size_t)(E.doc.numrows + 1));
    if (new_rows == NULL || new_rowlen == NULL)
        return;

    E.doc.rows = new_rows;
    E.doc.rowlen = new_rowlen;

    memmove(&E.doc.rows[at + 1], &E.doc.rows[at], sizeof(char *) * (size_t)(E.doc.numrows - at));
    memmove(&E.doc.rowlen[at + 1], &E.doc.rowlen[at], sizeof(size_t) * (size_t)(E.doc.numrows - at));

    E.doc.rows[at] = malloc(len + 1);
    if (E.doc.rows[at] == NULL)
        return;
    memcpy(E.doc.rows[at], s, len);
    E.doc.rows[at][len] = '\0';
    E.doc.rowlen[at] = len;
    E.doc.numrows++;
    E.dirty = true;
}

static void editorFreeRow(int at) {
    if (at < 0 || at >= E.doc.numrows)
        return;
    free(E.doc.rows[at]);
}

static void editorDelRow(int at) {
    if (at < 0 || at >= E.doc.numrows)
        return;
    editorFreeRow(at);
    memmove(&E.doc.rows[at], &E.doc.rows[at + 1], sizeof(char *) * (size_t)(E.doc.numrows - at - 1));
    memmove(&E.doc.rowlen[at], &E.doc.rowlen[at + 1], sizeof(size_t) * (size_t)(E.doc.numrows - at - 1));
    E.doc.numrows--;
    E.dirty = true;
}

static void editorRowInsertChar(int at, int c, int pos) {
    if (at < 0 || at >= E.doc.numrows)
        return;
    if (pos < 0 || pos > (int)E.doc.rowlen[at])
        pos = (int)E.doc.rowlen[at];

    char *row = E.doc.rows[at];
    char *new_row = realloc(row, E.doc.rowlen[at] + 2);
    if (new_row == NULL)
        return;
    memmove(&new_row[pos + 1], &new_row[pos], E.doc.rowlen[at] - (size_t)pos + 1);
    new_row[pos] = (char)c;
    E.doc.rows[at] = new_row;
    E.doc.rowlen[at]++;
    E.dirty = true;
}

static void editorRowAppendString(int at, const char *s, size_t len) {
    if (at < 0 || at >= E.doc.numrows)
        return;
    char *row = E.doc.rows[at];
    char *new_row = realloc(row, E.doc.rowlen[at] + len + 1);
    if (new_row == NULL)
        return;
    memcpy(&new_row[E.doc.rowlen[at]], s, len);
    new_row[E.doc.rowlen[at] + len] = '\0';
    E.doc.rows[at] = new_row;
    E.doc.rowlen[at] += len;
    E.dirty = true;
}

static void editorRowDelChar(int at, int pos) {
    if (at < 0 || at >= E.doc.numrows)
        return;
    if (pos < 0 || pos >= (int)E.doc.rowlen[at])
        return;

    memmove(&E.doc.rows[at][pos], &E.doc.rows[at][pos + 1], E.doc.rowlen[at] - (size_t)pos);
    E.doc.rowlen[at]--;
    E.dirty = true;
}

static void editorInsertChar(int c) {
    if (E.cy == E.doc.numrows)
        editorInsertRow(E.doc.numrows, "", 0);
    editorRowInsertChar(E.cy, c, E.cx);
    E.cx++;
    editorWrapLine(E.cy);
}

static void editorInsertNewline(void) {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        char *row = E.doc.rows[E.cy];
        editorInsertRow(E.cy + 1, &row[E.cx], E.doc.rowlen[E.cy] - (size_t)E.cx);
        E.doc.rowlen[E.cy] = (size_t)E.cx;
        E.doc.rows[E.cy][E.cx] = '\0';
    }
    E.cy++;
    E.cx = 0;
}

static void editorDelChar(void) {
    if (E.cy >= E.doc.numrows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;

    if (E.cx > 0) {
        editorRowDelChar(E.cy, E.cx - 1);
        E.cx--;
    } else {
        E.cx = (int)E.doc.rowlen[E.cy - 1];
        editorRowAppendString(E.cy - 1, E.doc.rows[E.cy], E.doc.rowlen[E.cy]);
        editorDelRow(E.cy);
        E.cy--;
    }
    editorWrapLine(E.cy);
}

static void editorInsertString(const char *s) {
    if (s == NULL)
        return;

    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') {
            editorInsertNewline();
        } else {
            editorInsertChar((unsigned char)s[i]);
        }
    }
}

static void editorCopyLine(void) {
    if (E.cy >= E.doc.numrows)
        return;

    size_t len = E.doc.rowlen[E.cy];
    char *buf = malloc(len + 1);
    if (buf == NULL)
        return;
    memcpy(buf, E.doc.rows[E.cy], len);
    buf[len] = '\0';

    free(clipboard);
    clipboard = buf;
    clipboard_len = len;
    systemClipboardWrite(clipboard);
    editorSetStatus("Copied line to clipboard (%zu byte%s)", clipboard_len, clipboard_len == 1 ? "" : "s");
}

static void editorPasteClipboard(void) {
    char *sys = systemClipboardRead();
    if (sys != NULL) {
        free(clipboard);
        clipboard = sys;
        clipboard_len = strlen(clipboard);
    }

    if (clipboard == NULL || clipboard_len == 0) {
        editorSetStatus("Clipboard is empty");
        return;
    }

    editorInsertString(clipboard);
    editorSetStatus("Pasted clipboard (%zu byte%s)", clipboard_len, clipboard_len == 1 ? "" : "s");
}

static char *editorRowsToString(int *buflen) {
    size_t totlen = 0;
    for (int i = 0; i < E.doc.numrows; i++)
        totlen += E.doc.rowlen[i] + 1;
    *buflen = (int)totlen;

    char *buf = malloc(totlen);
    if (buf == NULL)
        return NULL;
    char *p = buf;
    for (int i = 0; i < E.doc.numrows; i++) {
        memcpy(p, E.doc.rows[i], E.doc.rowlen[i]);
        p += E.doc.rowlen[i];
        *p = '\n';
        p++;
    }
    return buf;
}

static void editorOpen(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        editorSetStatus("Cannot open %s", filename);
        return;
    }

    free(E.filename);
    E.filename = strdup(filename);

    for (int i = 0; i < E.doc.numrows; i++)
        editorFreeRow(i);
    free(E.doc.rows);
    free(E.doc.rowlen);
    E.doc.rows = NULL;
    E.doc.rowlen = NULL;
    E.doc.numrows = 0;

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, fp)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        editorInsertRow(E.doc.numrows, line, (size_t)len);
    }
    free(line);
    fclose(fp);
    editorWrapDocument();
    E.dirty = false;
    E.cx = 0;
    E.cy = 0;
    editorSetStatus("Loaded %s", filename);
}

static void editorSave(void) {
    if (E.filename == NULL) {
        editorSetStatus("Save: enter filename with Ctrl-G");
        return;
    }

    int len = 0;
    char *buf = editorRowsToString(&len);
    if (buf == NULL) {
        editorSetStatus("Save failed");
        return;
    }

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, 0) != -1) {
            if (write(fd, buf, (size_t)len) == len) {
                close(fd);
                free(buf);
                E.dirty = false;
                editorSetStatus("Saved %s (%d bytes)", E.filename, len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatus("Save failed: %s", strerror(errno));
}

static void editorSaveAs(void) {
    char prompt[64] = "Save as: ";
    char name[128] = "";

    while (true) {
        editorRefreshScreen();
        write(STDOUT_FILENO, "\x1b[s", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        write(STDOUT_FILENO, prompt, strlen(prompt));
        write(STDOUT_FILENO, name, strlen(name));
        write(STDOUT_FILENO, "\x1b[u", 4);

        int c = editorReadKey();
        if (c == '\r') {
            if (strlen(name) > 0)
                break;
        } else if (c == '\x1b') {
            editorSetStatus("Save as canceled");
            return;
        } else if (c == BACKSPACE || c == CTRL_KEY('h')) {
            size_t len = strlen(name);
            if (len > 0)
                name[len - 1] = '\0';
        } else if (!iscntrl(c) && c < 128) {
            size_t len = strlen(name);
            if (len + 1 < sizeof(name)) {
                name[len] = (char)c;
                name[len + 1] = '\0';
            }
        }
    }

    free(E.filename);
    E.filename = strdup(name);
    editorSave();
}

static void editorNewFile(void) {
    for (int i = 0; i < E.doc.numrows; i++)
        editorFreeRow(i);
    free(E.doc.rows);
    free(E.doc.rowlen);
    E.doc.rows = NULL;
    E.doc.rowlen = NULL;
    E.doc.numrows = 0;
    E.cx = 0;
    E.cy = 0;
    free(E.filename);
    E.filename = NULL;
    editorInsertRow(0, "", 0);
    E.dirty = false;
    editorSetStatus("New book ready");
}

static int editorPrompt(const char *message, char *buf, size_t buflen) {
    buf[0] = '\0';
    size_t len = 0;
    while (true) {
        editorSetStatus("%s%s", message, buf);
        editorRefreshScreen();
        int c = editorReadKey();
        if (c == '\r') {
            if (len > 0) {
                editorSetStatus("");
                return 0;
            }
        } else if (c == '\x1b') {
            editorSetStatus("Canceled");
            return -1;
        } else if (c == BACKSPACE || c == CTRL_KEY('h')) {
            if (len != 0) {
                buf[--len] = '\0';
            }
        } else if (!iscntrl(c) && c < 128) {
            if (len + 1 < buflen) {
                buf[len++] = (char)c;
                buf[len] = '\0';
            }
        }
    }
}

static void editorFind(void) {
    char query[64];
    if (editorPrompt("Find: ", query, sizeof(query)) == -1)
        return;

    for (int i = 0; i < E.doc.numrows; i++) {
        char *match = strstr(E.doc.rows[i], query);
        if (match) {
            E.cy = i;
            E.cx = (int)(match - E.doc.rows[i]);
            editorSetStatus("Found '%s'", query);
            return;
        }
    }
    editorSetStatus("Not found");
}

static char *replaceAll(const char *line, size_t len, const char *find, const char *replace, size_t *outlen) {
    size_t find_len = strlen(find);
    size_t repl_len = strlen(replace);
    if (find_len == 0) {
        *outlen = len;
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, line, len);
            copy[len] = '\0';
        }
        return copy;
    }

    size_t count = 0;
    for (size_t i = 0; i + find_len <= len; i++) {
        if (memcmp(&line[i], find, find_len) == 0)
            count++;
    }
    size_t new_len = len + count * (repl_len - find_len);
    char *out = malloc(new_len + 1);
    if (!out) {
        *outlen = 0;
        return NULL;
    }

    size_t idx = 0;
    for (size_t i = 0; i < len;) {
        if (i + find_len <= len && memcmp(&line[i], find, find_len) == 0) {
            memcpy(&out[idx], replace, repl_len);
            idx += repl_len;
            i += find_len;
        } else {
            out[idx++] = line[i++];
        }
    }
    out[idx] = '\0';
    *outlen = idx;
    return out;
}

static void editorReplace(void) {
    char find[64];
    char replace[64];
    if (editorPrompt("Find for replace: ", find, sizeof(find)) == -1)
        return;
    if (editorPrompt("Replace with: ", replace, sizeof(replace)) == -1)
        return;

    int hits = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        size_t new_len = 0;
        char *updated = replaceAll(E.doc.rows[i], E.doc.rowlen[i], find, replace, &new_len);
        if (updated == NULL)
            continue;
        if (new_len != E.doc.rowlen[i] || memcmp(updated, E.doc.rows[i], new_len) != 0) {
            free(E.doc.rows[i]);
            E.doc.rows[i] = updated;
            E.doc.rowlen[i] = new_len;
            hits++;
        } else {
            free(updated);
        }
    }

    if (hits > 0) {
        E.dirty = true;
        editorWrapDocument();
        editorSetStatus("Replaced %d line(s)", hits);
    } else {
        editorSetStatus("No matches for '%s'", find);
    }
}

static void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = (int)E.doc.rowlen[E.cy];
            }
            break;
        case ARROW_RIGHT:
            if (E.cy < E.doc.numrows) {
                size_t rowlen = E.doc.rowlen[E.cy];
                if ((int)rowlen > E.cx) {
                    E.cx++;
                } else if ((int)rowlen == E.cx && E.cy + 1 < E.doc.numrows) {
                    E.cy++;
                    E.cx = 0;
                }
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)
                E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy + 1 < E.doc.numrows)
                E.cy++;
            break;
    }

    size_t rowlen = (E.cy >= E.doc.numrows) ? 0 : E.doc.rowlen[E.cy];
    if (E.cx > (int)rowlen)
        E.cx = (int)rowlen;
}

static void editorCyclePageSize(void) {
    if (E.page_size == PAGE_A6)
        E.page_size = PAGE_A4;
    else
        E.page_size = (enum PageSize)(E.page_size + 1);
    editorUpdateLayout();
    editorWrapDocument();
    editorSetStatus("Page set to %s", E.page_size == PAGE_A4 ? "A4" : (E.page_size == PAGE_A5 ? "A5" : "A6"));
}

static void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.filename = NULL;
    E.dirty = false;
    E.status_message[0] = '\0';
    E.doc.rows = NULL;
    E.doc.rowlen = NULL;
    E.doc.numrows = 0;
    E.page_size = PAGE_A4;
    E.running = true;
    clipboard = NULL;
    clipboard_len = 0;

    int rows = BOOK_TARGET_ROWS;
    int cols = BOOK_TARGET_COLS;
    budostack_clamp_terminal_size(&rows, &cols);
    if (getWindowSize(&rows, &cols) == -1) {
        rows = BOOK_TARGET_ROWS;
        cols = BOOK_TARGET_COLS;
    }
    E.screenrows = rows;
    E.screencols = cols;
    editorUpdateLayout();
    editorInsertRow(0, "", 0);
    editorSetStatus("Ctrl-] cycles page sizes; Ctrl-Q quits");
}

static void editorProcessKeypress(void) {
    int c = editorReadKey();
    switch (c) {
        case '\r':
            editorInsertNewline();
            break;
        case CTRL_KEY('q'):
            E.running = false;
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('g'):
            editorSaveAs();
            break;
        case CTRL_KEY('o'):
            editorSetStatus("Open file: type path");
            {
                char path[128];
                if (editorPrompt("Open: ", path, sizeof(path)) == 0)
                    editorOpen(path);
            }
            break;
        case CTRL_KEY('n'):
            editorNewFile();
            break;
        case CTRL_KEY('f'):
            editorFind();
            break;
        case CTRL_KEY('r'):
            editorReplace();
            break;
        case CTRL_KEY('c'):
            editorCopyLine();
            break;
        case CTRL_KEY('v'):
            editorPasteClipboard();
            break;
        case CTRL_KEY(']'):
            editorCyclePageSize();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY)
                editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN: {
            if (c == PAGE_UP)
                E.cy = E.rowoff;
            else if (c == PAGE_DOWN)
                E.cy = E.rowoff + E.textrows - 1;
            int times = E.textrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;
        }
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.doc.numrows)
                E.cx = (int)E.doc.rowlen[E.cy];
            break;
        case '\x1b':
            break;
        default:
            if (!iscntrl(c) && c < 128)
                editorInsertChar(c);
            break;
    }
}

int main(void) {
    setlocale(LC_ALL, "");
    budostack_apply_terminal_layout();
    enableRawMode();
    initEditor();

    while (E.running) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    return 0;
}

