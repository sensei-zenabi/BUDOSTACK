#define _POSIX_C_SOURCE 200809L
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
#include <locale.h>
#include <wchar.h>

// Define editor version
#define EDITOR_VERSION "0.1-micro-like"

// Key definitions for special keys (Ctrl key and Backspace)
#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127

// Maximum lengths for command buffer and status messages
#define CMD_BUF_SIZE 128
#define STATUS_MSG_SIZE 80

// Enumeration for arrow keys (using values above 255 to avoid char conflicts)
enum EditorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    // Additional keys (Home, End, etc.) could be added here if needed
};

// Data structure for a line of text in the editor
typedef struct {
    int size;       // length in bytes of the line
    char *chars;    // dynamically allocated character data (UTF-8 encoded)
} EditorLine;

// Global editor state structure
struct EditorConfig {
    int cx, cy;       // cursor position (logical columns/rows, not raw byte index)
    int screenrows;   // total terminal rows
    int screencols;   // total terminal columns
    int rowoff;       // vertical scroll: first row displayed
    int coloff;       // horizontal scroll: logical column offset
    int numrows;      // number of rows in the file
    EditorLine *row;  // array of lines
    char *filename;   // name of the open file
    int dirty;        // unsaved changes flag
    struct termios orig_termios; // original terminal settings

    // Fields for command mode:
    int in_command_mode;                  // 1 if in command mode
    char command_buffer[CMD_BUF_SIZE];    // command prompt input buffer
    int command_length;                   // current length of command buffer
    char status_message[STATUS_MSG_SIZE]; // temporary status message
    int textrows;                         // rows available for text (screenrows minus bar(s))
} E;

/* --- Function Prototypes --- */
void die(const char *s);
void disableRawMode();
void enableRawMode();
int  editorReadKey();
int  getWindowSize(int *rows, int *cols);
void editorRefreshScreen();
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

/* --- UTF-8 Helper Functions --- */

// Returns the display width of a UTF-8 encoded string.
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

// Converts a logical column (display width) to the corresponding byte index in a row.
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

// Renders a row starting from logical column E.coloff and writes up to E.screencols columns.
void editorRenderRow(EditorLine *row) {
    int logical_width = 0;
    int byte_index = editorRowCxToByteIndex(row, E.coloff);
    char buffer[1024];
    int buf_index = 0;
    size_t bytes;
    wchar_t wc;
    while (byte_index < row->size && logical_width < E.screencols) {
        bytes = mbrtowc(&wc, row->chars + byte_index, MB_CUR_MAX, NULL);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            bytes = 1;
        }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (logical_width + w > E.screencols) break;
        // Copy the UTF-8 bytes into the buffer.
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

/* --- Terminal Setup and Teardown --- */

// die: Print error and exit.
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

// disableRawMode: Restore original terminal attributes.
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

// enableRawMode: Enable raw mode.
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
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

// editorReadKey: Read next keypress.
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
            read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }
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

// getWindowSize: Get terminal window size.
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 ||
        ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/* --- Drawing Routines --- */

// Draw text rows.
void editorDrawRows() {
    for (int y = 0; y < E.textrows; y++) {
        int file_row = E.rowoff + y;
        if (file_row >= E.numrows) {
            write(STDOUT_FILENO, "~", 1);
        } else {
            editorRenderRow(&E.row[file_row]);
        }
        write(STDOUT_FILENO, "\x1b[K", 3);
        if (y < E.textrows - 1)
            write(STDOUT_FILENO, "\r\n", 2);
    }
}

// Draw status bar.
void editorDrawStatusBar() {
    char status[STATUS_MSG_SIZE];
    char rstatus[32];
    int len = snprintf(status, sizeof(status), "%.20s%s",
                       E.filename ? E.filename : "[No Name]",
                       E.dirty ? " (modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "Ln %d, Col %d",
                        E.cy + 1, E.cx + 1);
    if (len > E.screencols) len = E.screencols;
    write(STDOUT_FILENO, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) break;
        write(STDOUT_FILENO, " ", 1);
        len++;
    }
    write(STDOUT_FILENO, rstatus, rlen);
}

// Draw command bar.
void editorDrawCommandBar() {
    char buf[CMD_BUF_SIZE + 10];
    snprintf(buf, sizeof(buf), ":%s", E.command_buffer);
    write(STDOUT_FILENO, buf, strlen(buf));
}

// Refresh screen: clear, draw text area, status, and command bars.
void editorRefreshScreen() {
    if (E.in_command_mode)
        E.textrows = E.screenrows - 2;
    else
        E.textrows = E.screenrows - 1;

    write(STDOUT_FILENO, "\x1b[?25l", 6);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

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
        int cursor_x = 0;
        if (E.numrows > 0) {
            // Map logical column E.cx to byte offset for correct cursor positioning.
            cursor_x = editorDisplayWidth(E.row[E.cy].chars);
            // To place the cursor correctly relative to coloff,
            // we need to recompute display width up to (E.cx - E.coloff)
            int disp = editorDisplayWidth(E.row[E.cy].chars);
            // Here we simply position at the right if we have horizontal scrolling.
            // (A full solution would recompute the substring width.)
            cursor_x = (E.cx - E.coloff) + 1;
        } else {
            cursor_x = 1;
        }
        if (cursor_y < 1) cursor_y = 1;
        if (cursor_x < 1) cursor_x = 1;
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
    while (*cmd == ' ') cmd++;
    if (strncmp(cmd, "quit", 4) == 0 || strncmp(cmd, "q", 1) == 0) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    } else if (strncmp(cmd, "save", 4) == 0) {
        editorSave();
        snprintf(E.status_message, sizeof(E.status_message), "File saved.");
    } else if (strncmp(cmd, "open ", 5) == 0) {
        char *filename = cmd + 5;
        while (*filename == ' ') filename++;
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
    // Normal mode:
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
        case '\r':
            editorInsertNewline();
            break;
        case CTRL_KEY('h'):
        case BACKSPACE:
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
                    for (int i = 1; i < utf8_len; i++) {
                        utf8_buf[i] = editorReadKey();
                    }
                    utf8_buf[utf8_len] = '\0';
                    editorInsertUTF8(utf8_buf, utf8_len);
                }
            }
            break;
    }
    // Horizontal scrolling adjustments using logical columns.
    if (E.cx < E.coloff)
        E.coloff = E.cx;
    if (E.cx >= E.coloff + E.screencols)
        E.coloff = E.cx - E.screencols + 1;
    // Vertical scrolling:
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.textrows)
        E.rowoff = E.cy - E.textrows + 1;
}

/* --- File and Buffer Operations --- */

void editorOpen(const char *filename) {
    free(E.filename);
    E.filename = strdup(filename);
    if (E.filename == NULL) die("strdup");
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
    if (E.numrows == 0) editorAppendLine("", 0);
}

void editorSave() {
    if (E.filename == NULL) return;
    int total_len = 0;
    for (int j = 0; j < E.numrows; j++) {
        total_len += E.row[j].size + 1;
    }
    char *buf = malloc(total_len);
    if (buf == NULL) die("malloc");
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
            return;
        }
        close(fd);
    }
    free(buf);
    die("write");
}

void editorAppendLine(char *s, size_t len) {
    EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
    if (new_row == NULL) die("realloc");
    E.row = new_row;
    E.row[E.numrows].chars = malloc(len + 1);
    if (E.row[E.numrows].chars == NULL) die("malloc");
    memcpy(E.row[E.numrows].chars, s, len);
    E.row[E.numrows].chars[len] = '\0';
    E.row[E.numrows].size = (int)len;
    E.numrows++;
}

void editorInsertChar(int c) {
    if (E.cy == E.numrows) editorAppendLine("", 0);
    EditorLine *line = &E.row[E.cy];
    if (E.cx > editorDisplayWidth(line->chars)) {
        // If cursor is beyond current display width, adjust.
        E.cx = editorDisplayWidth(line->chars);
    }
    // Map logical column E.cx to byte index.
    int index = editorRowCxToByteIndex(line, E.cx);
    char *new_chars = realloc(line->chars, line->size + 2);
    if (new_chars == NULL) die("realloc");
    line->chars = new_chars;
    memmove(&line->chars[index + 1], &line->chars[index], line->size - index + 1);
    line->chars[index] = c;
    line->size++;
    E.cx++;  // ASCII always width 1.
    E.dirty = 1;
}

void editorInsertUTF8(const char *s, int len) {
    if (E.cy == E.numrows) editorAppendLine("", 0);
    EditorLine *line = &E.row[E.cy];
    if (E.cx > editorDisplayWidth(line->chars)) {
        E.cx = editorDisplayWidth(line->chars);
    }
    int index = editorRowCxToByteIndex(line, E.cx);
    char *new_chars = realloc(line->chars, line->size + len + 1);
    if (new_chars == NULL) die("realloc");
    line->chars = new_chars;
    memmove(&line->chars[index + len], &line->chars[index], line->size - index + 1);
    memcpy(&line->chars[index], s, len);
    line->size += len;
    // Determine display width of inserted character:
    wchar_t wc;
    size_t bytes = mbrtowc(&wc, s, len, NULL);
    int width = (bytes == (size_t)-1 || bytes == (size_t)-2) ? 1 : wcwidth(wc);
    if (width < 0) width = 1;
    E.cx += width;
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
        E.cy++;
    } else {
        EditorLine *line = &E.row[E.cy];
        int index = editorRowCxToByteIndex(line, E.cx);
        char *new_chars = malloc(line->size - index + 1);
        if (new_chars == NULL) die("malloc");
        memcpy(new_chars, &line->chars[index], line->size - index);
        new_chars[line->size - index] = '\0';
        int new_len = line->size - index;
        line->size = index;
        line->chars[index] = '\0';
        EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
        if (new_row == NULL) die("realloc");
        E.row = new_row;
        for (int j = E.numrows; j > E.cy; j--) {
            E.row[j] = E.row[j - 1];
        }
        E.numrows++;
        E.row[E.cy + 1].chars = new_chars;
        E.row[E.cy + 1].size = new_len;
        E.cy++;
        E.cx = 0;
    }
    E.dirty = 1;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    EditorLine *line = &E.row[E.cy];
    if (E.cx == 0) {
        EditorLine *prev_line = &E.row[E.cy - 1];
        int prev_size = prev_line->size;
        prev_line->chars = realloc(prev_line->chars, prev_size + line->size + 1);
        if (prev_line->chars == NULL) die("realloc");
        memcpy(&prev_line->chars[prev_size], line->chars, line->size);
        prev_line->chars[prev_size + line->size] = '\0';
        prev_line->size = prev_size + line->size;
        free(line->chars);
        for (int j = E.cy; j < E.numrows - 1; j++) {
            E.row[j] = E.row[j + 1];
        }
        E.numrows--;
        E.cy--;
        E.cx = editorDisplayWidth(prev_line->chars);
    } else {
        int index = editorRowCxToByteIndex(line, E.cx);
        // To delete, we need the byte length of the UTF-8 character before the cursor.
        int prev_index = editorRowCxToByteIndex(line, E.cx - 1);
        memmove(&line->chars[prev_index], &line->chars[index],
                line->size - index + 1);
        line->size -= (index - prev_index);
        E.cx -= 1; // Assume width 1 deletion; for more precision, recalc if needed.
    }
    E.dirty = 1;
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    // Set locale to support UTF-8
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

    if (argc >= 2) {
        editorOpen(argv[1]);
    } else {
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
