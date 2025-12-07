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
#include <strings.h>
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
    bool *softbreak;
    unsigned char *softbreak_gap;
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

    bool prompt_active;
    char prompt_line[128];
};

static struct EditorState E;
static char *clipboard;
static size_t clipboard_len;

struct HistoryEntry {
    struct Document doc;
    int cx;
    int cy;
    int rowoff;
    enum PageSize page_size;
    bool dirty;
};

static struct HistoryEntry *undo_stack;
static size_t undo_len;
static size_t undo_cap;
static struct HistoryEntry *redo_stack;
static size_t redo_len;
static size_t redo_cap;

#define HISTORY_LIMIT 200

static void editorInsertRowInternal(struct Document *doc, int at, const char *s, size_t len, bool soft, unsigned char gap);
static void editorInsertRow(int at, const char *s, size_t len);
static size_t editorCursorOffset(void);
static void editorRestoreCursor(size_t offset);
static char *editorPlainText(size_t *out_len);
static void freeDocument(struct Document *doc);
static int editorPrompt(const char *message, char *buf, size_t buflen);

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
    if (E.screenrows < 6)
        E.screenrows = 6;

    E.textrows = E.screenrows - 3;
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

        unsigned char gap = 0;
        if (new_start > (size_t)split) {
            size_t diff = new_start - (size_t)split;
            gap = diff > 255 ? 255 : (unsigned char)diff;
        }

        editorInsertRowInternal(&E.doc, row + 1, tail, tail_len, true, gap);
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

static char *editorPlainText(size_t *out_len) {
    size_t total = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        total += E.doc.rowlen[i];
        if (i + 1 < E.doc.numrows) {
            if (E.doc.softbreak[i + 1])
                total += E.doc.softbreak_gap[i + 1];
            else
                total++;
        }
    }

    char *buf = malloc(total + 1);
    if (buf == NULL)
        return NULL;

    size_t p = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        memcpy(&buf[p], E.doc.rows[i], E.doc.rowlen[i]);
        p += E.doc.rowlen[i];
        if (i + 1 < E.doc.numrows) {
            if (E.doc.softbreak[i + 1]) {
                size_t gap = E.doc.softbreak_gap[i + 1];
                if (gap == 0)
                    gap = 1;
                for (size_t g = 0; g < gap; g++)
                    buf[p++] = ' ';
            } else {
                buf[p++] = '\n';
            }
        }
    }
    buf[p] = '\0';
    if (out_len != NULL)
        *out_len = p;
    return buf;
}

static void appendWrappedLines(struct Document *doc, const char *text, size_t len) {
    size_t start = 0;
    bool first_line = true;
    while (start < len) {
        size_t remaining = len - start;
        if ((int)remaining <= E.page_width) {
            editorInsertRowInternal(doc, doc->numrows, &text[start], remaining, !first_line, 0);
            break;
        }

        size_t wrap_at = start + (size_t)E.page_width;
        size_t candidate = wrap_at;
        while (candidate > start && text[candidate - 1] != ' ')
            candidate--;
        if (candidate == start)
            candidate = wrap_at;

        size_t split = candidate;
        while (split > start && text[split - 1] == ' ')
            split--;

        size_t new_start = candidate;
        while (new_start < len && text[new_start] == ' ')
            new_start++;

        unsigned char gap = 0;
        if (new_start > split) {
            size_t diff = new_start - split;
            gap = diff > 255 ? 255 : (unsigned char)diff;
        }

        editorInsertRowInternal(doc, doc->numrows, &text[start], split - start, !first_line, gap);
        start = new_start;
        first_line = false;
    }
}

static void editorWrapDocument(void) {
    size_t plain_len = 0;
    char *plain = editorPlainText(&plain_len);
    if (plain == NULL)
        return;

    size_t offset = editorCursorOffset();

    struct Document newdoc = {0};
    int row_start = 0;
    for (size_t i = 0; i <= plain_len; i++) {
        if (i == plain_len || plain[i] == '\n') {
            size_t seg_len = i - (size_t)row_start;
            if (seg_len == 0)
                editorInsertRowInternal(&newdoc, newdoc.numrows, "", 0, false, 0);
            else
                appendWrappedLines(&newdoc, &plain[row_start], seg_len);
            row_start = (int)i + 1;
        }
    }

    free(plain);
    freeDocument(&E.doc);
    E.doc = newdoc;

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
    for (int i = 0; i < E.cy && i < E.doc.numrows; i++) {
        offset += E.doc.rowlen[i];
        if (i + 1 < E.doc.numrows) {
            if (E.doc.softbreak[i + 1])
                offset += E.doc.softbreak_gap[i + 1];
            else
                offset++;
        }
    }
    offset += (size_t)E.cx;
    return offset;
}

static void editorRestoreCursor(size_t offset) {
    int row = 0;
    while (row < E.doc.numrows) {
        size_t row_span = E.doc.rowlen[row];
        if (row + 1 < E.doc.numrows) {
            if (E.doc.softbreak[row + 1])
                row_span += E.doc.softbreak_gap[row + 1];
            else
                row_span += 1u;
        }
        if (offset <= row_span) {
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

static void freeDocument(struct Document *doc) {
    if (doc == NULL)
        return;
    for (int i = 0; i < doc->numrows; i++)
        free(doc->rows[i]);
    free(doc->rows);
    free(doc->rowlen);
    free(doc->softbreak);
    free(doc->softbreak_gap);
    doc->rows = NULL;
    doc->rowlen = NULL;
    doc->softbreak = NULL;
    doc->softbreak_gap = NULL;
    doc->numrows = 0;
}

static int copyDocument(const struct Document *src, struct Document *dest) {
    dest->numrows = src->numrows;
    dest->rows = NULL;
    dest->rowlen = NULL;
    dest->softbreak = NULL;
    dest->softbreak_gap = NULL;
    if (src->numrows == 0)
        return 0;

    dest->rows = malloc(sizeof(char *) * (size_t)src->numrows);
    dest->rowlen = malloc(sizeof(size_t) * (size_t)src->numrows);
    dest->softbreak = malloc(sizeof(bool) * (size_t)src->numrows);
    dest->softbreak_gap = malloc(sizeof(unsigned char) * (size_t)src->numrows);
    if (dest->rows == NULL || dest->rowlen == NULL || dest->softbreak == NULL || dest->softbreak_gap == NULL) {
        free(dest->rows);
        free(dest->rowlen);
        free(dest->softbreak);
        free(dest->softbreak_gap);
        dest->rows = NULL;
        dest->rowlen = NULL;
        dest->softbreak = NULL;
        dest->softbreak_gap = NULL;
        dest->numrows = 0;
        return -1;
    }

    for (int i = 0; i < src->numrows; i++) {
        dest->rowlen[i] = src->rowlen[i];
        dest->softbreak[i] = src->softbreak[i];
        dest->softbreak_gap[i] = src->softbreak_gap[i];
        dest->rows[i] = malloc(dest->rowlen[i] + 1);
        if (dest->rows[i] == NULL) {
            for (int j = 0; j < i; j++)
                free(dest->rows[j]);
            free(dest->rows);
            free(dest->rowlen);
            free(dest->softbreak);
            free(dest->softbreak_gap);
            dest->rows = NULL;
            dest->rowlen = NULL;
            dest->softbreak = NULL;
            dest->softbreak_gap = NULL;
            dest->numrows = 0;
            return -1;
        }
        memcpy(dest->rows[i], src->rows[i], dest->rowlen[i]);
        dest->rows[i][dest->rowlen[i]] = '\0';
    }
    return 0;
}

static void clearHistory(struct HistoryEntry **stack, size_t *len, size_t *cap) {
    if (*stack != NULL) {
        for (size_t i = 0; i < *len; i++)
            freeDocument(&(*stack)[i].doc);
        free(*stack);
    }
    *stack = NULL;
    *len = 0;
    *cap = 0;
}

static int pushHistory(struct HistoryEntry **stack, size_t *len, size_t *cap, const struct HistoryEntry *entry) {
    if (*len >= HISTORY_LIMIT && *len > 0) {
        freeDocument(&(*stack)[0].doc);
        memmove(&(*stack)[0], &(*stack)[1], sizeof(struct HistoryEntry) * (*len - 1));
        (*len)--;
    }

    if (*len >= *cap) {
        size_t new_cap = *cap == 0 ? 16 : *cap * 2;
        struct HistoryEntry *new_stack = realloc(*stack, new_cap * sizeof(struct HistoryEntry));
        if (new_stack == NULL)
            return -1;
        *stack = new_stack;
        *cap = new_cap;
    }

    (*stack)[*len] = *entry;
    (*len)++;
    return 0;
}

static int snapshotCurrent(struct HistoryEntry *entry) {
    if (copyDocument(&E.doc, &entry->doc) == -1)
        return -1;
    entry->cx = E.cx;
    entry->cy = E.cy;
    entry->rowoff = E.rowoff;
    entry->page_size = E.page_size;
    entry->dirty = E.dirty;
    return 0;
}

static void editorClearRedo(void) {
    clearHistory(&redo_stack, &redo_len, &redo_cap);
}

static void editorPushUndo(void) {
    struct HistoryEntry entry;
    if (snapshotCurrent(&entry) == -1)
        return;
    editorClearRedo();
    if (pushHistory(&undo_stack, &undo_len, &undo_cap, &entry) == -1)
        freeDocument(&entry.doc);
}

static void editorApplySnapshot(struct HistoryEntry *entry) {
    freeDocument(&E.doc);
    E.doc = entry->doc;
    entry->doc.rows = NULL;
    entry->doc.rowlen = NULL;
    entry->doc.softbreak = NULL;
    entry->doc.softbreak_gap = NULL;
    entry->doc.numrows = 0;
    E.cx = entry->cx;
    E.cy = entry->cy;
    E.rowoff = entry->rowoff;
    E.page_size = entry->page_size;
    E.dirty = entry->dirty;
    editorUpdateLayout();
    editorWrapDocument();
    if (E.cy >= E.doc.numrows)
        E.cy = E.doc.numrows > 0 ? E.doc.numrows - 1 : 0;
    size_t rowlen = (E.cy < E.doc.numrows) ? E.doc.rowlen[E.cy] : 0;
    if (E.cx > (int)rowlen)
        E.cx = (int)rowlen;
}

static void editorUndo(void) {
    if (undo_len == 0) {
        editorSetStatus("Nothing to undo");
        return;
    }

    struct HistoryEntry redo_entry;
    if (snapshotCurrent(&redo_entry) == -1)
        return;
    if (pushHistory(&redo_stack, &redo_len, &redo_cap, &redo_entry) == -1) {
        freeDocument(&redo_entry.doc);
        return;
    }

    undo_len--;
    struct HistoryEntry *entry = &undo_stack[undo_len];
    editorApplySnapshot(entry);
    entry->doc.rows = NULL;
    entry->doc.rowlen = NULL;
    entry->doc.numrows = 0;
    editorSetStatus("Undo");
}

static void editorRedo(void) {
    if (redo_len == 0) {
        editorSetStatus("Nothing to redo");
        return;
    }

    struct HistoryEntry undo_entry;
    if (snapshotCurrent(&undo_entry) == -1)
        return;
    if (pushHistory(&undo_stack, &undo_len, &undo_cap, &undo_entry) == -1) {
        freeDocument(&undo_entry.doc);
        return;
    }

    redo_len--;
    struct HistoryEntry *entry = &redo_stack[redo_len];
    editorApplySnapshot(entry);
    entry->doc.rows = NULL;
    entry->doc.rowlen = NULL;
    entry->doc.numrows = 0;
    editorSetStatus("Redo");
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

    const char *bar_bg = "\x1b[0;48;5;17m";
    const char *bar_reset = "\x1b[0m";

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    /* Top bar (two rows) */
    char top1[256];
    char top2[256];
    snprintf(top1, sizeof(top1), " Book  C-N New  C-O Open  C-S Save  C-G SaveAs  C-E Export  C-Q Quit ");
    snprintf(top2, sizeof(top2), " C-F Find  C-R Rplc  C-C Copy  C-V Paste  C-P Page %s  C-Z Undo  C-Y Redo ",
             (E.page_size == PAGE_A4 ? "A4" : (E.page_size == PAGE_A5 ? "A5" : "A6")));

    const char *tops[2] = {top1, top2};
    for (int i = 0; i < 2; i++) {
        int top_len = (int)strlen(tops[i]);
        if (top_len > E.screencols)
            top_len = E.screencols;

        abAppend(&ab, bar_bg, strlen(bar_bg));
        abAppend(&ab, tops[i], (size_t)top_len);
        if (top_len < E.screencols) {
            for (int j = top_len; j < E.screencols; j++)
                abAppend(&ab, " ", 1);
        }
        abAppend(&ab, bar_reset, strlen(bar_reset));
        abAppend(&ab, "\r\n", 2);
    }

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
        if (i + 1 < E.doc.numrows && E.doc.softbreak[i + 1] && E.doc.softbreak_gap[i + 1] > 0) {
            if (in_word)
                words++;
            in_word = false;
        }
        if (in_word)
            words++;
    }

    char bottom[256];
    if (E.prompt_active) {
        snprintf(bottom, sizeof(bottom), " %s", E.prompt_line);
    } else {
        const char *name = E.filename ? E.filename : "[new book]";
        char status_info[64];
        snprintf(status_info, sizeof(status_info), "%s | %s | %ld words",
                 name, timestr, words);

        if (E.status_message[0] != '\0' && time(NULL) - E.status_time < 5) {
            snprintf(bottom, sizeof(bottom), "%s | %s", status_info, E.status_message);
        } else {
            snprintf(bottom, sizeof(bottom), "%s", status_info);
        }
    }

    int bottom_len = (int)strlen(bottom);
    if (bottom_len > E.screencols)
        bottom_len = E.screencols;
    abAppend(&ab, bar_bg, strlen(bar_bg));
    abAppend(&ab, bottom, (size_t)bottom_len);
    if (bottom_len < E.screencols) {
        for (int i = bottom_len; i < E.screencols; i++)
            abAppend(&ab, " ", 1);
    }
    abAppend(&ab, bar_reset, strlen(bar_reset));

    int cursor_x = E.margin_left + (E.cx - E.coloff) + 1;
    int cursor_y = (E.cy - E.rowoff) + 3;
    abAppend(&ab, "\x1b[H", 3);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

static void editorInsertRowInternal(struct Document *doc, int at, const char *s, size_t len, bool soft, unsigned char gap) {
    if (at < 0 || at > doc->numrows)
        return;

    char **new_rows = realloc(doc->rows, sizeof(char *) * (size_t)(doc->numrows + 1));
    size_t *new_rowlen = realloc(doc->rowlen, sizeof(size_t) * (size_t)(doc->numrows + 1));
    bool *new_soft = realloc(doc->softbreak, sizeof(bool) * (size_t)(doc->numrows + 1));
    unsigned char *new_gap = realloc(doc->softbreak_gap, sizeof(unsigned char) * (size_t)(doc->numrows + 1));
    if (new_rows == NULL || new_rowlen == NULL || new_soft == NULL || new_gap == NULL)
        return;

    doc->rows = new_rows;
    doc->rowlen = new_rowlen;
    doc->softbreak = new_soft;
    doc->softbreak_gap = new_gap;

    memmove(&doc->rows[at + 1], &doc->rows[at], sizeof(char *) * (size_t)(doc->numrows - at));
    memmove(&doc->rowlen[at + 1], &doc->rowlen[at], sizeof(size_t) * (size_t)(doc->numrows - at));
    memmove(&doc->softbreak[at + 1], &doc->softbreak[at], sizeof(bool) * (size_t)(doc->numrows - at));
    memmove(&doc->softbreak_gap[at + 1], &doc->softbreak_gap[at], sizeof(unsigned char) * (size_t)(doc->numrows - at));

    doc->rows[at] = malloc(len + 1);
    if (doc->rows[at] == NULL)
        return;
    memcpy(doc->rows[at], s, len);
    doc->rows[at][len] = '\0';
    doc->rowlen[at] = len;
    doc->softbreak[at] = soft;
    doc->softbreak_gap[at] = gap;
    doc->numrows++;
    if (doc == &E.doc)
        E.dirty = true;
}

static void editorInsertRow(int at, const char *s, size_t len) {
    editorInsertRowInternal(&E.doc, at, s, len, false, 0);
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
    memmove(&E.doc.softbreak[at], &E.doc.softbreak[at + 1], sizeof(bool) * (size_t)(E.doc.numrows - at - 1));
    memmove(&E.doc.softbreak_gap[at], &E.doc.softbreak_gap[at + 1], sizeof(unsigned char) * (size_t)(E.doc.numrows - at - 1));
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

    editorPushUndo();
    editorInsertString(clipboard);
    editorSetStatus("Pasted clipboard (%zu byte%s)", clipboard_len, clipboard_len == 1 ? "" : "s");
}

static char *editorWrappedRowsToString(int *buflen) {
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

static enum PageSize editorParsePage(const char *v) {
    if (strcasecmp(v, "A4") == 0)
        return PAGE_A4;
    if (strcasecmp(v, "A5") == 0)
        return PAGE_A5;
    return PAGE_A6;
}

static void editorLoadPlainText(const char *text, size_t len) {
    freeDocument(&E.doc);
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t seg = i - start;
            editorInsertRow(E.doc.numrows, &text[start], seg);
            start = i + 1;
        }
    }
    if (E.doc.numrows == 0)
        editorInsertRow(0, "", 0);
    editorWrapDocument();
    E.dirty = false;
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
}

static void editorOpen(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        editorSetStatus("Cannot open %s", filename);
        return;
    }

    struct HistoryEntry snap;
    bool snapshot_ok = snapshotCurrent(&snap) == 0;

    free(E.filename);
    E.filename = strdup(filename);

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        editorSetStatus("Open failed");
        return;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        editorSetStatus("Open failed");
        return;
    }
    rewind(fp);

    char *buf = malloc((size_t)sz + 1u);
    if (buf == NULL) {
        fclose(fp);
        editorSetStatus("Open failed");
        return;
    }
    size_t read_len = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[read_len] = '\0';

    if (read_len >= 4 && strncmp(buf, "BK1\n", 4) == 0) {
        char *page_line = strchr(buf + 4, '\n');
        if (page_line != NULL && page_line - buf > 4 && strncmp(buf + 4, "page:", 5) == 0) {
            *page_line = '\0';
            E.page_size = editorParsePage(buf + 9);
            editorUpdateLayout();
            char *content = page_line + 1;
            if (*content == '\n')
                content++;
            size_t content_len = read_len - (size_t)(content - buf);
            editorLoadPlainText(content, content_len);
        } else {
            editorUpdateLayout();
            editorLoadPlainText(buf + 4, read_len - 4);
        }
    } else {
        editorUpdateLayout();
        editorLoadPlainText(buf, read_len);
    }

    free(buf);
    editorSetStatus("Loaded %s", filename);
    if (snapshot_ok) {
        editorClearRedo();
        if (pushHistory(&undo_stack, &undo_len, &undo_cap, &snap) == -1)
            freeDocument(&snap.doc);
    }
}

static bool has_bk_extension(const char *name) {
    size_t len = strlen(name);
    return len >= 3 && strcasecmp(&name[len - 3], ".bk") == 0;
}

static char *ensure_bk_extension(const char *name) {
    if (has_bk_extension(name))
        return strdup(name);
    size_t len = strlen(name);
    char *out = malloc(len + 4);
    if (out == NULL)
        return NULL;
    memcpy(out, name, len);
    memcpy(out + len, ".bk", 4);
    return out;
}

static void editorSave(void) {
    if (E.filename == NULL) {
        editorSetStatus("Save: enter filename with Ctrl-G");
        return;
    }

    char *target = ensure_bk_extension(E.filename);
    if (target == NULL) {
        editorSetStatus("Save failed");
        return;
    }

    size_t plain_len = 0;
    char *plain = editorPlainText(&plain_len);
    if (plain == NULL) {
        free(target);
        editorSetStatus("Save failed");
        return;
    }

    const char *page = (E.page_size == PAGE_A4 ? "A4" : (E.page_size == PAGE_A5 ? "A5" : "A6"));
    char header[64];
    int header_len = snprintf(header, sizeof(header), "BK1\npage:%s\n\n", page);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        free(target);
        free(plain);
        editorSetStatus("Save failed");
        return;
    }

    size_t total_len = (size_t)header_len + plain_len;
    char *buf = malloc(total_len);
    if (buf == NULL) {
        free(target);
        free(plain);
        editorSetStatus("Save failed");
        return;
    }
    memcpy(buf, header, (size_t)header_len);
    memcpy(buf + header_len, plain, plain_len);
    free(plain);

    int fd = open(target, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, 0) != -1) {
            if (write(fd, buf, total_len) == (ssize_t)total_len) {
                close(fd);
                free(buf);
                free(E.filename);
                E.filename = target;
                E.dirty = false;
                editorSetStatus("Saved %s (%zu bytes)", E.filename, total_len);
                return;
            }
        }
        close(fd);
    }
    free(target);
    free(buf);
    editorSetStatus("Save failed: %s", strerror(errno));
}

static void editorSaveAs(void) {
    char name[128] = "";
    if (editorPrompt("Save as: ", name, sizeof(name)) == -1) {
        editorSetStatus("Save as canceled");
        return;
    }

    free(E.filename);
    E.filename = ensure_bk_extension(name);
    if (E.filename != NULL)
        editorSave();
    else
        editorSetStatus("Save failed");
}

static char *editorExportName(void) {
    if (E.filename == NULL)
        return NULL;
    size_t len = strlen(E.filename);
    if (has_bk_extension(E.filename))
        len -= 3;
    char *out = malloc(len + 5);
    if (out == NULL)
        return NULL;
    memcpy(out, E.filename, len);
    memcpy(out + len, ".txt", 5);
    return out;
}

static void editorExportText(void) {
    if (E.filename == NULL) {
        editorSetStatus("Name the book before export");
        return;
    }

    editorWrapDocument();
    int len = 0;
    char *buf = editorWrappedRowsToString(&len);
    if (buf == NULL) {
        editorSetStatus("Export failed");
        return;
    }

    char *txtname = editorExportName();
    if (txtname == NULL) {
        free(buf);
        editorSetStatus("Export failed");
        return;
    }

    int fd = open(txtname, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        ssize_t written = -1;
        if (ftruncate(fd, 0) != -1)
            written = write(fd, buf, (size_t)len);
        if (written == (ssize_t)len) {
            close(fd);
            free(buf);
            editorSetStatus("Exported %s", txtname);
            free(txtname);
            return;
        }
        close(fd);
    }
    free(txtname);
    free(buf);
    editorSetStatus("Export failed: %s", strerror(errno));
}

static void editorNewFile(void) {
    editorPushUndo();
    freeDocument(&E.doc);
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
    E.prompt_active = true;
    snprintf(E.prompt_line, sizeof(E.prompt_line), "%s", message);
    while (true) {
        editorRefreshScreen();
        char prompt_pos[32];
        int line = E.screenrows - 1;
        snprintf(prompt_pos, sizeof(prompt_pos), "\x1b[%d;1H", line);
        write(STDOUT_FILENO, prompt_pos, strlen(prompt_pos));
        write(STDOUT_FILENO, "\x1b[2K", 4);
        write(STDOUT_FILENO, message, strlen(message));
        write(STDOUT_FILENO, buf, strlen(buf));
        int c = editorReadKey();
        if (c == '\r') {
            if (len > 0) {
                E.prompt_active = false;
                E.prompt_line[0] = '\0';
                editorSetStatus("");
                return 0;
            }
        } else if (c == '\x1b') {
            E.prompt_active = false;
            E.prompt_line[0] = '\0';
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

        snprintf(E.prompt_line, sizeof(E.prompt_line), "%s%s", message, buf);
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

    size_t find_len = strlen(find);
    int hits = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        const char *row = E.doc.rows[i];
        const char *p = row;
        while ((p = strstr(p, find)) != NULL) {
            hits++;
            p += find_len;
        }
    }

    if (hits == 0) {
        editorSetStatus("No matches for '%s'", find);
        return;
    }

    editorPushUndo();

    int replaced_lines = 0;
    for (int i = 0; i < E.doc.numrows; i++) {
        size_t new_len = 0;
        char *updated = replaceAll(E.doc.rows[i], E.doc.rowlen[i], find, replace, &new_len);
        if (updated == NULL)
            continue;
        if (new_len != E.doc.rowlen[i] || memcmp(updated, E.doc.rows[i], new_len) != 0) {
            free(E.doc.rows[i]);
            E.doc.rows[i] = updated;
            E.doc.rowlen[i] = new_len;
            replaced_lines++;
        } else {
            free(updated);
        }
    }

    if (replaced_lines > 0) {
        E.dirty = true;
        editorWrapDocument();
        editorSetStatus("Replaced %d line(s)", replaced_lines);
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
    E.prompt_active = false;
    E.prompt_line[0] = '\0';
    undo_stack = NULL;
    undo_len = 0;
    undo_cap = 0;
    redo_stack = NULL;
    redo_len = 0;
    redo_cap = 0;

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
}

static void editorProcessKeypress(void) {
    int c = editorReadKey();
    switch (c) {
        case '\r':
            editorPushUndo();
            editorInsertNewline();
            break;
        case CTRL_KEY('q'):
            E.running = false;
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('e'):
            editorExportText();
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
        case CTRL_KEY('p'):
            editorCyclePageSize();
            break;
        case CTRL_KEY('z'):
            editorUndo();
            break;
        case CTRL_KEY('y'):
            editorRedo();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            editorPushUndo();
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
            if (!iscntrl(c) && c < 128) {
                editorPushUndo();
                editorInsertChar(c);
            }
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

