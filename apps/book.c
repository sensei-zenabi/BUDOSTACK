#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "../lib/terminal_layout.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127

enum Key {
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE
};

typedef enum {
    PAGE_A4 = 0,
    PAGE_A5,
    PAGE_A6
} PageSize;

typedef struct {
    size_t start;
    size_t end;  /* exclusive */
} LineSpan;

typedef struct {
    int is_break; /* 0 text, 1 page break */
    size_t span_index; /* for text rows */
} VisualRow;

typedef struct {
    char *text;
    size_t len;
    size_t cap;
    size_t cursor;
} Document;

typedef struct HistoryNode {
    char *text;
    size_t len;
    size_t cursor;
    struct HistoryNode *next;
} HistoryNode;

static struct termios orig_termios;
static Document doc_state = {NULL, 0, 0, 0};
static LineSpan *wrapped = NULL;
static size_t wrapped_len = 0;
static VisualRow *visual = NULL;
static size_t visual_len = 0;
static size_t row_offset = 0;
static size_t page_line_height = 36;
static size_t page_text_width = 60;
static PageSize current_page = PAGE_A4;
static char status_msg[128] = "";
static char filename[256] = "untitled.bk";
/*
 * The book layout is tuned for a slightly smaller surface than the global
 * terminal defaults so the centered page fits cleanly when running inside the
 * SDL-based apps/terminal emulator (which targets BUDOSTACK_TARGET_COLS x
 * BUDOSTACK_TARGET_ROWS). Derive the writer's target from those values to stay
 * aligned when the terminal grid changes.
 */
#ifndef BOOK_TARGET_COLS
#define BOOK_TARGET_COLS (BUDOSTACK_TARGET_COLS - 1)
#endif

#ifndef BOOK_TARGET_ROWS
#define BOOK_TARGET_ROWS (BUDOSTACK_TARGET_ROWS - 1)
#endif

static int screen_rows = BOOK_TARGET_ROWS;
static int screen_cols = BOOK_TARGET_COLS;
static int text_rows = 40;
static int top_rows = 2;
static int bottom_rows = 1;
static int prompt_rows = 1;
static HistoryNode *undo_stack = NULL;
static HistoryNode *redo_stack = NULL;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void die(const char *msg) {
    disable_raw_mode();
    perror(msg);
    exit(1);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
    atexit(disable_raw_mode);
}

static void clamp_layout(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        screen_cols = BOOK_TARGET_COLS;
        screen_rows = BOOK_TARGET_ROWS;
    } else {
        screen_cols = ws.ws_col;
        screen_rows = ws.ws_row;
    }
    budostack_clamp_terminal_size(&screen_rows, &screen_cols);
    if (screen_cols > BOOK_TARGET_COLS)
        screen_cols = BOOK_TARGET_COLS;
    if (screen_rows > BOOK_TARGET_ROWS)
        screen_rows = BOOK_TARGET_ROWS;
    text_rows = screen_rows - top_rows - bottom_rows - prompt_rows;
    if (text_rows < 5)
        text_rows = 5;
}

static void ensure_capacity(size_t extra) {
    if (doc_state.len + extra < doc_state.cap)
        return;
    size_t newcap = doc_state.cap == 0 ? 1024 : doc_state.cap * 2;
    while (newcap < doc_state.len + extra)
        newcap *= 2;
    char *newtext = realloc(doc_state.text, newcap);
    if (!newtext)
        die("realloc");
    doc_state.text = newtext;
    doc_state.cap = newcap;
}

static void push_undo(void) {
    HistoryNode *node = malloc(sizeof(HistoryNode));
    if (!node)
        return;
    node->text = malloc(doc_state.len + 1);
    if (!node->text) {
        free(node);
        return;
    }
    memcpy(node->text, doc_state.text, doc_state.len);
    node->text[doc_state.len] = '\0';
    node->len = doc_state.len;
    node->cursor = doc_state.cursor;
    node->next = undo_stack;
    undo_stack = node;
}

static void clear_stack(HistoryNode **stack) {
    HistoryNode *n = *stack;
    while (n) {
        HistoryNode *next = n->next;
        free(n->text);
        free(n);
        n = next;
    }
    *stack = NULL;
}

static void restore_state(HistoryNode **from, HistoryNode **to) {
    if (!*from)
        return;
    HistoryNode *node = *from;
    *from = node->next;
    HistoryNode *redo = malloc(sizeof(HistoryNode));
    if (redo) {
        redo->text = malloc(doc_state.len + 1);
        if (redo->text) {
            memcpy(redo->text, doc_state.text, doc_state.len);
            redo->text[doc_state.len] = '\0';
            redo->len = doc_state.len;
            redo->cursor = doc_state.cursor;
            redo->next = *to;
            *to = redo;
        } else {
            free(redo);
        }
    }
    ensure_capacity(node->len + 1);
    memcpy(doc_state.text, node->text, node->len);
    doc_state.len = node->len;
    doc_state.cursor = node->cursor <= doc_state.len ? node->cursor : doc_state.len;
    free(node->text);
    free(node);
}

static void undo(void) { restore_state(&undo_stack, &redo_stack); }
static void redo(void) { restore_state(&redo_stack, &undo_stack); }

static void insert_char(char c) {
    push_undo();
    clear_stack(&redo_stack);
    ensure_capacity(1);
    memmove(&doc_state.text[doc_state.cursor + 1], &doc_state.text[doc_state.cursor], doc_state.len - doc_state.cursor);
    doc_state.text[doc_state.cursor] = c;
    doc_state.len++;
    doc_state.cursor++;
}

static void delete_char(void) {
    if (doc_state.cursor == 0)
        return;
    push_undo();
    clear_stack(&redo_stack);
    memmove(&doc_state.text[doc_state.cursor - 1], &doc_state.text[doc_state.cursor], doc_state.len - doc_state.cursor);
    doc_state.len--;
    doc_state.cursor--;
}

static void delete_forward(void) {
    if (doc_state.cursor >= doc_state.len)
        return;
    push_undo();
    clear_stack(&redo_stack);
    memmove(&doc_state.text[doc_state.cursor], &doc_state.text[doc_state.cursor + 1], doc_state.len - doc_state.cursor - 1);
    doc_state.len--;
}

static void set_status(const char *msg) {
    snprintf(status_msg, sizeof(status_msg), "%s", msg);
}

static size_t count_words(void) {
    int in_word = 0;
    size_t count = 0;
    for (size_t i = 0; i < doc_state.len; i++) {
        unsigned char c = (unsigned char)doc_state.text[i];
        if (isspace(c)) {
            if (in_word)
                in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    return count;
}

static void apply_page_size(PageSize size) {
    current_page = size;
    switch (size) {
    case PAGE_A4:
        page_text_width = 60;
        page_line_height = 36;
        break;
    case PAGE_A5:
        page_text_width = 52;
        page_line_height = 32;
        break;
    case PAGE_A6:
        page_text_width = 44;
        page_line_height = 28;
        break;
    }
}

static void wrap_document(void) {
    free(wrapped);
    wrapped = NULL;
    wrapped_len = 0;
    size_t cap = 128;
    wrapped = malloc(cap * sizeof(LineSpan));
    if (!wrapped)
        die("malloc");
    size_t pos = 0;
    while (pos < doc_state.len) {
        if (wrapped_len + 1 >= cap) {
            cap *= 2;
            LineSpan *tmp = realloc(wrapped, cap * sizeof(LineSpan));
            if (!tmp)
                die("realloc");
            wrapped = tmp;
        }
        size_t line_start = pos;
        size_t line_len = 0;
        size_t last_space = (size_t)-1;
        while (pos < doc_state.len) {
            char c = doc_state.text[pos];
            if (c == '\n') {
                pos++;
                break;
            }
            if (isspace((unsigned char)c))
                last_space = pos;
            if (line_len + 1 > (size_t)page_text_width) {
                if (last_space != (size_t)-1 && last_space >= line_start) {
                    pos = last_space + 1;
                    while (pos < doc_state.len && doc_state.text[pos] == ' ')
                        pos++;
                }
                break;
            }
            line_len++;
            pos++;
        }
        size_t end = pos;
        if (end < line_start)
            end = line_start;
        wrapped[wrapped_len].start = line_start;
        wrapped[wrapped_len].end = end;
        wrapped_len++;
    }
    if (doc_state.len == 0) {
        wrapped[0].start = 0;
        wrapped[0].end = 0;
        wrapped_len = 1;
    }
}

static void build_visual_rows(void) {
    free(visual);
    visual = NULL;
    visual_len = 0;
    size_t cap = wrapped_len + wrapped_len / page_line_height + 4;
    visual = malloc(cap * sizeof(VisualRow));
    if (!visual)
        die("malloc");
    size_t line_in_page = 0;
    for (size_t i = 0; i < wrapped_len; i++) {
        if (visual_len >= cap) {
            cap *= 2;
            VisualRow *tmp = realloc(visual, cap * sizeof(VisualRow));
            if (!tmp)
                die("realloc");
            visual = tmp;
        }
        visual[visual_len].is_break = 0;
        visual[visual_len].span_index = i;
        visual_len++;
        line_in_page++;
        if (line_in_page >= page_line_height && i + 1 < wrapped_len) {
            visual[visual_len].is_break = 1;
            visual[visual_len].span_index = 0;
            visual_len++;
            line_in_page = 0;
        }
    }
}

static void refresh_wrap(void) {
    wrap_document();
    build_visual_rows();
}

static size_t span_for_cursor(void) {
    size_t pos = doc_state.cursor;
    for (size_t i = 0; i < wrapped_len; i++) {
        if (pos <= wrapped[i].end) {
            return i;
        }
    }
    return wrapped_len - 1;
}

static size_t visual_index_for_span(size_t span) {
    for (size_t i = 0; i < visual_len; i++) {
        if (!visual[i].is_break && visual[i].span_index == span)
            return i;
    }
    return 0;
}

static void move_cursor_left(void) {
    if (doc_state.cursor > 0)
        doc_state.cursor--;
}

static void move_cursor_right(void) {
    if (doc_state.cursor < doc_state.len)
        doc_state.cursor++;
}

static void move_cursor_home(void) {
    size_t span = span_for_cursor();
    doc_state.cursor = wrapped[span].start;
}

static void move_cursor_end(void) {
    size_t span = span_for_cursor();
    doc_state.cursor = wrapped[span].end;
}

static void move_cursor_up(void) {
    size_t span = span_for_cursor();
    size_t current_visual = visual_index_for_span(span);
    while (current_visual > 0) {
        current_visual--;
        if (!visual[current_visual].is_break)
            break;
    }
    if (visual[current_visual].is_break && current_visual == 0)
        return;
    size_t target_span = visual[current_visual].span_index;
    size_t offset = doc_state.cursor - wrapped[span].start;
    size_t newlen = wrapped[target_span].end - wrapped[target_span].start;
    if (offset > newlen)
        offset = newlen;
    doc_state.cursor = wrapped[target_span].start + offset;
}

static void move_cursor_down(void) {
    size_t span = span_for_cursor();
    size_t current_visual = visual_index_for_span(span);
    while (current_visual + 1 < visual_len) {
        current_visual++;
        if (!visual[current_visual].is_break)
            break;
    }
    if (visual[current_visual].is_break)
        return;
    size_t target_span = visual[current_visual].span_index;
    size_t offset = doc_state.cursor - wrapped[span].start;
    size_t newlen = wrapped[target_span].end - wrapped[target_span].start;
    if (offset > newlen)
        offset = newlen;
    doc_state.cursor = wrapped[target_span].start + offset;
}

static void move_page_up(void) {
    size_t span = span_for_cursor();
    size_t current_visual = visual_index_for_span(span);
    if (current_visual > (size_t)text_rows)
        current_visual -= text_rows;
    else
        current_visual = 0;
    while (current_visual > 0 && visual[current_visual].is_break)
        current_visual--;
    doc_state.cursor = wrapped[visual[current_visual].span_index].start;
}

static void move_page_down(void) {
    size_t span = span_for_cursor();
    size_t current_visual = visual_index_for_span(span);
    current_visual += text_rows;
    if (current_visual >= visual_len)
        current_visual = visual_len - 1;
    while (current_visual + 1 < visual_len && visual[current_visual].is_break)
        current_visual++;
    doc_state.cursor = wrapped[visual[current_visual].span_index].start;
}

static void append_render(char **buf, size_t *len, const char *s) {
    size_t l = strlen(s);
    char *tmp = realloc(*buf, *len + l + 1);
    if (!tmp)
        die("realloc");
    memcpy(tmp + *len, s, l);
    *len += l;
    tmp[*len] = '\0';
    *buf = tmp;
}

static void draw_status_bar(char **out, size_t *len) {
    char line[512];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    size_t words = count_words();
    snprintf(line, sizeof(line), "%-30s %s | %zu words | %s", filename, timebuf, words, status_msg);
    line[screen_cols] = '\0';
    size_t l = strlen(line);
    if ((int)l < screen_cols) {
        memset(line + l, ' ', screen_cols - l);
        line[screen_cols] = '\0';
    }
    append_render(out, len, line);
}

static void draw_top_bar(char **out, size_t *len) {
    char line1[256];
    char line2[256];
    snprintf(line1, sizeof(line1), "[Ctrl+N New] [Ctrl+O Open] [Ctrl+S Save] [Ctrl+G SaveAs] [Ctrl+E Export] [Ctrl+Q Quit]");
    snprintf(line2, sizeof(line2), "[Ctrl+P Page] [Ctrl+F Find] [Ctrl+R Replace] [Ctrl+C Copy] [Ctrl+V Paste] [Ctrl+Z Undo] [Ctrl+Y Redo]");
    char *lines[2] = {line1, line2};
    for (int i = 0; i < 2; i++) {
        size_t l = strlen(lines[i]);
        if ((int)l < screen_cols) {
            memset(lines[i] + l, ' ', screen_cols - l);
            lines[i][screen_cols] = '\0';
        } else {
            lines[i][screen_cols] = '\0';
        }
        append_render(out, len, lines[i]);
    }
}

static void draw_prompt_line(char **out, size_t *len, const char *prompt) {
    char line[256];
    snprintf(line, sizeof(line), "%s", prompt ? prompt : "");
    size_t l = strlen(line);
    if ((int)l < screen_cols) {
        memset(line + l, ' ', screen_cols - l);
        line[screen_cols] = '\0';
    } else {
        line[screen_cols] = '\0';
    }
    append_render(out, len, line);
}

static void draw_rows(char **out, size_t *len) {
    size_t margin_left = 0;
    if (screen_cols > (int)page_text_width)
        margin_left = (screen_cols - page_text_width) / 2;
    size_t line_index = row_offset;
    for (int y = 0; y < text_rows; y++) {
        if (line_index >= visual_len) {
            append_render(out, len, "~");
            if (screen_cols > 1) {
                char *space = malloc(screen_cols);
                if (!space)
                    die("malloc");
                memset(space, ' ', screen_cols - 1);
                space[screen_cols - 1] = '\0';
                append_render(out, len, space);
                free(space);
            }
        } else if (visual[line_index].is_break) {
            char *line = malloc(screen_cols + 1);
            if (!line)
                die("malloc");
            memset(line, ' ', screen_cols);
            size_t dash_len = page_text_width;
            if (dash_len > (size_t)screen_cols)
                dash_len = screen_cols;
            size_t start = margin_left;
            for (size_t i = 0; i < dash_len && start + i < (size_t)screen_cols; i++)
                line[start + i] = '-';
            line[screen_cols] = '\0';
            append_render(out, len, line);
            free(line);
        } else {
            LineSpan span = wrapped[visual[line_index].span_index];
            size_t line_len = span.end - span.start;
            if (line_len > (size_t)page_text_width)
                line_len = page_text_width;
            size_t padding_left = margin_left;
            char *line = malloc(screen_cols + 1);
            if (!line)
                die("malloc");
            memset(line, ' ', screen_cols);
            for (size_t i = 0; i < line_len && padding_left + i < (size_t)screen_cols; i++)
                line[padding_left + i] = doc_state.text[span.start + i];
            line[screen_cols] = '\0';
            append_render(out, len, line);
            free(line);
        }
        line_index++;
    }
}

static void refresh_screen(const char *prompt) {
    clamp_layout();
    refresh_wrap();
    size_t span = span_for_cursor();
    size_t vis_index = visual_index_for_span(span);
    size_t vis_row = vis_index;
    size_t margin_left = 0;
    if (screen_cols > (int)page_text_width)
        margin_left = (screen_cols - page_text_width) / 2;
    size_t cursor_row = vis_row;
    for (size_t i = 0; i < vis_row; i++) {
        if (visual[i].is_break)
            cursor_row--;
    }
    size_t cursor_col = doc_state.cursor - wrapped[span].start + margin_left;
    if (cursor_row < row_offset)
        row_offset = cursor_row;
    else if (cursor_row >= row_offset + (size_t)text_rows)
        row_offset = cursor_row - text_rows + 1;

    char *out = NULL;
    size_t len = 0;
    append_render(&out, &len, "\x1b[?25l");
    append_render(&out, &len, "\x1b[H");

    draw_top_bar(&out, &len);
    draw_rows(&out, &len);
    draw_prompt_line(&out, &len, prompt);
    draw_status_bar(&out, &len);

    char buf[64];
    size_t cursor_screen_row = top_rows + cursor_row - row_offset + 1;
    size_t cursor_screen_col = cursor_col + 1;
    if (cursor_screen_row > (size_t)screen_rows)
        cursor_screen_row = screen_rows;
    if (cursor_screen_col > (size_t)screen_cols)
        cursor_screen_col = screen_cols;
    snprintf(buf, sizeof(buf), "\x1b[%zu;%zuH", cursor_screen_row, cursor_screen_col);
    append_render(&out, &len, buf);
    append_render(&out, &len, "\x1b[?25h");
    write(STDOUT_FILENO, out, len);
    free(out);
}

static int read_key(void) {
    char c;
    while (1) {
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread == 1)
            break;
        else if (nread == 0)
            return -1;
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
                    case '7':
                        return KEY_HOME;
                    case '4':
                    case '8':
                        return KEY_END;
                    case '3':
                        return KEY_DELETE;
                    case '5':
                        return KEY_PAGE_UP;
                    case '6':
                        return KEY_PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return KEY_ARROW_UP;
                case 'B':
                    return KEY_ARROW_DOWN;
                case 'C':
                    return KEY_ARROW_RIGHT;
                case 'D':
                    return KEY_ARROW_LEFT;
                case 'H':
                    return KEY_HOME;
                case 'F':
                    return KEY_END;
                }
            }
        }
        return '\x1b';
    }
    return c;
}

static void prompt(char *buf, size_t bufsize, const char *label) {
    buf[0] = '\0';
    size_t len = 0;
    while (1) {
        refresh_screen(label);
        int c = read_key();
        if (c == '\r') {
            buf[len] = '\0';
            return;
        } else if (c == BACKSPACE || c == CTRL_KEY('h')) {
            if (len > 0)
                len--;
            buf[len] = '\0';
        } else if (!iscntrl(c) && c < 128 && len + 1 < bufsize) {
            buf[len++] = (char)c;
            buf[len] = '\0';
        } else if (c == 27) {
            buf[0] = '\0';
            return;
        }
    }
}

static void ensure_extension(char *name) {
    size_t l = strlen(name);
    if (l < 3 || strcmp(name + l - 3, ".bk") != 0) {
        strncat(name, ".bk", 252 - l);
    }
}

static void set_filename(const char *name) {
    snprintf(filename, sizeof(filename), "%s", name);
    ensure_extension(filename);
}

static void open_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_status("open failed");
        return;
    }
    char header[8];
    if (!fgets(header, sizeof(header), f)) {
        fclose(f);
        set_status("invalid file");
        return;
    }
    if (strncmp(header, "BK1", 3) != 0) {
        fclose(f);
        set_status("bad header");
        return;
    }
    char size_line[16];
    if (!fgets(size_line, sizeof(size_line), f)) {
        fclose(f);
        set_status("missing size");
        return;
    }
    if (strncmp(size_line, "A4", 2) == 0)
        apply_page_size(PAGE_A4);
    else if (strncmp(size_line, "A5", 2) == 0)
        apply_page_size(PAGE_A5);
    else
        apply_page_size(PAGE_A6);
    push_undo();
    clear_stack(&redo_stack);
    doc_state.len = 0;
    doc_state.cursor = 0;
    ensure_capacity(1);
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        ensure_capacity(1);
        doc_state.text[doc_state.len++] = (char)ch;
    }
    fclose(f);
    set_status("opened");
}

static void save_file(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        set_status("save failed");
        return;
    }
    fprintf(f, "BK1\n");
    const char *size = current_page == PAGE_A4 ? "A4" : current_page == PAGE_A5 ? "A5" : "A6";
    fprintf(f, "%s\n", size);
    fwrite(doc_state.text, 1, doc_state.len, f);
    fclose(f);
    set_status("saved");
}

static void export_text(void) {
    char base[256];
    snprintf(base, sizeof(base), "%s", filename);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';
    char outname[256];
    snprintf(outname, sizeof(outname), "%.251s.txt", base);
    FILE *f = fopen(outname, "wb");
    if (!f) {
        set_status("export failed");
        return;
    }
    wrap_document();
    for (size_t i = 0; i < wrapped_len; i++) {
        size_t len = wrapped[i].end - wrapped[i].start;
        fwrite(&doc_state.text[wrapped[i].start], 1, len, f);
        fputc('\n', f);
    }
    fclose(f);
    set_status("exported txt");
}

static void copy_clipboard(void) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        set_status("copy failed");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        execlp("xclip", "xclip", "-selection", "clipboard", NULL);
        _exit(1);
    }
    close(pipefd[0]);
    write(pipefd[1], doc_state.text, doc_state.len);
    close(pipefd[1]);
    waitpid(pid, NULL, 0);
    set_status("copied");
}

static void paste_clipboard(void) {
    FILE *fp = popen("xclip -selection clipboard -o", "r");
    if (!fp) {
        set_status("paste failed");
        return;
    }
    push_undo();
    clear_stack(&redo_stack);
    char buf[256];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) {
        ensure_capacity(r);
        memmove(&doc_state.text[doc_state.cursor + r], &doc_state.text[doc_state.cursor], doc_state.len - doc_state.cursor);
        memcpy(&doc_state.text[doc_state.cursor], buf, r);
        doc_state.cursor += r;
        doc_state.len += r;
    }
    pclose(fp);
    set_status("pasted");
}

static void do_find(void) {
    char query[128];
    prompt(query, sizeof(query), "Find: ");
    if (!query[0])
        return;
    for (size_t i = doc_state.cursor; i + strlen(query) <= doc_state.len; i++) {
        if (memcmp(&doc_state.text[i], query, strlen(query)) == 0) {
            doc_state.cursor = i;
            set_status("found");
            return;
        }
    }
    set_status("not found");
}

static void do_replace(void) {
    char findbuf[128];
    prompt(findbuf, sizeof(findbuf), "Replace: find ");
    if (!findbuf[0])
        return;
    char replbuf[128];
    prompt(replbuf, sizeof(replbuf), "Replace: with ");
    if (!replbuf[0])
        return;
    push_undo();
    clear_stack(&redo_stack);
    size_t qlen = strlen(findbuf);
    size_t rlen = strlen(replbuf);
    for (size_t i = 0; i + qlen <= doc_state.len;) {
        if (memcmp(&doc_state.text[i], findbuf, qlen) == 0) {
            if (qlen != rlen) {
                if (qlen > rlen) {
                    memmove(&doc_state.text[i + rlen], &doc_state.text[i + qlen], doc_state.len - (i + qlen));
                } else {
                    ensure_capacity(rlen - qlen);
                    memmove(&doc_state.text[i + rlen], &doc_state.text[i + qlen], doc_state.len - (i + qlen));
                }
                doc_state.len = doc_state.len - qlen + rlen;
            }
            memcpy(&doc_state.text[i], replbuf, rlen);
            i += rlen;
        } else {
            i++;
        }
    }
    set_status("replaced");
}

static void new_document(void) {
    push_undo();
    clear_stack(&redo_stack);
    doc_state.len = 0;
    doc_state.cursor = 0;
    set_filename("untitled.bk");
    set_status("new");
}

static void cycle_page_size(void) {
    if (current_page == PAGE_A4)
        apply_page_size(PAGE_A5);
    else if (current_page == PAGE_A5)
        apply_page_size(PAGE_A6);
    else
        apply_page_size(PAGE_A4);
    set_status("page size");
}

static void handle_keypress(void) {
    int c = read_key();
    if (c == -1) {
        refresh_screen(NULL);
        return;
    }
    switch (c) {
    case CTRL_KEY('q'):
        disable_raw_mode();
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    case CTRL_KEY('n'):
        new_document();
        break;
    case CTRL_KEY('s'):
        save_file(filename);
        break;
    case CTRL_KEY('g'): {
        char buf[256];
        prompt(buf, sizeof(buf), "Save as: ");
        if (buf[0]) {
            set_filename(buf);
            save_file(filename);
        }
        break;
    }
    case CTRL_KEY('o'): {
        char buf[256];
        prompt(buf, sizeof(buf), "Open: ");
        if (buf[0]) {
            set_filename(buf);
            open_file(filename);
        }
        break;
    }
    case CTRL_KEY('e'):
        export_text();
        break;
    case CTRL_KEY('p'):
        cycle_page_size();
        break;
    case CTRL_KEY('c'):
        copy_clipboard();
        break;
    case CTRL_KEY('v'):
        paste_clipboard();
        break;
    case CTRL_KEY('f'):
        do_find();
        break;
    case CTRL_KEY('r'):
        do_replace();
        break;
    case CTRL_KEY('z'):
        undo();
        break;
    case CTRL_KEY('y'):
        redo();
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
        delete_char();
        break;
    case KEY_DELETE:
        delete_forward();
        break;
    case KEY_ARROW_LEFT:
        move_cursor_left();
        break;
    case KEY_ARROW_RIGHT:
        move_cursor_right();
        break;
    case KEY_ARROW_UP:
        move_cursor_up();
        break;
    case KEY_ARROW_DOWN:
        move_cursor_down();
        break;
    case KEY_HOME:
        move_cursor_home();
        break;
    case KEY_END:
        move_cursor_end();
        break;
    case KEY_PAGE_UP:
        move_page_up();
        break;
    case KEY_PAGE_DOWN:
        move_page_down();
        break;
    case '\r':
        insert_char('\n');
        break;
    default:
        if (!iscntrl(c) && c < 128)
            insert_char((char)c);
        break;
    }
}

int main(void) {
    budostack_apply_terminal_layout();
    clamp_layout();
    apply_page_size(PAGE_A4);
    enable_raw_mode();
    refresh_wrap();
    set_status("");
    while (1) {
        refresh_screen(NULL);
        handle_keypress();
    }
    return 0;
}

