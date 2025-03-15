#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
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
#include <string.h>
#include <fcntl.h>
#include <signal.h>

/* Editor version */
#define EDITOR_VERSION "0.1-micro-like"

/* Key definitions */
#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127

/* Maximum lengths for command buffer and status messages */
#define CMD_BUF_SIZE 128
#define STATUS_MSG_SIZE 80

/* Undo history capacity */
#define UNDO_HISTORY_MAX 100

/* Enumeration for arrow keys */
enum EditorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

/* Data structure for a line in the editor.
   Added 'modified' field: 0 = unchanged, 1 = modified. */
typedef struct {
    int size;       // length in bytes
    char *chars;    // UTF-8 encoded text
    int modified;   // flag indicating if this row has been changed
} EditorLine;

/* Global editor state */
struct EditorConfig {
    int cx, cy;       // cursor position (logical columns/rows)
    int screenrows;   // total terminal rows
    int screencols;   // total terminal columns
    int rowoff;       // vertical scroll offset (first row displayed)
    int coloff;       // horizontal scroll offset (for text area only)
    int numrows;      // number of rows in the file
    EditorLine *row;  // array of rows
    char *filename;   // open file name
    int dirty;        // unsaved changes flag
    struct termios orig_termios; // original terminal settings

    /* Command mode fields */
    int in_command_mode;
    char command_buffer[CMD_BUF_SIZE];
    int command_length;
    char status_message[STATUS_MSG_SIZE];
    int textrows; // text area rows (screenrows minus status/command bars)
} E;

/* Undo state structure */
typedef struct {
    int cx, cy;
    int numrows;
    EditorLine *row; // deep copy of rows
} UndoState;

/* Global undo history (snapshot-based) */
UndoState *undo_history[UNDO_HISTORY_MAX];
int undo_history_len = 0;

/* Function prototypes */
void die(const char *s);
void disableRawMode();
void enableRawMode();
int editorReadKey();
int getWindowSize(int *rows, int *cols);
void editorRefreshScreen();
int getRowNumWidth(void);
int editorDisplayWidth(const char *s);
int editorRowCxToByteIndex(EditorLine *row, int cx);
void editorRenderRow(EditorLine *row, int avail);
void editorDrawRows(int rn_width);
void editorDrawStatusBar();
void editorDrawCommandBar();
void editorProcessKeypress();
void processCommand();
void editorOpen(const char *filename);
void editorSave();
void editorAppendLine(char *s, size_t len);
void editorInsertChar(int c);
void editorInsertUTF8(const char *s, int len);
void editorInsertNewline();
void editorDelChar();
/* Undo functions */
void push_undo_state(void);
void free_undo_state(UndoState *state);
void pop_undo_state(void);

/* --- Helper Functions --- */

/* getRowNumWidth: Compute margin width based on number of rows */
int getRowNumWidth(void) {
    int n = E.numrows;
    int digits = 1;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits + 1; // extra space as separator
}

/* editorDisplayWidth: Compute display width of a UTF-8 string */
int editorDisplayWidth(const char *s) {
    int width = 0;
    size_t bytes;
    wchar_t wc;
    while (*s) {
        bytes = mbrtowc(&wc, s, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            bytes = 1;
            wc = *s;
        }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        width += w;
        s += bytes;
    }
    return width;
}

/* editorRowCxToByteIndex: Map logical column (display columns) to byte index */
int editorRowCxToByteIndex(EditorLine *row, int cx) {
    int cur_width = 0;
    int index = 0;
    size_t bytes;
    wchar_t wc;
    while (index < row->size) {
        bytes = mbrtowc(&wc, row->chars + index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            bytes = 1;
            wc = row->chars[index];
        }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (cur_width + w > cx)
            break;
        cur_width += w;
        index += bytes;
    }
    return index;
}

/* editorRenderRow: Render a row starting at E.coloff and write up to avail columns */
void editorRenderRow(EditorLine *row, int avail) {
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
        logical_width += w;
        byte_index += bytes;
    }
    buffer[buf_index] = '\0';
    write(STDOUT_FILENO, buffer, buf_index);
}

/* --- Undo Functions --- */

/* push_undo_state: Save a deep copy of the current state */
void push_undo_state(void) {
    UndoState *state = malloc(sizeof(UndoState));
    if (!state)
        die("malloc undo state");
    state->cx = E.cx;
    state->cy = E.cy;
    state->numrows = E.numrows;
    state->row = malloc(sizeof(EditorLine) * E.numrows);
    if (!state->row)
        die("malloc undo rows");
    for (int i = 0; i < E.numrows; i++) {
        state->row[i].size = E.row[i].size;
        state->row[i].chars = malloc(E.row[i].size + 1);
        if (!state->row[i].chars)
            die("malloc undo row char");
        memcpy(state->row[i].chars, E.row[i].chars, E.row[i].size);
        state->row[i].chars[E.row[i].size] = '\0';
    }
    if (undo_history_len == UNDO_HISTORY_MAX) {
        free_undo_state(undo_history[0]);
        for (int i = 1; i < UNDO_HISTORY_MAX; i++)
            undo_history[i - 1] = undo_history[i];
        undo_history_len--;
    }
    undo_history[undo_history_len++] = state;
}

/* free_undo_state: Free a saved undo state */
void free_undo_state(UndoState *state) {
    for (int i = 0; i < state->numrows; i++)
        free(state->row[i].chars);
    free(state->row);
    free(state);
}

/* pop_undo_state: Restore the last saved state */
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
    if (!E.row)
        die("malloc restore rows");
    for (int i = 0; i < E.numrows; i++) {
        E.row[i].size = state->row[i].size;
        E.row[i].chars = malloc(E.row[i].size + 1);
        if (!E.row[i].chars)
            die("malloc restore row char");
        memcpy(E.row[i].chars, state->row[i].chars, E.row[i].size);
        E.row[i].chars[E.row[i].size] = '\0';
        /* Also restore modified flag as unchanged */
        E.row[i].modified = 0;
    }
    E.cx = state->cx;
    E.cy = state->cy;
    free_undo_state(state);
}

/* --- Terminal Setup Functions --- */

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

void enableRawMode() {
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
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/* --- Input Functions --- */

int editorReadKey() {
    char c;
    int nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) == 0)
        ;
    if (nread == -1 && errno != EAGAIN)
        die("read");
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
            read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                char seq2;
                read(STDIN_FILENO, &seq2, 1);
                return '\x1b';
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }
        }
        return '\x1b';
    }
    return c;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/* --- Drawing Routines --- */

/* editorDrawRows: Draw text rows with row numbers and a change indicator.
   The indicator is drawn as a red block (using red background) in the last column if the row is modified. */
void editorDrawRows(int rn_width) {
    int text_width = E.screencols - rn_width - 1; // reserve 1 column for indicator
    char numbuf[16];
    for (int y = 0; y < E.textrows; y++) {
        int file_row = E.rowoff + y;
        if (file_row < E.numrows) {
            int rn = file_row + 1;
            int num_len = snprintf(numbuf, sizeof(numbuf), "%*d ", rn_width - 1, rn);
            write(STDOUT_FILENO, numbuf, num_len);
            // Render row text
            editorRenderRow(&E.row[file_row], text_width);
            // Calculate printed width
            int printed_width = editorDisplayWidth(E.row[file_row].chars) - E.coloff;
            if (printed_width < 0) printed_width = 0;
            if (printed_width > text_width) printed_width = text_width;
            // Pad remaining spaces
            for (int i = printed_width; i < text_width; i++) {
                write(STDOUT_FILENO, " ", 1);
            }
            // Draw change indicator in last column:
            if (E.row[file_row].modified)
                write(STDOUT_FILENO, "\x1b[41m \x1b[0m", 10);  // red background block
            else
                write(STDOUT_FILENO, " ", 1);
        } else {
            for (int i = 0; i < rn_width; i++)
                write(STDOUT_FILENO, " ", 1);
            write(STDOUT_FILENO, "~", 1);
        }
        write(STDOUT_FILENO, "\x1b[K", 3);
        if (y < E.textrows - 1)
            write(STDOUT_FILENO, "\r\n", 2);
    }
}

/* editorDrawStatusBar: Draw status bar with file info */
void editorDrawStatusBar() {
    char status[STATUS_MSG_SIZE];
    char rstatus[32];
    int len = snprintf(status, sizeof(status), "%.20s%s",
                       E.filename ? E.filename : "[No Name]",
                       E.dirty ? " (modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "Ln %d, Col %d",
                        E.cy + 1, E.cx + 1);
    if (len > E.screencols)
        len = E.screencols;
    write(STDOUT_FILENO, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen)
            break;
        write(STDOUT_FILENO, " ", 1);
        len++;
    }
    write(STDOUT_FILENO, rstatus, rlen);
}

/* editorDrawCommandBar: Draw command prompt */
void editorDrawCommandBar() {
    char buf[CMD_BUF_SIZE + 10];
    snprintf(buf, sizeof(buf), ":%s", E.command_buffer);
    write(STDOUT_FILENO, buf, strlen(buf));
}

/* editorRefreshScreen: Clear screen, draw text area, status bar, command bar, and position cursor */
void editorRefreshScreen() {
    int rn_width = getRowNumWidth();
    if (E.in_command_mode)
        E.textrows = E.screenrows - 2;
    else
        E.textrows = E.screenrows - 1;
    write(STDOUT_FILENO, "\x1b[?25l", 6);
    write(STDOUT_FILENO, "\x1b[H", 3);
    editorDrawRows(rn_width);
    write(STDOUT_FILENO, "\r\n", 2);
    write(STDOUT_FILENO, "\x1b[7m", 4);
    editorDrawStatusBar();
    write(STDOUT_FILENO, "\x1b[m", 4);
    if (E.in_command_mode) {
        write(STDOUT_FILENO, "\r\n", 2);
        write(STDOUT_FILENO, "\x1b[7m", 4);
        editorDrawCommandBar();
        write(STDOUT_FILENO, "\x1b[m", 4);
    }
    if (!E.in_command_mode) {
        int cursor_y = (E.cy - E.rowoff) + 1;
        int cursor_x = rn_width + (E.cx - E.coloff) + 1;
        if (cursor_y < 1)
            cursor_y = 1;
        if (cursor_x < 1)
            cursor_x = 1;
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
        write(STDOUT_FILENO, buf, strlen(buf));
    } else {
        int cmd_cursor = 1 + E.command_length;
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenrows, cmd_cursor);
        write(STDOUT_FILENO, buf, strlen(buf));
    }
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

/* --- Command Processing --- */

void processCommand() {
    E.status_message[0] = '\0';
    char *cmd = E.command_buffer;
    while (*cmd == ' ')
        cmd++;
    if (strncmp(cmd, "quit", 4) == 0 || strncmp(cmd, "q", 1) == 0) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    } else if (strncmp(cmd, "save", 4) == 0) {
        editorSave();
        snprintf(E.status_message, sizeof(E.status_message), "File saved.");
    } else if (strncmp(cmd, "open ", 5) == 0) {
        char *filename = cmd + 5;
        while (*filename == ' ')
            filename++;
        editorOpen(filename);
        snprintf(E.status_message, sizeof(E.status_message), "Opened %s", filename);
    } else if (strncmp(cmd, "search ", 7) == 0) {
        char *query = cmd + 7;
        int found = 0;
        for (int i = 0; i < E.numrows; i++) {
            char *p = strstr(E.row[i].chars, query);
            if (p) {
                E.cy = i;
                E.cx = editorDisplayWidth(E.row[i].chars) - editorDisplayWidth(p);
                found = 1;
                break;
            }
        }
        if (found)
            snprintf(E.status_message, sizeof(E.status_message), "Found \"%s\"", query);
        else
            snprintf(E.status_message, sizeof(E.status_message), "Not found: \"%s\"", query);
    } else if (strncmp(cmd, "goto ", 5) == 0) {
        char *line_str = cmd + 5;
        int line = atoi(line_str);
        if (line > 0 && line <= E.numrows) {
            E.cy = line - 1;
            if (E.cx > editorDisplayWidth(E.row[E.cy].chars))
                E.cx = editorDisplayWidth(E.row[E.cy].chars);
            snprintf(E.status_message, sizeof(E.status_message), "Moved to line %d", line);
        } else {
            snprintf(E.status_message, sizeof(E.status_message), "Invalid line: %d", line);
        }
    } else {
        snprintf(E.status_message, sizeof(E.status_message), "Unknown command: %s", cmd);
    }
}

/* --- Key Processing --- */

void editorProcessKeypress() {
    int c = editorReadKey();
    if (E.in_command_mode) {
        if (c == '\r') {
            processCommand();
            E.in_command_mode = 0;
            E.command_length = 0;
            E.command_buffer[0] = '\0';
        } else if (c == '\x1b') {
            E.in_command_mode = 0;
            E.command_length = 0;
            E.command_buffer[0] = '\0';
            snprintf(E.status_message, sizeof(E.status_message), "Command canceled.");
        } else if (c == BACKSPACE || c == CTRL_KEY('h')) {
            if (E.command_length > 0) {
                E.command_length--;
                E.command_buffer[E.command_length] = '\0';
            }
        } else if (!iscntrl(c) && E.command_length < CMD_BUF_SIZE - 1) {
            E.command_buffer[E.command_length++] = c;
            E.command_buffer[E.command_length] = '\0';
        }
        return;
    }
    /* Normal mode key processing */
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('e'):
            E.in_command_mode = 1;
            E.command_length = 0;
            E.command_buffer[0] = '\0';
            break;
        case CTRL_KEY('z'):
            pop_undo_state();
            break;
        case '\r':
            push_undo_state();
            editorInsertNewline();
            break;
        case CTRL_KEY('h'):
        case BACKSPACE:
            push_undo_state();
            editorDelChar();
            break;
        case ARROW_UP:
            if (E.cy > 0)
                E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1)
                E.cy++;
            break;
        case ARROW_LEFT:
            if (E.cx > 0)
                E.cx--;
            else if (E.cy > 0) {
                E.cy--;
                E.cx = editorDisplayWidth(E.row[E.cy].chars);
            }
            break;
        case ARROW_RIGHT:
            {
                int roww = editorDisplayWidth(E.row[E.cy].chars);
                if (E.cx < roww)
                    E.cx++;
                else if (E.cy < E.numrows - 1) {
                    E.cy++;
                    E.cx = 0;
                }
            }
            break;
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
            break;
    }
    int rn_width = getRowNumWidth();
    if (E.cx < E.coloff)
        E.coloff = E.cx;
    if (E.cx >= E.coloff + (E.screencols - rn_width - 1))
        E.coloff = E.cx - (E.screencols - rn_width - 1) + 1;
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.textrows)
        E.rowoff = E.cy - E.textrows + 1;
}

/* --- File/Buffer Operations --- */

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
            return;
        } else {
            die("fopen");
        }
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        if (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }
        editorAppendLine(line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    if (E.numrows == 0)
        editorAppendLine("", 0);
}

void editorSave() {
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
            close(fd);
            free(buf);
            E.dirty = 0;
            // Reset modified flags on all rows after save.
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
    E.row[E.numrows].modified = 0;  // new rows are unmodified.
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
    line->modified = 1;
    E.dirty = 1;
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
    if (width < 0)
        width = 1;
    E.cx += width;
    line->modified = 1;
    E.dirty = 1;
}

void editorInsertNewline() {
    if (E.cx == 0) {
        editorAppendLine("", 0);
        for (int i = E.numrows - 1; i > E.cy; i--) {
            E.row[i] = E.row[i - 1];
        }
        E.row[E.cy].chars = malloc(1);
        E.row[E.cy].chars[0] = '\0';
        E.row[E.cy].size = 0;
        E.row[E.cy].modified = 0;
        E.cy++;
    } else {
        EditorLine *line = &E.row[E.cy];
        int index = editorRowCxToByteIndex(line, E.cx);
        char *new_chars = malloc(line->size - index + 1);
        if (new_chars == NULL)
            die("malloc");
        memcpy(new_chars, &line->chars[index], line->size - index);
        new_chars[line->size - index] = '\0';
        int new_len = line->size - index;
        line->size = index;
        line->chars[index] = '\0';
        EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
        if (new_row == NULL)
            die("realloc");
        E.row = new_row;
        for (int j = E.numrows; j > E.cy; j--) {
            E.row[j] = E.row[j - 1];
        }
        E.numrows++;
        E.row[E.cy + 1].chars = new_chars;
        E.row[E.cy + 1].size = new_len;
        E.row[E.cy + 1].modified = 1;
        E.cy++;
        E.cx = 0;
    }
    E.dirty = 1;
}

void editorDelChar() {
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
        memcpy(&prev_line->chars[prev_size], line->chars, line->size);
        prev_line->chars[prev_size + line->size] = '\0';
        prev_line->size = prev_size + line->size;
        prev_line->modified = 1;
        free(line->chars);
        for (int j = E.cy; j < E.numrows - 1; j++) {
            E.row[j] = E.row[j + 1];
        }
        E.numrows--;
        E.cy--;
        E.cx = editorDisplayWidth(prev_line->chars);
    } else {
        int index = editorRowCxToByteIndex(line, E.cx);
        int prev_index = editorRowCxToByteIndex(line, E.cx - 1);
        memmove(&line->chars[prev_index], &line->chars[index],
                line->size - index + 1);
        line->size -= (index - prev_index);
        E.cx -= 1;
        line->modified = 1;
    }
    E.dirty = 1;
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    setlocale(LC_CTYPE, "");
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.dirty = 0;
    E.in_command_mode = 0;
    E.command_length = 0;
    E.command_buffer[0] = '\0';
    E.status_message[0] = '\0';

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    E.textrows = E.screenrows - 1;

    if (argc >= 2)
        editorOpen(argv[1]);
    else {
        editorAppendLine("", 0);
        E.dirty = 0;
    }

    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
