#define _POSIX_C_SOURCE 200809L
/*
    Full-Screen Text Editor with Multi-Selection in Plain C
    ---------------------------------------------------------
    Design Principles:
      - All functionality is implemented in a single C file using only standard libraries.
      - Uses raw terminal mode to capture keypresses.
      - Utilizes dynamic buffers for text lines with a fixed maximum line length.
      - Implements multi-line selection, copy/cut/paste, find/replace, and file I/O.
      - Plain C is used with -std=c11 and only standard cross-platform libraries.
      - No header files are created.
      - Comments throughout the code explain design decisions and modifications.

    Fixes:
      - Replaced direct exit(0) calls with a quit flag to ensure memory cleanup.
      - In cutSelection(), ensured no double-free or orphaned memory in merged line logic.
      - Provided minimal feedback on partial insertion/paste/replace if a line would exceed MAX_LINE_LENGTH.
      - Provided more robust handling of malloc/realloc failures by reverting or showing an error message.
      - Fixed file loading: Removed initial empty line from the text buffer when a file is successfully loaded, ensuring that the first displayed row number is 1.
      - Fixed screen refresh: Reserved the last terminal row for the status bar to ensure that the text area and cursor remain aligned with the terminal display.

    Additional bug fixes:
      - **FIX**: SHIFT+Arrow sequences with multiple digits after '[' (e.g. "[1;2A", "[10;2A") are now parsed correctly, so multi-digit row/col codes do not break SHIFT-based selection.
      - **FIX**: Insert/delete code clamps `E.cx` if it ever exceeds line length to avoid out-of-bounds writes.
      - **FIX**: Handle carriage return '\r' in file loading so Windows-style line endings don’t remain in text.
      - **FIX**: Added a small wrapper for status/error messages to ensure partial writes (if any) are completed.
      
    Bug Fixes Added in This Revision:
      - **FIX**: Added ENTER key functionality to create a newline by splitting the current line at the cursor.
      - **FIX**: Added CTRL+A functionality to select all text in the editor.
      
    References:
      - Original source code: :contentReference[oaicite:0]{index=0}
      - Design inspiration: Kilo text editor (by Salvatore Sanfilippo)
*/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*** Prototypes ***/
int editorReadKey(void);
static void robustWrite(int fd, const char *buf, size_t len);

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 4
#define INITIAL_BUFFER_CAPACITY 100
#define MAX_LINE_LENGTH 1024

/* Escape sequence codes for arrow keys and shifted arrows */
enum EditorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    SHIFT_ARROW_UP,
    SHIFT_ARROW_DOWN,
    SHIFT_ARROW_LEFT,
    SHIFT_ARROW_RIGHT
};

/*** Data Structures ***/

// Position structure for selection boundaries.
typedef struct {
    int x, y;
} Position;

// TextBuffer holds an array of dynamically allocated lines.
typedef struct {
    char **lines;
    size_t count;
    size_t capacity;
} TextBuffer;

// Editor configuration and state.
typedef struct {
    /* Cursor in text buffer */
    int cx, cy;               

    /* Visible screen size (rows & cols) */
    int screenrows, screencols;

    /* Saved original terminal settings */
    struct termios orig_termios;

    /* File data */
    char *filename;           
    int dirty;               

    /* Our text buffer. */
    TextBuffer buffer;       

    /* Clipboard (may hold multi-line text). */
    char *clipboard;         

    /* Selection state: active or not, plus anchor. */
    int sel_active;
    int sel_anchor_x;         
    int sel_anchor_y;         

    /* Scrolling offsets: which top row & left col are shown on screen? */
    int rowoff;               
    int coloff;               

    /* Flag to signal that we should quit gracefully. */
    int shouldQuit;
} EditorConfig;

EditorConfig E;

/*** Low-level I/O Helpers ***/
/* 
   FIX: A small wrapper that ensures the entire buffer is written. 
   (Prevents any partial writes from losing data for status messages.)
*/
static void robustWrite(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t nw = write(fd, buf + written, len - written);
        if (nw < 1) {
            /* If we fail, just break. We'll ignore the rest. */
            break;
        }
        written += (size_t)nw;
    }
}

void die(const char *s) {
    perror(s);
    exit(EXIT_FAILURE); // Fatal error => direct exit is acceptable
}

/* For showing short status messages (like errors) */
static void editorStatusMessage(const char *msg) {
    robustWrite(STDOUT_FILENO, "\x1b[2K\r", 5); /* Clear line & go to start */
    robustWrite(STDOUT_FILENO, msg, strlen(msg));
    /* Pause briefly or let it remain until next refresh. */
    tcdrain(STDOUT_FILENO);
}

/*** Terminal Handling ***/
void disableRawMode() {
    /* Restore original terminal attributes */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        perror("tcsetattr");
    /* Show the cursor again */
    robustWrite(STDOUT_FILENO, "\x1b[?25h", 6);
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    /* Input modes off */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output modes off */
    raw.c_oflag &= ~(OPOST);
    /* Control modes */
    raw.c_cflag |= CS8;
    /* Local modes off */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    /* Return each read() as soon as there is at least 1 byte, or after 100 ms. */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;  // 100 ms

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");

    /* Hide the cursor */
    robustWrite(STDOUT_FILENO, "\x1b[?25l", 6);
}

int getWindowSize(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    /* Response should be ESC [ rows ; cols R */
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

/*** File I/O ***/

/*
  editorOpen: loads file content into E.buffer line by line.
  FIX: also strip '\r' for Windows CR-LF compatibility.
*/
void editorOpen(const char *filename) {
    E.filename = strdup(filename);
    if (!E.filename) {
        editorStatusMessage("[ERROR] Out of memory storing filename.");
        return;
    }
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        const char *msg = "\x1b[2K\r[New file] Press any key to continue...";
        robustWrite(STDOUT_FILENO, msg, strlen(msg));
        char dummy;
        read(STDIN_FILENO, &dummy, 1);
        return;
    }

    /* If the editor buffer has 1 empty line from init, remove it */
    if (E.buffer.count == 1 && E.buffer.lines[0][0] == '\0') {
        free(E.buffer.lines[0]);
        E.buffer.count = 0;
    }

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        /* Strip trailing newline */
        if (len && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        /* FIX: Also strip trailing '\r' if present (Windows line ending) */
        if (len && line[len - 1] == '\r') {
            line[len - 1] = '\0';
            len--;
        }

        /* Expand buffer if needed */
        if (E.buffer.count == E.buffer.capacity) {
            E.buffer.capacity *= 2;
            char **new_lines = realloc(E.buffer.lines, E.buffer.capacity * sizeof(char*));
            if (!new_lines) {
                editorStatusMessage("[ERROR] Out of memory while loading file.");
                break; /* Attempt partial load */
            }
            E.buffer.lines = new_lines;
        }
        char *stored = strdup(line);
        if (!stored) {
            editorStatusMessage("[ERROR] Out of memory while storing line.");
            break;
        }
        E.buffer.lines[E.buffer.count++] = stored;
    }
    fclose(fp);
}

void editorSave() {
    if (E.filename == NULL) {
        const char *msg = "\x1b[2K\r[ERROR] No filename provided!\n";
        robustWrite(STDOUT_FILENO, msg, strlen(msg));
        return;
    }
    FILE *fp = fopen(E.filename, "w");
    if (!fp) {
        perror("fopen");
        return;
    }
    for (size_t i = 0; i < E.buffer.count; i++)
        fprintf(fp, "%s\n", E.buffer.lines[i]);
    fclose(fp);
    E.dirty = 0;
    const char *msg = "\x1b[2K\r[Saved]\n";
    robustWrite(STDOUT_FILENO, msg, strlen(msg));
}

/*** Text Buffer Handling ***/

void initBuffer(TextBuffer *buffer) {
    buffer->lines = malloc(INITIAL_BUFFER_CAPACITY * sizeof(char*));
    if (!buffer->lines) {
        fprintf(stderr, "Unable to allocate text buffer.\n");
        exit(EXIT_FAILURE);
    }
    buffer->count = 0;
    buffer->capacity = INITIAL_BUFFER_CAPACITY;
}

void freeBuffer(TextBuffer *buffer) {
    for (size_t i = 0; i < buffer->count; i++)
        free(buffer->lines[i]);
    free(buffer->lines);
}

// Append an empty line if the file is empty or user scrolls down to a new line.
void appendEmptyLine(TextBuffer *buffer) {
    if (buffer->count == buffer->capacity) {
        buffer->capacity *= 2;
        char **new_lines = realloc(buffer->lines, buffer->capacity * sizeof(char*));
        if (!new_lines) {
            editorStatusMessage("[ERROR] Out of memory while expanding lines.");
            return; /* Incomplete insertion. */
        }
        buffer->lines = new_lines;
    }
    buffer->lines[buffer->count++] = calloc(1, 1); /* empty string line */
    E.dirty = 1;
}

/*** Editor Insertion/Deletion ***/

/*
  Inserts a character at E.cx, then increments E.cx.
  FIX: Clamp E.cx <= current line length to avoid out-of-bounds copying.
*/
void insertChar(int c) {
    /* If below last line, add a new empty line. */
    if ((size_t)E.cy == E.buffer.count)
        appendEmptyLine(&E.buffer);

    if ((size_t)E.cy >= E.buffer.count) return;

    char *line = E.buffer.lines[E.cy];
    size_t len = strlen(line);

    /* FIX: clamp E.cx if out-of-bounds */
    if (E.cx < 0) E.cx = 0;
    if (E.cx > (int)len) E.cx = (int)len;

    if (len + 2 > MAX_LINE_LENGTH) {
        editorStatusMessage("[WARN] Line length limit reached. Insertion skipped.");
        return;
    }
    char *new_line = malloc(len + 2);
    if (!new_line) {
        editorStatusMessage("[ERROR] Out of memory during insert.");
        return;
    }
    memcpy(new_line, line, (size_t)E.cx);
    new_line[E.cx] = (char)c;
    memcpy(new_line + E.cx + 1, line + E.cx, len - E.cx + 1);

    free(E.buffer.lines[E.cy]);
    E.buffer.lines[E.cy] = new_line;
    E.cx++;
    E.dirty = 1;
}

/*
  Deletes a character at E.cx-1 (backspace),
  or merges lines if we're at the start of a line and not on the first line.
  FIX: Clamp E.cx in case it exceeded line length.
*/
void deleteChar() {
    if ((size_t)E.cy >= E.buffer.count) return;

    char *line = E.buffer.lines[E.cy];
    size_t len = strlen(line);

    /* FIX: clamp E.cx if out-of-bounds */
    if (E.cx > (int)len) E.cx = (int)len;
    if (E.cx < 0) E.cx = 0;

    // If we are at the start of a line, and not on the first line, merge with previous line
    if (E.cx == 0 && E.cy > 0) {
        int prevLen = (int)strlen(E.buffer.lines[E.cy - 1]);

        if ((size_t)(prevLen + len + 1) > MAX_LINE_LENGTH) {
            editorStatusMessage("[WARN] Merge would exceed line length limit. Deletion skipped.");
            return;
        }
        char *new_line = malloc(prevLen + len + 1);
        if (!new_line) {
            editorStatusMessage("[ERROR] Out of memory during line merge.");
            return;
        }
        strcpy(new_line, E.buffer.lines[E.cy - 1]);
        strcat(new_line, line);

        free(E.buffer.lines[E.cy - 1]);
        E.buffer.lines[E.cy - 1] = new_line;
        free(line);

        // Shift lines upward
        for (size_t i = E.cy; i < E.buffer.count - 1; i++) {
            E.buffer.lines[i] = E.buffer.lines[i + 1];
        }
        E.buffer.count--;

        E.cy--;
        E.cx = prevLen;
    }
    else if (E.cx > 0) {
        // Normal backspace
        char *new_line = malloc(len);
        if (!new_line) {
            editorStatusMessage("[ERROR] Out of memory during deleteChar.");
            return;
        }
        memcpy(new_line, line, (size_t)(E.cx - 1));
        memcpy(new_line + (E.cx - 1), line + E.cx, len - E.cx + 1);

        free(E.buffer.lines[E.cy]);
        E.buffer.lines[E.cy] = new_line;
        E.cx--;
    }
    E.dirty = 1;
}

/*
   Inserts a newline by splitting the current line at the cursor position.
   Design: This function creates a new line containing the text after the cursor
   and truncates the current line at the cursor.
   BUG FIX: Resolves the issue where pressing ENTER did not create a new line.
*/
void insertNewline(void) {
    if ((size_t)E.cy >= E.buffer.count) {
        appendEmptyLine(&E.buffer);
        E.cy++;
        E.cx = 0;
        E.dirty = 1;
        return;
    }
    char *line = E.buffer.lines[E.cy];
    size_t len = strlen(line);

    /* Create a new line with the text after the current cursor position */
    char *new_line = strdup(line + E.cx);
    if (!new_line) {
        editorStatusMessage("[ERROR] Out of memory during newline insertion.");
        return;
    }
    /* Truncate the current line at the cursor */
    line[E.cx] = '\0';

    /* Ensure buffer capacity */
    if (E.buffer.count == E.buffer.capacity) {
        E.buffer.capacity *= 2;
        char **new_lines = realloc(E.buffer.lines, E.buffer.capacity * sizeof(char*));
        if (!new_lines) {
            editorStatusMessage("[ERROR] Out of memory expanding buffer for newline.");
            free(new_line);
            return;
        }
        E.buffer.lines = new_lines;
    }
    /* Shift subsequent lines down to make room for the new line */
    for (size_t i = E.buffer.count; i > (size_t)E.cy + 1; i--) {
        E.buffer.lines[i] = E.buffer.lines[i - 1];
    }
    E.buffer.lines[E.cy + 1] = new_line;
    E.buffer.count++;

    /* Move cursor to beginning of the new line */
    E.cy++;
    E.cx = 0;
    E.dirty = 1;
}

/*** Selection Helpers ***/
void getSelectionBounds(Position *start, Position *end) {
    if (E.sel_anchor_y < E.cy ||
       (E.sel_anchor_y == E.cy && E.sel_anchor_x <= E.cx)) {
        start->y = E.sel_anchor_y;
        start->x = E.sel_anchor_x;
        end->y = E.cy;
        end->x = E.cx;
    } else {
        start->y = E.cy;
        start->x = E.cx;
        end->y = E.sel_anchor_y;
        end->x = E.sel_anchor_x;
    }
}

/*** Clipboard Operations ***/
void copySelection() {
    if (E.buffer.count == 0) return;
    free(E.clipboard);
    E.clipboard = NULL;

    // If no selection is active, copy the entire current line.
    if (!E.sel_active) {
        if ((size_t)E.cy >= E.buffer.count) return;
        E.clipboard = strdup(E.buffer.lines[E.cy]);
        return;
    }
    Position start, end;
    getSelectionBounds(&start, &end);

    if (start.y == end.y) {
        /* Single-line selection */
        char *line = E.buffer.lines[start.y];
        size_t len = strlen(line);
        int s = start.x, e = end.x;
        if (s < 0) s = 0;
        if (e > (int)len) e = (int)len;
        size_t n = (size_t)(e - s);
        E.clipboard = malloc(n + 1);
        if (!E.clipboard) {
            editorStatusMessage("[ERROR] Out of memory while copying selection.");
            return;
        }
        memcpy(E.clipboard, line + s, n);
        E.clipboard[n] = '\0';
    } else {
        /* Multi-line selection */
        size_t cap = 0;
        for (int i = start.y; i <= end.y; i++)
            cap += strlen(E.buffer.lines[i]) + 1;
        char *copybuf = malloc(cap + 1);
        if (!copybuf) {
            editorStatusMessage("[ERROR] Out of memory copying multi-line selection.");
            return;
        }
        copybuf[0] = '\0';

        /* First line: portion from start.x to end of line */
        strncat(copybuf,
                E.buffer.lines[start.y] + start.x,
                strlen(E.buffer.lines[start.y]) - start.x);
        strcat(copybuf, "\n");

        /* Middle lines in their entirety */
        for (int i = start.y + 1; i < end.y; i++) {
            strcat(copybuf, E.buffer.lines[i]);
            strcat(copybuf, "\n");
        }

        /* Last line: portion from 0..end.x */
        strncat(copybuf, E.buffer.lines[end.y], end.x);
        E.clipboard = copybuf;
    }
    E.sel_active = 0;
}

void cutSelection() {
    if (E.buffer.count == 0) return;
    if (!E.sel_active) {
        // Cut entire line
        free(E.clipboard);
        E.clipboard = strdup(E.buffer.lines[E.cy]);
        if (!E.clipboard) {
            editorStatusMessage("[ERROR] Out of memory while cutting line.");
            return;
        }
        free(E.buffer.lines[E.cy]);
        // Shift everything up
        for (size_t i = E.cy; i < E.buffer.count - 1; i++)
            E.buffer.lines[i] = E.buffer.lines[i + 1];
        E.buffer.count--;
        if (E.cy >= (int)E.buffer.count && E.cy > 0) E.cy--;
        E.cx = 0;
        E.dirty = 1;
    } else {
        Position start, end;
        getSelectionBounds(&start, &end);

        copySelection();
        if (!E.clipboard) return; // out of memory or something went wrong

        if (start.y == end.y) {
            // Single-line removal
            char *line = E.buffer.lines[start.y];
            int cut_len = end.x - start.x;
            size_t oldLen = strlen(line);
            char *new_line = malloc(oldLen - cut_len + 1);
            if (!new_line) {
                editorStatusMessage("[ERROR] Out of memory removing selection.");
                return;
            }
            memcpy(new_line, line, (size_t)start.x);
            strcpy(new_line + start.x, line + end.x);
            free(E.buffer.lines[start.y]);
            E.buffer.lines[start.y] = new_line;
        } else {
            // Multi-line removal
            char *first_part = strndup(E.buffer.lines[start.y], (size_t)start.x);
            if (!first_part) {
                editorStatusMessage("[ERROR] Out of memory for first_part in multi-line cut.");
                return;
            }
            char *last_part = strdup(E.buffer.lines[end.y] + end.x);
            if (!last_part) {
                free(first_part);
                editorStatusMessage("[ERROR] Out of memory for last_part in multi-line cut.");
                return;
            }

            free(E.buffer.lines[start.y]);
            E.buffer.lines[start.y] = first_part;

            // Free all lines in between (including the end line)
            for (int i = start.y + 1; i <= end.y; i++) {
                free(E.buffer.lines[i]);
            }
            size_t shift = (size_t)(end.y - start.y);
            for (size_t i = end.y + 1; i < E.buffer.count; i++) {
                E.buffer.lines[i - shift] = E.buffer.lines[i];
            }
            E.buffer.count -= shift;

            // Merge the first_part and last_part into a single line.
            size_t new_len = strlen(first_part) + strlen(last_part);
            if (new_len + 1 > MAX_LINE_LENGTH) {
                last_part[MAX_LINE_LENGTH - strlen(first_part) - 1] = '\0';
                editorStatusMessage("[WARN] Merged line was truncated.");
            }
            char *merged = malloc(strlen(first_part) + strlen(last_part) + 1);
            if (!merged) {
                editorStatusMessage("[ERROR] Out of memory merging lines.");
                free(last_part);
                return;
            }
            strcpy(merged, first_part);
            strcat(merged, last_part);
            free(first_part);
            E.buffer.lines[start.y] = merged;
            free(last_part);
        }
        E.cx = start.x;
        E.cy = start.y;
        E.dirty = 1;
    }
}

void pasteClipboard() {
    if (!E.clipboard) return;
    char *clip = strdup(E.clipboard);
    if (!clip) {
        editorStatusMessage("[ERROR] Out of memory duplicating clipboard.");
        return;
    }

    char *nl = strchr(clip, '\n');
    if (!nl) {
        // Single-line paste
        size_t len = strlen(clip);
        if ((size_t)E.cy == E.buffer.count) 
            appendEmptyLine(&E.buffer);

        if ((size_t)E.cy >= E.buffer.count) {
            free(clip);
            return;
        }
        char *line = E.buffer.lines[E.cy];
        size_t linelen = strlen(line);

        /* clamp E.cx if needed */
        if (E.cx < 0) E.cx = 0;
        if (E.cx > (int)linelen) E.cx = (int)linelen;

        if (linelen + len + 1 > MAX_LINE_LENGTH) {
            editorStatusMessage("[WARN] Line length limit. Paste truncated/skipped.");
            free(clip);
            return;
        }
        char *new_line = malloc(linelen + len + 1);
        if (!new_line) {
            editorStatusMessage("[ERROR] Out of memory during paste.");
            free(clip);
            return;
        }

        memcpy(new_line, line, (size_t)E.cx);
        strcpy(new_line + E.cx, clip);
        strcpy(new_line + E.cx + len, line + E.cx);

        free(E.buffer.lines[E.cy]);
        E.buffer.lines[E.cy] = new_line;
        E.cx += (int)len;
    } else {
        // Multi-line paste
        if ((size_t)E.cy == E.buffer.count)
            appendEmptyLine(&E.buffer);
        if ((size_t)E.cy >= E.buffer.count) {
            free(clip);
            return;
        }

        char *current = E.buffer.lines[E.cy];
        size_t leftLen = (size_t)E.cx;
        size_t rightLen = strlen(current) - leftLen;

        char *left_part = malloc(leftLen + 1);
        char *right_part = malloc(rightLen + 1);
        if (!left_part || !right_part) {
            editorStatusMessage("[ERROR] Out of memory splitting line for paste.");
            free(clip);
            free(left_part);
            free(right_part);
            return;
        }
        memcpy(left_part, current, leftLen);
        left_part[leftLen] = '\0';
        strcpy(right_part, current + leftLen);

        char *saveptr;
        char *line_token = strtok_r(clip, "\n", &saveptr);
        if (!line_token) {
            free(left_part);
            free(right_part);
            free(clip);
            return;
        }
        size_t firstLen = strlen(line_token);

        // The new line after the left part
        size_t newSize = leftLen + firstLen + 1;
        char *new_line = malloc(newSize);
        if (!new_line) {
            editorStatusMessage("[ERROR] Out of memory building first pasted line.");
            free(left_part);
            free(right_part);
            free(clip);
            return;
        }
        strcpy(new_line, left_part);
        strcat(new_line, line_token);

        free(E.buffer.lines[E.cy]);
        E.buffer.lines[E.cy] = new_line;

        // Insert additional tokens as new lines
        while ((line_token = strtok_r(NULL, "\n", &saveptr)) != NULL) {
            if (E.buffer.count == E.buffer.capacity) {
                E.buffer.capacity *= 2;
                char **new_lines = realloc(E.buffer.lines, E.buffer.capacity * sizeof(char*));
                if (!new_lines) {
                    editorStatusMessage("[ERROR] Out of memory expanding buffer for paste.");
                    break;
                }
                E.buffer.lines = new_lines;
            }
            // Shift lines down
            for (size_t i = E.buffer.count; i > (size_t)E.cy + 1; i--)
                E.buffer.lines[i] = E.buffer.lines[i - 1];

            E.buffer.lines[E.cy + 1] = strdup(line_token);
            E.buffer.count++;
            E.cy++;
        }

        // Merge the last line with right_part
        char *last_line = E.buffer.lines[E.cy];
        size_t lastLen = strlen(last_line);
        size_t mergedLen = lastLen + rightLen;
        if (mergedLen + 1 > MAX_LINE_LENGTH) {
            editorStatusMessage("[WARN] Final line in paste truncated.");
            right_part[MAX_LINE_LENGTH - lastLen - 1] = '\0';
        }
        char *merged = malloc(strlen(last_line) + strlen(right_part) + 1);
        if (merged) {
            strcpy(merged, last_line);
            strcat(merged, right_part);
            free(E.buffer.lines[E.cy]);
            E.buffer.lines[E.cy] = merged;
            // Position the cursor at the boundary
            E.cx = (int)lastLen;
        }
        free(left_part);
        free(right_part);
    }
    free(clip);
    E.dirty = 1;
}

/*** Prompt and Find/Replace ***/
char *editorPrompt(const char *prompt) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    if (!buf) return NULL;
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        // Draw prompt on status bar
        char status[256];
        snprintf(status, sizeof(status), "\x1b[7m%s%s\x1b[0m", prompt, buf);

        // Save cursor, move to top-left, print prompt, restore cursor
        robustWrite(STDOUT_FILENO, "\x1b[s", 3);
        robustWrite(STDOUT_FILENO, "\x1b[H", 3);
        robustWrite(STDOUT_FILENO, status, strlen(status));
        robustWrite(STDOUT_FILENO, "\x1b[u", 3);

        int c = editorReadKey();
        if (c == '\r' || c == '\n') {
            robustWrite(STDOUT_FILENO, "\x1b[2K\r", 5);
            break;
        } else if (c == 127 || c == CTRL_KEY('h')) {
            if (buflen != 0) {
                buflen--;
                buf[buflen] = '\0';
            }
        } else if (isprint(c)) {
            if (buflen + 1 >= bufsize) {
                bufsize *= 2;
                char *newbuf = realloc(buf, bufsize);
                if (!newbuf) {
                    free(buf);
                    editorStatusMessage("[ERROR] Out of memory in prompt.");
                    return NULL;
                }
                buf = newbuf;
            }
            buf[buflen++] = (char)c;
            buf[buflen] = '\0';
        }
    }
    return buf;
}

void editorFindReplace() {
    char *find_str = editorPrompt("Find: ");
    if (!find_str || find_str[0] == '\0') {
        free(find_str);
        return;
    }
    char *replace_str = editorPrompt("Replace: ");
    if (!replace_str) {
        free(find_str);
        return;
    }
    int total_replacements = 0;

    for (size_t i = 0; i < E.buffer.count; i++) {
        char *line = E.buffer.lines[i];
        char new_line[MAX_LINE_LENGTH];
        new_line[0] = '\0';

        int replaced = 0;
        char *curr = line;
        char *pos;
        while ((pos = strstr(curr, find_str)) != NULL) {
            size_t prefix_len = (size_t)(pos - curr);
            /* Check if we might overflow new_line */
            if (strlen(new_line) + prefix_len + strlen(replace_str) +
                strlen(pos + strlen(find_str)) >= MAX_LINE_LENGTH) {
                editorStatusMessage("[WARN] Replacement line truncated.");
                break;
            }
            strncat(new_line, curr, prefix_len);
            strcat(new_line, replace_str);
            total_replacements++;
            replaced = 1;
            curr = pos + strlen(find_str);
        }
        strcat(new_line, curr);

        if (replaced) {
            free(E.buffer.lines[i]);
            E.buffer.lines[i] = strdup(new_line);
            E.dirty = 1;
        }
    }
    free(find_str);
    free(replace_str);

    char msg[64];
    snprintf(msg, sizeof(msg), "\x1b[2K\r[Replaced %d occurrences]\n", total_replacements);
    robustWrite(STDOUT_FILENO, msg, strlen(msg));
}

/*** Scrolling and Rendering ***/
static char *expandTabs(const char *input) {
    int length = 0;
    int cap = MAX_LINE_LENGTH * 2; // Enough to hold expansions
    char *output = malloc((size_t)cap);
    if (!output) return NULL;
    output[0] = '\0';

    for (const char *p = input; *p != '\0'; p++) {
        if (*p == '\t') {
            for (int i = 0; i < TAB_STOP; i++) {
                if (length + 1 >= cap - 1) break;
                output[length++] = ' ';
            }
        } else {
            if (length + 1 >= cap - 1) break;
            output[length++] = *p;
        }
    }
    output[length] = '\0';
    return output;
}

static void printSubstringWithOptionalInvert(const char *line, int start, int length, int invert) {
    if (length <= 0) return;
    if (invert) robustWrite(STDOUT_FILENO, "\x1b[7m", 4);  
    robustWrite(STDOUT_FILENO, line + start, (size_t)length);
    if (invert) robustWrite(STDOUT_FILENO, "\x1b[0m", 4);  
}

void renderLine(int row, Position *selStart, Position *selEnd) {
    int filerow = E.rowoff + row;
    char lineNum[8];
    snprintf(lineNum, sizeof(lineNum), "%4d ", filerow + 1);
    robustWrite(STDOUT_FILENO, lineNum, strlen(lineNum));

    if ((size_t)filerow >= E.buffer.count) {
        robustWrite(STDOUT_FILENO, "~", 1);
        return;
    }

    const char *rawLine = E.buffer.lines[filerow];
    char *expanded = expandTabs(rawLine);
    if (!expanded) return;
    int expandedLen = (int)strlen(expanded);

    int avail = E.screencols - 5; // line number columns

    int leftEdge = E.coloff;
    if (leftEdge < expandedLen) {
        expandedLen -= leftEdge;
        if (expandedLen < 0) expandedLen = 0;
    } else {
        expandedLen = 0;
    }
    if (expandedLen > avail) expandedLen = avail;

    const char *renderPtr = expanded + E.coloff;

    // If no active selection or the selection is outside this row, just print
    if (!E.sel_active ||
        filerow < selStart->y || filerow > selEnd->y) {
        robustWrite(STDOUT_FILENO, renderPtr, (size_t)expandedLen);
        free(expanded);
        return;
    }

    // There's an active selection that touches this line
    int rawLen = (int)strlen(rawLine);
    int *tabIndex = malloc(sizeof(int) * (size_t)(rawLen + 1));
    if (!tabIndex) {
        robustWrite(STDOUT_FILENO, renderPtr, (size_t)expandedLen);
        free(expanded);
        return;
    }
    // Build running "expanded column" index
    int col = 0;
    for (int i = 0; i < rawLen; i++) {
        if (rawLine[i] == '\t') {
            for (int s = 0; s < TAB_STOP; s++) col++;
        } else {
            col++;
        }
        tabIndex[i] = col;
    }
    tabIndex[rawLen] = col; // end of line

    // Convert selection raw positions to expanded positions
    int expSelBegin = 0, expSelEnd = 0;
    {
        int startRaw = (filerow == selStart->y) ? selStart->x : 0;
        if (startRaw < 0) startRaw = 0;
        if (startRaw > rawLen) startRaw = rawLen;

        int endRaw = (filerow == selEnd->y) ? selEnd->x : rawLen;
        if (endRaw < 0) endRaw = 0;
        if (endRaw > rawLen) endRaw = rawLen;

        int sCol = (startRaw > 0) ? tabIndex[startRaw - 1] : 0;
        int eCol = (endRaw > 0)   ? tabIndex[endRaw - 1]   : 0;
        if (startRaw == 0) sCol = 0;
        if (endRaw == 0) eCol = 0;

        expSelBegin = sCol - E.coloff;
        expSelEnd   = eCol - E.coloff;
        if (expSelBegin < 0) expSelBegin = 0;
        if (expSelBegin > avail) expSelBegin = avail;
        if (expSelEnd < 0) expSelEnd = 0;
        if (expSelEnd > avail) expSelEnd = avail;
    }

    printSubstringWithOptionalInvert(renderPtr, 0, expSelBegin, 0);
    printSubstringWithOptionalInvert(renderPtr, expSelBegin,
                                     expSelEnd - expSelBegin, 1);
    printSubstringWithOptionalInvert(renderPtr, expSelEnd,
                                     expandedLen - expSelEnd, 0);

    free(tabIndex);
    free(expanded);
}

void editorScroll() {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    int screen_text_width = E.screencols - 5; // line number columns
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + screen_text_width) {
        E.coloff = E.cx - screen_text_width + 1;
    }
}

void editorRefreshScreen() {
    editorScroll();

    // Hide cursor and reposition to top-left.
    robustWrite(STDOUT_FILENO, "\x1b[?25l", 6);
    robustWrite(STDOUT_FILENO, "\x1b[H", 3);

    Position selStart, selEnd;
    if (E.sel_active) {
        getSelectionBounds(&selStart, &selEnd);
    } else {
        selStart.x = selStart.y = -1;
        selEnd.x   = selEnd.y   = -1;
    }

    int text_rows = E.screenrows - 1;  // reserve the last row for the status bar
    for (int y = 0; y < text_rows; y++) {
        renderLine(y, &selStart, &selEnd);
        robustWrite(STDOUT_FILENO, "\x1b[K\r\n", 5);
    }

    // Draw status bar on the last row.
    char statusBar[256];
    snprintf(statusBar, sizeof(statusBar),
             "\x1b[7m[File: %s] [Lines: %zu] [Cursor: %d,%d]\x1b[0m",
             E.filename ? E.filename : "Untitled",
             E.buffer.count, E.cx, E.cy);
    char statusPos[32];
    snprintf(statusPos, sizeof(statusPos), "\x1b[%d;1H", E.screenrows);
    robustWrite(STDOUT_FILENO, statusPos, strlen(statusPos));
    robustWrite(STDOUT_FILENO, statusBar, strlen(statusBar));

    // Calculate cursor position within the text area
    int cx_screen = (E.cx - E.coloff) + 6;
    int cy_screen = (E.cy - E.rowoff) + 1;
    if (cy_screen > text_rows) cy_screen = text_rows;
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy_screen, cx_screen);
    robustWrite(STDOUT_FILENO, buf, strlen(buf));

    // Show cursor again.
    robustWrite(STDOUT_FILENO, "\x1b[?25h", 6);
    tcdrain(STDOUT_FILENO);
}

/*** Input Processing ***/

int editorReadKey(void) {
    char c;
    while (read(STDIN_FILENO, &c, 1) != 1) {
        /* Keep reading until we get a byte. */
    }
    if (c == '\x1b') {
        // Escape sequence - read more
        char seq[32];
        memset(seq, 0, sizeof(seq));

        // First attempt to read up to 30 more bytes or until we hit a letter
        // that typically ends an escape sequence.
        int i = 0;
        if (read(STDIN_FILENO, &seq[i], 1) != 1) {
            return '\x1b';
        }
        i++;
        if (seq[0] != '[') {
            // Not a recognized CSI sequence
            return '\x1b';
        }
        // Read further bytes
        while (i < (int)(sizeof(seq) - 1)) {
            if (read(STDIN_FILENO, &seq[i], 1) != 1) {
                break;
            }
            // Break if we find a letter in the typical range
            if ((seq[i] >= '@' && seq[i] <= 'Z') || (seq[i] >= 'a' && seq[i] <= 'z')) {
                i++;
                break;
            }
            i++;
        }
        seq[i] = '\0';

        // Examples of possible sequences:
        //   "[A" (up), "[B" (down), "[C" (right), "[D" (left)
        //   "[1;2A" (shift+up), "[1;2B", "[1;2C", "[1;2D"
        //   "[10;2A" could appear, etc.
        // We only check for arrow patterns:
        //   final char A/B/C/D => up/down/right/left
        char finalChar = seq[i-1]; 
        // We'll parse the "before finalChar" to see if there's ";2"
        // indicating SHIFT.
        if (finalChar == 'A' || finalChar == 'B' ||
            finalChar == 'C' || finalChar == 'D') {
            // Check if there's ";2" somewhere
            if (strstr(seq, ";2") != NULL) {
                // SHIFT + arrow
                if (finalChar == 'A') return SHIFT_ARROW_UP;
                if (finalChar == 'B') return SHIFT_ARROW_DOWN;
                if (finalChar == 'C') return SHIFT_ARROW_RIGHT;
                if (finalChar == 'D') return SHIFT_ARROW_LEFT;
            } else {
                // Normal arrow
                if (finalChar == 'A') return ARROW_UP;
                if (finalChar == 'B') return ARROW_DOWN;
                if (finalChar == 'C') return ARROW_RIGHT;
                if (finalChar == 'D') return ARROW_LEFT;
            }
        }
        // If none matched, just return ESC for now
        return '\x1b';
    } else {
        return c;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();
    switch (c) {
    /* Instead of exit(0), set E.shouldQuit so we can free memory. */
    case CTRL_KEY('q'):
        E.shouldQuit = 1;
        return;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case CTRL_KEY('f'):
        editorFindReplace();
        break;

    case CTRL_KEY('a'):
        // Select all text in the editor.
        if (E.buffer.count > 0) {
            E.sel_active = 1;
            E.sel_anchor_x = 0;
            E.sel_anchor_y = 0;
            E.cy = (int)E.buffer.count - 1;
            E.cx = (int)strlen(E.buffer.lines[E.cy]);
        }
        break;

    case CTRL_KEY('c'):
        copySelection();
        break;

    case CTRL_KEY('x'):
        cutSelection();
        break;

    case CTRL_KEY('v'):
        pasteClipboard();
        break;

    case '\r':  // Handle ENTER key (both '\r' and '\n')
    case '\n':
        insertNewline();
        break;

    // Basic arrow keys (no selection)
    case ARROW_LEFT:
        E.sel_active = 0;
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = (int)strlen(E.buffer.lines[E.cy]);
        }
        break;

    case ARROW_RIGHT:
        E.sel_active = 0;
        if (E.cy < (int)E.buffer.count &&
            (size_t)E.cx < strlen(E.buffer.lines[E.cy])) {
            E.cx++;
        } else if (E.cy < (int)E.buffer.count) {
            E.cy++;
            E.cx = 0;
        }
        break;

    case ARROW_UP:
        E.sel_active = 0;
        if (E.cy > 0) {
            E.cy--;
        }
        if ((size_t)E.cx > strlen(E.buffer.lines[E.cy])) {
            E.cx = (int)strlen(E.buffer.lines[E.cy]);
        }
        break;

    case ARROW_DOWN:
        E.sel_active = 0;
        if (E.cy < (int)E.buffer.count) {
            E.cy++;
        }
        if (E.cy < (int)E.buffer.count) {
            size_t len = strlen(E.buffer.lines[E.cy]);
            if ((size_t)E.cx > len) {
                E.cx = (int)len;
            }
        } else {
            E.cx = 0;
        }
        break;

    // Shift+Arrows => selection
    case SHIFT_ARROW_LEFT:
        if (!E.sel_active) {
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            E.sel_active = 1;
        }
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = (int)strlen(E.buffer.lines[E.cy]);
        }
        break;

    case SHIFT_ARROW_RIGHT:
        if (!E.sel_active) {
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            E.sel_active = 1;
        }
        if (E.cy < (int)E.buffer.count &&
            (size_t)E.cx < strlen(E.buffer.lines[E.cy])) {
            E.cx++;
        } else if (E.cy < (int)E.buffer.count) {
            E.cy++;
            E.cx = 0;
        }
        break;

    case SHIFT_ARROW_UP:
        if (!E.sel_active) {
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            E.sel_active = 1;
        }
        if (E.cy > 0) {
            E.cy--;
        }
        if ((size_t)E.cx > strlen(E.buffer.lines[E.cy])) {
            E.cx = (int)strlen(E.buffer.lines[E.cy]);
        }
        break;

    case SHIFT_ARROW_DOWN:
        if (!E.sel_active) {
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            E.sel_active = 1;
        }
        if (E.cy < (int)E.buffer.count) {
            E.cy++;
        }
        if (E.cy < (int)E.buffer.count) {
            size_t len = strlen(E.buffer.lines[E.cy]);
            if ((size_t)E.cx > len) {
                E.cx = (int)len;
            }
        } else {
            E.cx = 0;
        }
        break;

    // Backspace or CTRL-H
    case 127:
    case CTRL_KEY('h'):
        deleteChar();
        break;

    // Insert normal printable characters
    default:
        if (isprint(c)) {
            insertChar(c);
        }
        break;
    }
}

/*** Initialization ***/
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.dirty = 0;
    E.filename = NULL;
    E.clipboard = NULL;
    E.sel_active = 0;
    E.sel_anchor_x = 0;
    E.sel_anchor_y = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.shouldQuit = 0;
    initBuffer(&E.buffer);

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    if (E.buffer.count == 0)
        appendEmptyLine(&E.buffer);
}

/*** Main ***/
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    // Initial screen refresh so the editor is visible on startup.
    editorRefreshScreen();

    /* Main loop: keep going until E.shouldQuit is set. */
    while (!E.shouldQuit) {
        editorProcessKeypress();
        if (!E.shouldQuit) {
            editorRefreshScreen();
        }
    }

    /* Now do a graceful exit. */
    freeBuffer(&E.buffer);
    free(E.clipboard);
    free(E.filename);
    return 0;
}
