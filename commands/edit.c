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

// Define editor version
#define EDITOR_VERSION "0.1"

// Key definitions for special keys (Ctrl key and Backspace)
#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127

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
    int size;       // length of the line (number of characters)
    char *chars;    // pointer to the line's character data (dynamically allocated)
} EditorLine;

// Global editor state structure
struct EditorConfig {
    int cx, cy;       // cursor x (column) and y (row) position in the file
    int screenrows;   // number of rows in the terminal screen
    int screencols;   // number of columns in the terminal screen
    int rowoff;       // index of the row of the file that is at the top of the screen (vertical scroll)
    int coloff;       // index of the column that is at the left of the screen (horizontal scroll)
    int numrows;      // number of rows (lines) of text in the file
    EditorLine *row;  // array of lines (of length numrows)
    char *filename;   // name of the open file
    int dirty;        // flag indicating if the file has unsaved changes
    struct termios orig_termios; // original terminal settings (to restore when exiting)
} E;

// Function prototypes for editor functionality
void die(const char *s);
void disableRawMode();
void enableRawMode();
int  editorReadKey();
int  getWindowSize(int *rows, int *cols);
void editorRefreshScreen();
void editorDrawRows();
void editorProcessKeypress();
void editorOpen(const char *filename);
void editorSave();
void editorAppendLine(char *s, size_t len);
void editorInsertChar(int c);
void editorInsertNewline();
void editorDelChar();

// die: Print an error message and exit (used for fatal errors). Also disable raw mode.
void die(const char *s) {
    // Clear the screen and reposition cursor before exiting to avoid messing up the terminal
    write(STDOUT_FILENO, "\x1b[2J", 4); // ESC [ 2 J  (clear entire screen)
    write(STDOUT_FILENO, "\x1b[H", 3);  // ESC [ H    (cursor to home position)
    perror(s);
    exit(1);
}

// disableRawMode: Restore the terminal's original attributes.
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

// enableRawMode: Put the terminal into raw mode (turn off canonical mode, echo, etc.).
void enableRawMode() {
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);    // Ensure raw mode is disabled when the program exits
    signal(SIGINT, SIG_IGN);   // Ignore Ctrl-C signals (we handle exit manually)
    signal(SIGTERM, SIG_IGN);  // Ignore termination signals for safety

    struct termios raw = E.orig_termios;
    // Input flags: disable break, CR->NL, parity checks, strip, and XON/XOFF
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Output flags: disable post-processing (we'll output raw bytes)
    raw.c_oflag &= ~(OPOST);
    // Control flags: set character size to 8 bits per byte
    raw.c_cflag |= (CS8);
    // Local flags: disable echo, canonical mode, extended input processing, and signals
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // Timing: read() returns after 1 byte or 0.1s (so escape sequences can be read easily)
    raw.c_cc[VMIN]  = 0;  // return even if 0 bytes available (after timeout)
    raw.c_cc[VTIME] = 1;  // 0.1 second timeout for read

    // Apply the raw mode settings immediately
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

// editorReadKey: Read the next keypress (handles escape sequences for arrow keys).
int editorReadKey() {
    char c;
    int nread;
    // Continuously read 1 byte until we get a result (read might timeout and return 0)
    while ((nread = read(STDIN_FILENO, &c, 1)) == 0) {
        // If no bytes were read (nread == 0), just loop and try again
        continue;
    }
    if (nread == -1 && errno != EAGAIN) {
        die("read");
    }

    if (c == '\x1b') {
        // Escape sequence (possible arrow key or other control sequence)
        char seq[3];
        // Read the next two bytes of the sequence (if available)
        if (read(STDIN_FILENO, &seq[0], 1) != 1 || 
            read(STDIN_FILENO, &seq[1], 1) != 1) {
            // If the sequence is incomplete (e.g., just ESC was pressed), return ESC
            return '\x1b';
        }
        if (seq[0] == '[') {
            // Arrow keys or other sequences start with ESC [
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Extended escape sequences (like Home, End, F1-F12 keys) are not handled in this editor.
                // We could read another byte and handle specific cases, but we'll ignore them here.
                char seq2;
                read(STDIN_FILENO, &seq2, 1);
                return '\x1b';
            } else {
                // Simple arrow keys
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }
        }
        // If it's an unrecognized escape sequence, return it as a literal ESC.
        return '\x1b';
    } else {
        // Not an escape sequence, return character as-is
        return c;
    }
}

// getWindowSize: Obtain the size of the terminal window (rows and columns).
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // ioctl failed or returned zero size; in a more complete implementation, we could try an alternative method.
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

// editorDrawRows: Draw each line of the buffer to the terminal, and ~ for empty lines beyond the buffer.
void editorDrawRows() {
    for (int y = 0; y < E.screenrows; y++) {
        int file_row = E.rowoff + y;
        if (file_row >= E.numrows) {
            // We are past the end of the file, draw a "~" on this row
            write(STDOUT_FILENO, "~", 1);
        } else {
            // Determine how much of the line to draw (handle horizontal scrolling)
            int len = E.row[file_row].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            // Write the visible part of the line
            write(STDOUT_FILENO, &E.row[file_row].chars[E.coloff], len);
        }
        // Clear the rest of the line to the right (to erase old content)
        write(STDOUT_FILENO, "\x1b[K", 3);
        // If not the last line, add a newline (carriage return + line feed)
        if (y < E.screenrows - 1) {
            write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}

// editorRefreshScreen: Clear the screen and redraw the editor content, then position the cursor.
void editorRefreshScreen() {
    // Hide the cursor while we redraw
    write(STDOUT_FILENO, "\x1b[?25l", 6);  // ESC [ ? 25 l  (hide cursor)
    // Move cursor to top-left before drawing (home position)
    write(STDOUT_FILENO, "\x1b[H", 3);     // ESC [ H

    editorDrawRows();  // draw the text buffer and tildes

    // After drawing, position the cursor where the user’s cursor (E.cx, E.cy) is
    char buf[32];
    int cursor_y = (E.cy - E.rowoff) + 1;   // +1 because terminal rows are 1-indexed
    int cursor_x = (E.cx - E.coloff) + 1;   // +1 for columns (1-indexed in terminal escape code)
    if (cursor_y < 1) cursor_y = 1;
    if (cursor_x < 1) cursor_x = 1;
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
    write(STDOUT_FILENO, buf, strlen(buf));

    // Show the cursor again
    write(STDOUT_FILENO, "\x1b[?25h", 6);  // ESC [ ? 25 h  (show cursor)
}

// editorOpen: Load a file from disk into the editor buffer.
void editorOpen(const char *filename) {
    // Free any existing filename and assign the new filename
    free(E.filename);
    E.filename = strdup(filename);
    if (E.filename == NULL) {
        die("strdup");
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (errno == ENOENT) {
            // File not found: start with an empty buffer (one empty line)
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
    // Read lines from the file one by one
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        // Remove any trailing newline or carriage return
        if (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }
        // Append the line to our editor buffer
        editorAppendLine(line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;  // file just loaded, no unsaved changes yet

    if (E.numrows == 0) {
        // If the file was empty, ensure we have one empty line in the buffer
        editorAppendLine("", 0);
    }
}

// editorSave: Save the current buffer to disk (overwrite the file).
void editorSave() {
    if (E.filename == NULL) return;  // safety check, should not happen since we set filename on open

    // Compute the total size of the file content (all lines + newline for each line)
    int total_len = 0;
    for (int j = 0; j < E.numrows; j++) {
        total_len += E.row[j].size + 1;  // each line plus a newline character
    }

    // Build one contiguous buffer to write to file
    char *buf = malloc(total_len);
    if (buf == NULL) {
        die("malloc");
    }
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    // Write buffer to file (create/truncate file)
    int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        if (write(fd, buf, total_len) == total_len) {
            // File saved successfully
            close(fd);
            free(buf);
            E.dirty = 0;
            return;
        }
        close(fd);
    }
    free(buf);
    die("write");  // if we reach here, write failed
}

// editorAppendLine: Add a new line to the editor's buffer (used for loading file or creating new lines).
void editorAppendLine(char *s, size_t len) {
    // Reallocate the line array to hold one more line
    EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
    if (new_row == NULL) {
        die("realloc");
    }
    E.row = new_row;
    // Allocate memory for the new line's text
    E.row[E.numrows].chars = malloc(len + 1);
    if (E.row[E.numrows].chars == NULL) {
        die("malloc");
    }
    memcpy(E.row[E.numrows].chars, s, len);
    E.row[E.numrows].chars[len] = '\0';   // null-terminate the line
    E.row[E.numrows].size = (int)len;
    E.numrows++;
}

// editorInsertChar: Insert a character at the current cursor position.
void editorInsertChar(int c) {
    // If cursor is on the virtual line *after* the last line, create a new empty line first
    if (E.cy == E.numrows) {
        editorAppendLine("", 0);
    }
    EditorLine *line = &E.row[E.cy];
    if (E.cx > line->size) {
        E.cx = line->size;
    }
    // Expand the memory for this line by 1 character (+1 for new char, +1 for null terminator)
    char *new_chars = realloc(line->chars, line->size + 2);
    if (new_chars == NULL) {
        die("realloc");
    }
    line->chars = new_chars;
    // Move the existing characters after the cursor one position to the right
    memmove(&line->chars[E.cx + 1], &line->chars[E.cx], line->size - E.cx + 1);
    // Insert the new character
    line->chars[E.cx] = c;
    line->size++;
    E.cx++;  // move cursor forward after insertion
    E.dirty = 1;
}

// editorInsertNewline: Split the line at the cursor position (or add new blank line).
void editorInsertNewline() {
    if (E.cx == 0) {
        // Cursor is at the beginning of a line; insert a new empty line before it
        editorAppendLine("", 0);
        // Move all lines from the current line downward by one to make space
        for (int i = E.numrows - 1; i > E.cy; i--) {
            E.row[i] = E.row[i - 1];
        }
        // The current line becomes an empty line
        E.row[E.cy].chars = malloc(1);
        E.row[E.cy].chars[0] = '\0';
        E.row[E.cy].size = 0;
        // Move cursor to the new line (which is blank)
        E.cy++;
    } else {
        // Cursor is in the middle or end of a line; split the line at cursor
        EditorLine *line = &E.row[E.cy];
        // Allocate new buffer for the text after the cursor
        char *new_chars = malloc(line->size - E.cx + 1);
        if (new_chars == NULL) {
            die("malloc");
        }
        memcpy(new_chars, &line->chars[E.cx], line->size - E.cx);
        new_chars[line->size - E.cx] = '\0';
        int new_len = line->size - E.cx;
        // Shrink the current line to end at the cursor position
        line->size = E.cx;
        line->chars[line->size] = '\0';
        // Insert a new entry in the lines array for the new line
        EditorLine *new_row = realloc(E.row, sizeof(EditorLine) * (E.numrows + 1));
        if (new_row == NULL) {
            die("realloc");
        }
        E.row = new_row;
        // Move lines after the current line down by one to make space for new line
        for (int j = E.numrows; j > E.cy; j--) {
            E.row[j] = E.row[j - 1];
        }
        E.numrows++;
        // Set the new line's content
        E.row[E.cy + 1].chars = new_chars;
        E.row[E.cy + 1].size = new_len;
        // Move cursor to the beginning of the new line
        E.cy++;
        E.cx = 0;
    }
    E.dirty = 1;
}

// editorDelChar: Delete the character immediately before the cursor (backspace).
void editorDelChar() {
    if (E.cy == E.numrows) return;           // cursor is past last line (nothing to delete)
    if (E.cx == 0 && E.cy == 0) return;      // at file start, nothing to delete

    EditorLine *line = &E.row[E.cy];
    if (E.cx == 0) {
        // We're at the beginning of a line and press backspace: merge this line with the previous one.
        EditorLine *prev_line = &E.row[E.cy - 1];
        int prev_size = prev_line->size;
        // Reallocate prev_line to have enough space for itself + current line
        prev_line->chars = realloc(prev_line->chars, prev_size + line->size + 1);
        if (prev_line->chars == NULL) {
            die("realloc");
        }
        // Copy current line's contents to the end of the previous line
        memcpy(&prev_line->chars[prev_size], line->chars, line->size);
        prev_line->chars[prev_size + line->size] = '\0';
        prev_line->size = prev_size + line->size;
        // Free the current line and remove it from the array
        free(line->chars);
        for (int j = E.cy; j < E.numrows - 1; j++) {
            E.row[j] = E.row[j + 1];
        }
        E.numrows--;
        // Move cursor to end of previous line
        E.cy--;
        E.cx = prev_size;
    } else {
        // Deleting a character in the middle or end of a line
        memmove(&line->chars[E.cx - 1], &line->chars[E.cx], line->size - E.cx + 1);
        line->size--;
        E.cx--;
    }
    E.dirty = 1;
}

// editorProcessKeypress: Fetch a key and handle it (navigate, edit, save, quit).
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):  // Ctrl-Q to quit
            // If we wanted, we could check E.dirty here and ask to save. For now, just quit.
            write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
            write(STDOUT_FILENO, "\x1b[H", 3);  // cursor to top-left
            exit(0);
            break;
        case CTRL_KEY('s'):  // Ctrl-S to save
            editorSave();
            break;
        case '\r':  // Enter key
            editorInsertNewline();
            break;
        case CTRL_KEY('h'):  // Ctrl-H (often sent by Backspace in some terminals)
        case BACKSPACE:      // Backspace key
            editorDelChar();
            break;
        case ARROW_UP:
            if (E.cy > 0) {
                E.cy--;
                // Adjust cursor x position if the line is shorter than current x
                if (E.cx > E.row[E.cy].size) {
                    E.cx = E.row[E.cy].size;
                }
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) {
                E.cy++;
                if (E.cx > E.row[E.cy].size) {
                    E.cx = E.row[E.cy].size;
                }
            }
            break;
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                // Move to end of previous line if at beginning of current line
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (E.cy < E.numrows) {
                if (E.cx < E.row[E.cy].size) {
                    E.cx++;
                } else if (E.cy < E.numrows - 1) {
                    // Move to start of next line if at end of current line
                    E.cy++;
                    E.cx = 0;
                }
            }
            break;
        default:
            // If it's a normal printable character, insert it
            if (!iscntrl(c)) {
                editorInsertChar(c);
            }
            break;
    }

    // After handling the key, adjust the scroll offsets to ensure the cursor is on-screen
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

// main: Entry point. Sets up the editor state, enters raw mode, and loops handling input.
int main(int argc, char *argv[]) {
    // Initialize the EditorConfig state
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.dirty = 0;

    // Get terminal size, or default to 24x80 if unavailable
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }

    // Open file if provided as argument, otherwise start with an empty buffer
    if (argc >= 2) {
        editorOpen(argv[1]);
    } else {
        editorAppendLine("", 0);
        E.dirty = 0;
    }

    enableRawMode();  // enter raw mode (after file is loaded, to avoid messing up stdout if error occurs during loading)

    // Main loop: draw screen and process keypresses
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
