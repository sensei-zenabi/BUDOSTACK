#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

// Target the shared 80x45 layout so the app aligns with the SDL terminal
#define BUDOSTACK_TARGET_COLS 80
#define BUDOSTACK_TARGET_ROWS 45

#include "../lib/terminal_layout.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CTRL_KEY(k) ((k) & 0x1f)
#define BOOK_HISTORY_LIMIT 128
#define BOOK_STATUS_MAX 128
#define BOOK_PROMPT_MAX 256

// Layout constants: two top bar lines + one prompt line, and a two-line bottom bar
#define BOOK_HEADER_ROWS 3
#define BOOK_BOTTOM_ROWS 2

static struct termios orig_termios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }
}

enum BookKey {
    KEY_NULL = 0,
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

static int read_key(void) {
    char c = '\0';
    int nread = (int)read(STDIN_FILENO, &c, 1);
    if (nread == 0) {
        return KEY_NULL;
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) == 0) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '3': return KEY_DELETE;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        }
        return KEY_NULL;
    }
    return (int)c;
}

struct PageSize {
    const char *name;
    int target_cols;
    int target_rows;
    int margin_cols;
    int margin_rows;
};

struct WrappedLine {
    size_t start;
    size_t len;
};

struct HistoryEntry {
    char *text;
    size_t len;
    size_t cursor;
    char filename[PATH_MAX];
    int page_index;
};

struct BookState {
    char *text;
    size_t len;
    size_t cap;
    size_t cursor;

    struct WrappedLine *lines;
    size_t line_count;

    int rows;
    int cols;
    int content_rows;
    int page_left;
    int page_width;
    int page_height;
    int page_index;

    int row_offset;
    int preferred_col;

    char status[BOOK_STATUS_MAX];
    char prompt[BOOK_PROMPT_MAX];
    int prompt_active;

    char filename[PATH_MAX];
    size_t word_count;

    struct HistoryEntry undo[BOOK_HISTORY_LIMIT];
    struct HistoryEntry redo[BOOK_HISTORY_LIMIT];
    int undo_len;
    int redo_len;
};

// A-series sizes at 60 DPI with 8x8 font (values rounded up)
static const struct PageSize PAGE_SIZES[] = {
    {"A4", 62, 88, 7, 1},
    {"A5", 44, 62, 7, 1},
    {"A6", 31, 44, 7, 1}
};

static void render(struct BookState *state);

static void free_history_entry(struct HistoryEntry *e) {
    if (e->text) {
        free(e->text);
        e->text = NULL;
    }
}

static void clear_history(struct BookState *state) {
    for (int i = 0; i < state->undo_len; i++) {
        free_history_entry(&state->undo[i]);
    }
    for (int i = 0; i < state->redo_len; i++) {
        free_history_entry(&state->redo[i]);
    }
    state->undo_len = 0;
    state->redo_len = 0;
}

static void push_history(struct HistoryEntry *stack, int *len, const struct BookState *state) {
    if (*len >= BOOK_HISTORY_LIMIT) {
        free_history_entry(&stack[0]);
        memmove(&stack[0], &stack[1], sizeof(struct HistoryEntry) * (BOOK_HISTORY_LIMIT - 1));
        *len = BOOK_HISTORY_LIMIT - 1;
    }
    struct HistoryEntry *entry = &stack[*len];
    entry->len = state->len;
    entry->cursor = state->cursor;
    entry->page_index = state->page_index;
    strncpy(entry->filename, state->filename, sizeof(entry->filename));
    entry->filename[sizeof(entry->filename) - 1] = '\0';
    entry->text = malloc(state->len + 1);
    if (entry->text) {
        memcpy(entry->text, state->text, state->len);
        entry->text[state->len] = '\0';
    }
    (*len)++;
}

static void restore_history_entry(struct BookState *state, struct HistoryEntry *entry) {
    free(state->text);
    state->text = malloc(entry->len + 1);
    if (state->text == NULL) {
        state->text = NULL;
        state->len = 0;
        state->cap = 0;
        state->cursor = 0;
        return;
    }
    memcpy(state->text, entry->text, entry->len);
    state->text[entry->len] = '\0';
    state->len = entry->len;
    state->cap = entry->len + 1;
    state->cursor = entry->cursor;
    state->page_index = entry->page_index;
    strncpy(state->filename, entry->filename, sizeof(state->filename));
    state->filename[sizeof(state->filename) - 1] = '\0';
}

static void push_undo(struct BookState *state) {
    push_history(state->undo, &state->undo_len, state);
    for (int i = 0; i < state->redo_len; i++) {
        free_history_entry(&state->redo[i]);
    }
    state->redo_len = 0;
}

static void apply_undo(struct BookState *state) {
    if (state->undo_len == 0) {
        return;
    }
    push_history(state->redo, &state->redo_len, state);
    state->undo_len--;
    restore_history_entry(state, &state->undo[state->undo_len]);
}

static void apply_redo(struct BookState *state) {
    if (state->redo_len == 0) {
        return;
    }
    push_history(state->undo, &state->undo_len, state);
    state->redo_len--;
    restore_history_entry(state, &state->redo[state->redo_len]);
}

static void ensure_capacity(struct BookState *state, size_t needed) {
    if (needed <= state->cap) {
        return;
    }
    size_t new_cap = state->cap == 0 ? 128u : state->cap;
    while (new_cap < needed) {
        new_cap *= 2u;
    }
    char *new_buf = realloc(state->text, new_cap);
    if (!new_buf) {
        return;
    }
    state->text = new_buf;
    state->cap = new_cap;
}

static void update_word_count(struct BookState *state) {
    int in_word = 0;
    size_t count = 0u;
    for (size_t i = 0; i < state->len; i++) {
        if (isspace((unsigned char)state->text[i])) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    state->word_count = count;
}

static char *strcasestr_local(const char *haystack, const char *needle) {
    if (!haystack || !needle) {
        return NULL;
    }
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return (char *)haystack;
    }
    for (const char *p = haystack; *p; p++) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)needle[0])) {
            if (strncasecmp(p, needle, nlen) == 0) {
                return (char *)p;
            }
        }
    }
    return NULL;
}

static void insert_char(struct BookState *state, char c) {
    push_undo(state);
    ensure_capacity(state, state->len + 2);
    if (state->text == NULL) {
        return;
    }
    memmove(&state->text[state->cursor + 1], &state->text[state->cursor], state->len - state->cursor);
    state->text[state->cursor] = c;
    state->len++;
    state->cursor++;
    state->text[state->len] = '\0';
    update_word_count(state);
}

static void backspace_char(struct BookState *state) {
    if (state->cursor == 0 || state->len == 0) {
        return;
    }
    push_undo(state);
    memmove(&state->text[state->cursor - 1], &state->text[state->cursor], state->len - state->cursor);
    state->len--;
    state->cursor--;
    state->text[state->len] = '\0';
    update_word_count(state);
}

static void delete_char(struct BookState *state) {
    if (state->cursor >= state->len) {
        return;
    }
    push_undo(state);
    memmove(&state->text[state->cursor], &state->text[state->cursor + 1], state->len - state->cursor - 1);
    state->len--;
    state->text[state->len] = '\0';
    update_word_count(state);
}

static void set_status(struct BookState *state, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state->status, sizeof(state->status), fmt, ap);
    va_end(ap);
}

static void wrap_text(struct BookState *state) {
    free(state->lines);
    state->lines = NULL;
    state->line_count = 0u;

    if (state->page_width <= 0) {
        return;
    }

    size_t cap = 128u;
    state->lines = malloc(sizeof(struct WrappedLine) * cap);
    if (!state->lines) {
        return;
    }

    size_t start = 0u;
    size_t i = 0u;
    int col = 0;
    size_t count = 0u;
    size_t last_space = (size_t)(-1);

    while (i < state->len) {
        char c = state->text[i];
        if (c == '\n') {
            if (count >= cap) {
                cap *= 2u;
                state->lines = realloc(state->lines, sizeof(struct WrappedLine) * cap);
                if (!state->lines) return;
            }
            state->lines[count].start = start;
            state->lines[count].len = i - start;
            count++;
            i++;
            start = i;
            col = 0;
            last_space = (size_t)(-1);
            continue;
        }
        if (c == ' ') {
            last_space = i;
        }
        col++;
        if (col > state->page_width) {
            size_t break_pos = (last_space != (size_t)(-1) && last_space >= start) ? last_space + 1u : i;
            size_t line_len = (last_space != (size_t)(-1) && last_space >= start) ? (last_space - start) : (i - start);
            if (count >= cap) {
                cap *= 2u;
                state->lines = realloc(state->lines, sizeof(struct WrappedLine) * cap);
                if (!state->lines) return;
            }
            state->lines[count].start = start;
            state->lines[count].len = line_len;
            count++;
            start = break_pos;
            i = break_pos;
            col = 0;
            last_space = (size_t)(-1);
            continue;
        }
        i++;
    }

    if (start <= state->len) {
        if (count >= cap) {
            cap *= 2u;
            state->lines = realloc(state->lines, sizeof(struct WrappedLine) * cap);
            if (!state->lines) return;
        }
        state->lines[count].start = start;
        state->lines[count].len = state->len - start;
        count++;
    }

    state->line_count = count;
}

static void update_dimensions(struct BookState *state) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        state->cols = BUDOSTACK_TARGET_COLS;
        state->rows = BUDOSTACK_TARGET_ROWS;
    } else {
        state->cols = ws.ws_col;
        state->rows = ws.ws_row;
    }
    budostack_clamp_terminal_size(&state->rows, &state->cols);

    int available_rows = state->rows - BOOK_HEADER_ROWS - BOOK_BOTTOM_ROWS;
    if (available_rows < 1) available_rows = 1;
    const struct PageSize *ps = &PAGE_SIZES[state->page_index];

    int max_page_width = state->cols - ps->margin_cols * 2;
    if (max_page_width < 4) max_page_width = 4;

    int page_width = ps->target_cols;
    if (page_width > max_page_width) {
        page_width = max_page_width;
    }
    if (page_width < 4) {
        page_width = 4;
    }

    int page_height = ps->target_rows;
    if (page_height < 4) {
        page_height = 4;
    }

    state->page_width = page_width;
    state->page_left = (state->cols - state->page_width) / 2;
    state->page_height = page_height;
    state->content_rows = available_rows;
    wrap_text(state);
}

static size_t line_for_cursor(const struct BookState *state, int *out_col) {
    if (state->line_count == 0) {
        if (out_col) *out_col = 0;
        return 0u;
    }
    for (size_t i = 0; i < state->line_count; i++) {
        size_t start = state->lines[i].start;
        size_t end = start + state->lines[i].len;
        if (state->cursor < end || (state->cursor == end && state->cursor == state->len)) {
            if (out_col) {
                int col = (int)(state->cursor - start);
                if (col < 0) col = 0;
                *out_col = col;
            }
            return i;
        }
        if (state->cursor == end && (i + 1 == state->line_count || state->lines[i + 1].start > end)) {
            if (out_col) *out_col = (int)(state->lines[i].len);
            return i;
        }
    }
    if (out_col) *out_col = 0;
    return state->line_count - 1;
}

static void scroll_to_cursor(struct BookState *state) {
    int col = 0;
    size_t line = line_for_cursor(state, &col);
    if ((int)line < state->row_offset) {
        state->row_offset = (int)line;
    } else if ((int)line >= state->row_offset + state->content_rows) {
        state->row_offset = (int)line - state->content_rows + 1;
    }
    state->preferred_col = col;
}

static void move_cursor_up(struct BookState *state) {
    int col = state->preferred_col;
    size_t line = line_for_cursor(state, NULL);
    if (line == 0) {
        state->cursor = 0;
        return;
    }
    size_t target_line = line - 1;
    size_t start = state->lines[target_line].start;
    size_t len = state->lines[target_line].len;
    size_t offset = (size_t)col;
    if (offset > len) offset = len;
    state->cursor = start + offset;
}

static void move_cursor_down(struct BookState *state) {
    int col = state->preferred_col;
    size_t line = line_for_cursor(state, NULL);
    if (line + 1 >= state->line_count) {
        state->cursor = state->len;
        return;
    }
    size_t target_line = line + 1;
    size_t start = state->lines[target_line].start;
    size_t len = state->lines[target_line].len;
    size_t offset = (size_t)col;
    if (offset > len) offset = len;
    state->cursor = start + offset;
}

static void move_cursor_left(struct BookState *state) {
    if (state->cursor > 0) {
        state->cursor--;
    }
}

static void move_cursor_right(struct BookState *state) {
    if (state->cursor < state->len) {
        state->cursor++;
    }
}

static void move_cursor_home(struct BookState *state) {
    size_t line_idx = line_for_cursor(state, NULL);
    state->cursor = state->lines[line_idx].start;
}

static void move_cursor_end(struct BookState *state) {
    size_t line_idx = line_for_cursor(state, NULL);
    state->cursor = state->lines[line_idx].start + state->lines[line_idx].len;
}

static char *prompt_user(struct BookState *state, const char *label) {
    memset(state->prompt, 0, sizeof(state->prompt));
    size_t len = 0u;
    state->prompt_active = 1;
    int label_limit = (int)(sizeof(state->status) / 2);
    int prompt_limit = (int)sizeof(state->status) - label_limit - 4;
    if (label_limit < 0) label_limit = 0;
    if (prompt_limit < 0) prompt_limit = 0;
    snprintf(state->status, sizeof(state->status), "%.*s %.*s", label_limit, label, prompt_limit, state->prompt);
    render(state);
    while (1) {
        int key = read_key();
        if (key == KEY_NULL) {
            render(state);
            continue;
        }
        if (key == '\r') {
            state->prompt_active = 0;
            return strdup(state->prompt);
        } else if (key == 27) {
            state->prompt_active = 0;
            return NULL;
        } else if (key == CTRL_KEY('h') || key == 127) {
            if (len > 0) {
                len--;
                state->prompt[len] = '\0';
            }
        } else if (isprint(key) && len + 1 < sizeof(state->prompt)) {
            state->prompt[len++] = (char)key;
            state->prompt[len] = '\0';
        }
        snprintf(state->status, sizeof(state->status), "%.*s %.*s", label_limit, label, prompt_limit, state->prompt);
        render(state);
    }
}

static void ensure_extension(char *filename, size_t buflen, const char *ext) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(ext);
    if (len < ext_len || strcasecmp(filename + len - ext_len, ext) != 0) {
        if (len + ext_len + 1 < buflen) {
            strcat(filename, ext);
        }
    }
}

static int save_file(struct BookState *state, int save_as) {
    char path[PATH_MAX];
    if (save_as || state->filename[0] == '\0') {
        char *input = prompt_user(state, "Save as:");
        if (!input) {
            set_status(state, "Save cancelled");
            return -1;
        }
        strncpy(path, input, sizeof(path));
        path[sizeof(path) - 1] = '\0';
        free(input);
    } else {
        strncpy(path, state->filename, sizeof(path));
        path[sizeof(path) - 1] = '\0';
    }
    if (path[0] == '\0') {
        set_status(state, "No filename provided");
        return -1;
    }
    ensure_extension(path, sizeof(path), ".bk");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        set_status(state, "Save failed: %s", strerror(errno));
        return -1;
    }
    char header[32];
    int header_len = snprintf(header, sizeof(header), "BK1 %s\n", PAGE_SIZES[state->page_index].name);
    if (write(fd, header, (size_t)header_len) != header_len) {
        close(fd);
        set_status(state, "Save failed: header");
        return -1;
    }
    if (state->len > 0 && write(fd, state->text, state->len) != (ssize_t)state->len) {
        close(fd);
        set_status(state, "Save failed: data");
        return -1;
    }
    close(fd);
    strncpy(state->filename, path, sizeof(state->filename));
    state->filename[sizeof(state->filename) - 1] = '\0';
    set_status(state, "Saved %s", state->filename);
    return 0;
}

static int load_file(struct BookState *state) {
    char *input = prompt_user(state, "Open:");
    if (!input) {
        set_status(state, "Open cancelled");
        return -1;
    }
    char path[PATH_MAX];
    strncpy(path, input, sizeof(path));
    path[sizeof(path) - 1] = '\0';
    free(input);
    ensure_extension(path, sizeof(path), ".bk");
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        set_status(state, "Open failed: %s", strerror(errno));
        return -1;
    }
    char header[32];
    if (!fgets(header, sizeof(header), fp)) {
        fclose(fp);
        set_status(state, "Invalid book file");
        return -1;
    }
    if (strncmp(header, "BK1", 3) != 0) {
        fclose(fp);
        set_status(state, "Missing BK1 header");
        return -1;
    }
    char page_name[8] = {0};
    if (sscanf(header, "BK1 %7s", page_name) == 1) {
        for (size_t i = 0; i < sizeof(PAGE_SIZES) / sizeof(PAGE_SIZES[0]); i++) {
            if (strcasecmp(page_name, PAGE_SIZES[i].name) == 0) {
                state->page_index = (int)i;
                break;
            }
        }
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz < 0) sz = 0;
    fseek(fp, (long)strlen(header), SEEK_SET);
    size_t content_size = (size_t)(sz - (long)strlen(header));
    char *buffer = malloc(content_size + 1);
    if (!buffer) {
        fclose(fp);
        set_status(state, "Memory error");
        return -1;
    }
    size_t read_bytes = fread(buffer, 1, content_size, fp);
    buffer[read_bytes] = '\0';
    fclose(fp);

    push_undo(state);
    free(state->text);
    state->text = buffer;
    state->len = read_bytes;
    state->cap = content_size + 1;
    state->cursor = 0;
    state->row_offset = 0;
    strncpy(state->filename, path, sizeof(state->filename));
    state->filename[sizeof(state->filename) - 1] = '\0';
    update_word_count(state);
    set_status(state, "Opened %s", state->filename);
    return 0;
}

static void new_file(struct BookState *state) {
    push_undo(state);
    free(state->text);
    state->text = calloc(1, 1);
    state->len = 0;
    state->cap = 1;
    state->cursor = 0;
    state->row_offset = 0;
    state->filename[0] = '\0';
    update_word_count(state);
    set_status(state, "New book");
}

static void export_text(const struct BookState *state) {
    char base[PATH_MAX];
    if (state->filename[0] == '\0') {
        snprintf(base, sizeof(base), "untitled");
    } else {
        strncpy(base, state->filename, sizeof(base));
        base[sizeof(base) - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%.*s.txt", (int)sizeof(path) - 5, base);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return;
    }
    size_t line_index = 0u;
    size_t lines_written = 0u;
    int lines_in_page = 0;
    while (line_index < state->line_count) {
        const struct WrappedLine *line = &state->lines[line_index];
        fwrite(&state->text[line->start], 1, line->len, fp);
        fputc('\n', fp);
        lines_written++;
        lines_in_page++;
        if (lines_in_page >= state->page_height) {
            for (int i = 0; i < state->page_width; i++) {
                fputc('-', fp);
            }
            fputc('\n', fp);
            lines_in_page = 0;
        }
        line_index++;
    }
    (void)lines_written;
    fclose(fp);
}

static void copy_to_clipboard(const struct BookState *state) {
    FILE *fp = popen("xclip -selection clipboard", "w");
    if (!fp) {
        return;
    }
    if (state->len > 0 && fwrite(state->text, 1, state->len, fp) != state->len) {
        perror("fwrite");
    }
    pclose(fp);
}

static void paste_from_clipboard(struct BookState *state) {
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!fp) {
        return;
    }
    push_undo(state);
    char buffer[256];
    size_t total = 0u;
    while (!feof(fp)) {
        size_t n = fread(buffer, 1, sizeof(buffer), fp);
        if (n > 0) {
            ensure_capacity(state, state->len + n + 1);
            memmove(&state->text[state->cursor + n], &state->text[state->cursor], state->len - state->cursor);
            memcpy(&state->text[state->cursor], buffer, n);
            state->cursor += n;
            state->len += n;
            total += n;
        }
    }
    state->text[state->len] = '\0';
    pclose(fp);
    if (total > 0u) {
        update_word_count(state);
    }
}

static void find_text(struct BookState *state) {
    char *term = prompt_user(state, "Find:");
    if (!term || term[0] == '\0') {
        free(term);
        set_status(state, "Find cancelled");
        return;
    }
    char *match = strcasestr_local(state->text, term);
    if (match) {
        state->cursor = (size_t)(match - state->text);
        set_status(state, "Found '%s'", term);
    } else {
        set_status(state, "'%s' not found", term);
    }
    free(term);
}

static void replace_text(struct BookState *state) {
    char *find = prompt_user(state, "Replace find:");
    if (!find || find[0] == '\0') {
        free(find);
        set_status(state, "Replace cancelled");
        return;
    }
    char *repl = prompt_user(state, "Replace with:");
    if (!repl) {
        free(find);
        set_status(state, "Replace cancelled");
        return;
    }
    push_undo(state);
    size_t find_len = strlen(find);
    size_t repl_len = strlen(repl);
    if (find_len == 0) {
        free(find);
        free(repl);
        return;
    }
    size_t i = 0u;
    while (i + find_len <= state->len) {
        if (strncasecmp(&state->text[i], find, find_len) == 0) {
            ensure_capacity(state, state->len - find_len + repl_len + 1);
            memmove(&state->text[i + repl_len], &state->text[i + find_len], state->len - i - find_len);
            memcpy(&state->text[i], repl, repl_len);
            state->len = state->len - find_len + repl_len;
            i += repl_len;
        } else {
            i++;
        }
    }
    state->text[state->len] = '\0';
    update_word_count(state);
    free(find);
    free(repl);
    wrap_text(state);
    set_status(state, "Replace done");
}

static void draw_bars(const struct BookState *state) {
    char datebuf[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", tm);

    printf("\x1b[H");
    printf("\x1b[?25l");
    printf("\x1b[0m");

    // Top bar line 1
    printf("\x1b[7m");
    printf("%-*.*s", state->cols, state->cols, " Ctrl+N New  | ^+O Open  | ^+S Save | ^+G Save As | ^+E Export  | ^+Q Quit |");
    printf("\x1b[0m\r\n");

    // Top bar line 2
    printf("\x1b[7m");
    char top_line[256];
    snprintf(top_line, sizeof(top_line), " Ctrl+F Find | ^+R Repl. | ^+Z Undo | ^+Y Redo    | ^+P Page %s |", PAGE_SIZES[state->page_index].name);
    printf("%-*.*s", state->cols, state->cols, top_line);
    printf("\x1b[0m\r\n");

    // Text area rendering handled separately
    // Prompt line (blank by default)
    printf("\x1b[7m");
    if (state->prompt_active) {
        char prompt_line[BOOK_PROMPT_MAX + 32];
        snprintf(prompt_line, sizeof(prompt_line), " %s", state->status);
        printf("%-*.*s", state->cols, state->cols, prompt_line);
    } else {
        printf("%-*.*s", state->cols, state->cols, "");
    }
    printf("\x1b[0m\r\n");

    // Position text cursor to content start
}

static void draw_content(const struct BookState *state) {
    int content_rows = state->content_rows;
    int current_row = 0;
    int page_line_counter = state->page_height == 0 ? 0 : state->row_offset % state->page_height;
    size_t line_index = (size_t)state->row_offset;
    while (current_row < content_rows) {
        if (line_index >= state->line_count) {
            printf("\x1b[2K\r\n");
            current_row++;
            continue;
        }
        if (page_line_counter >= state->page_height) {
            static const char label[] = " page break ";
            int left_pad = state->page_left;
            if (left_pad < 0) left_pad = 0;
            printf("\x1b[2K\r");
            if (left_pad > 0) {
                printf("\x1b[%dC", left_pad);
            }
            printf("\x1b[2m");
            int label_len = (int)strlen(label);
            if (state->page_width > label_len + 2) {
                int start = (state->page_width - label_len) / 2;
                for (int i = 0; i < start; i++) {
                    putchar('-');
                }
                fputs(label, stdout);
                for (int i = start + label_len; i < state->page_width; i++) {
                    putchar('-');
                }
            } else {
                for (int i = 0; i < state->page_width; i++) {
                    putchar('-');
                }
            }
            printf("\x1b[0m\r\n");
            current_row++;
            page_line_counter = 0;
            continue;
        }
        const struct WrappedLine *line = &state->lines[line_index];
        int left_pad = state->page_left;
        if (left_pad < 0) left_pad = 0;
        printf("\x1b[2K\r");
        if (left_pad > 0) {
            printf("\x1b[%dC", left_pad);
        }
        fwrite(&state->text[line->start], 1, line->len, stdout);
        putchar('\r');
        putchar('\n');
        current_row++;
        line_index++;
        page_line_counter++;
    }
}

static void draw_bottom_bar(const struct BookState *state) {
    char datebuf[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", tm);

    printf("\x1b[7m");
    char line1[256];
    const char *raw_name = state->filename[0] ? state->filename : "(untitled)";
    char namebuf[128];
    snprintf(namebuf, sizeof(namebuf), "%.*s", (int)sizeof(namebuf) - 1, raw_name);
    snprintf(line1, sizeof(line1), " File: %s | Page: %s (%dx%d)", namebuf,
             PAGE_SIZES[state->page_index].name, state->page_width, state->page_height);
    printf("%-*.*s", state->cols, state->cols, line1);
    printf("\x1b[0m\r\n");

    printf("\x1b[7m");
    char line2[256];
    snprintf(line2, sizeof(line2), " %s | Words: %zu | %s", datebuf, state->word_count, state->status);
    printf("%-*.*s", state->cols, state->cols, line2);
    printf("\x1b[0m\r");
}

static void render(struct BookState *state) {
    update_dimensions(state);
    scroll_to_cursor(state);
    printf("\x1b[?25l");
    printf("\x1b[H");
    draw_bars(state);
    printf("\x1b[%d;1H", BOOK_HEADER_ROWS + 1);
    draw_content(state);
    draw_bottom_bar(state);

    int col = 0;
    size_t line = line_for_cursor(state, &col);
    int cursor_row = BOOK_HEADER_ROWS + 1 + (int)(line - state->row_offset);
    int cursor_col = state->page_left + col + 1;
    if (cursor_row < 1) cursor_row = 1;
    printf("\x1b[%d;%dH", cursor_row, cursor_col);
    printf("\x1b[?25h");
    fflush(stdout);
}

static void page_cycle(struct BookState *state) {
    state->page_index = (state->page_index + 1) % (int)(sizeof(PAGE_SIZES) / sizeof(PAGE_SIZES[0]));
    wrap_text(state);
    set_status(state, "Page size %s", PAGE_SIZES[state->page_index].name);
}

static void page_jump(struct BookState *state, int direction) {
    if (state->line_count == 0) {
        return;
    }
    int step = state->content_rows;
    int col = state->preferred_col;
    size_t line = line_for_cursor(state, NULL);
    int target = (int)line + (direction * step);
    if (target < 0) target = 0;
    if (target >= (int)state->line_count) target = (int)state->line_count - 1;
    size_t start = state->lines[target].start;
    size_t len = state->lines[target].len;
    size_t offset = (size_t)col;
    if (offset > len) offset = len;
    state->cursor = start + offset;
}

int main(void) {
    budostack_apply_terminal_layout();
    enable_raw_mode();
    struct BookState state = {0};
    state.text = calloc(1, 1);
    state.cap = 1;
    state.len = 0;
    state.cursor = 0;
    state.page_index = 0;
    state.preferred_col = 0;
    state.status[0] = '\0';
    state.prompt_active = 0;
    update_word_count(&state);
    update_dimensions(&state);

    int running = 1;
    while (running) {
        render(&state);
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 200000};
        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ready == 0) {
            continue;
        }
        int c = read_key();
        switch (c) {
            case CTRL_KEY('q'):
                running = 0;
                break;
            case CTRL_KEY('n'):
                new_file(&state);
                break;
            case CTRL_KEY('o'):
                load_file(&state);
                break;
            case CTRL_KEY('s'):
                save_file(&state, 0);
                break;
            case CTRL_KEY('g'):
                save_file(&state, 1);
                break;
            case CTRL_KEY('e'):
                export_text(&state);
                set_status(&state, "Exported text");
                break;
            case CTRL_KEY('f'):
                find_text(&state);
                break;
            case CTRL_KEY('r'):
                replace_text(&state);
                break;
            case CTRL_KEY('z'):
                apply_undo(&state);
                update_word_count(&state);
                break;
            case CTRL_KEY('y'):
                apply_redo(&state);
                update_word_count(&state);
                break;
            case CTRL_KEY('c'):
                copy_to_clipboard(&state);
                set_status(&state, "Copied to clipboard");
                break;
            case CTRL_KEY('v'):
                paste_from_clipboard(&state);
                set_status(&state, "Pasted clipboard");
                break;
            case CTRL_KEY('p'):
                page_cycle(&state);
                break;
            case KEY_HOME:
                move_cursor_home(&state);
                break;
            case KEY_END:
                move_cursor_end(&state);
                break;
            case KEY_ARROW_LEFT:
                move_cursor_left(&state);
                break;
            case KEY_ARROW_RIGHT:
                move_cursor_right(&state);
                break;
            case KEY_ARROW_UP:
                move_cursor_up(&state);
                break;
            case KEY_ARROW_DOWN:
                move_cursor_down(&state);
                break;
            case KEY_PAGE_UP:
                page_jump(&state, -1);
                break;
            case KEY_PAGE_DOWN:
                page_jump(&state, 1);
                break;
            case KEY_DELETE:
                delete_char(&state);
                break;
            case 127:
                backspace_char(&state);
                break;
            case '\r':
                insert_char(&state, '\n');
                break;
            default:
                if (isprint(c)) {
                    insert_char(&state, (char)c);
                }
                break;
        }
        wrap_text(&state);
    }

    printf("\x1b[2J\x1b[H\x1b[0m\x1b[?25h");
    clear_history(&state);
    free(state.text);
    free(state.lines);
    return 0;
}
