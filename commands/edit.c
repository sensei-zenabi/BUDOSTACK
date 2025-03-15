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
      - **Fixed file loading: Removed initial empty line from the text buffer when a file is successfully loaded, ensuring that the first displayed row number is 1.**
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

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 4            /* Expand each \t to 4 spaces */
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

/*** Terminal Handling ***/
void die(const char *s) {
    perror(s);
    exit(EXIT_FAILURE);
}

/* For showing short status messages (like errors) */
static void editorStatusMessage(const char *msg) {
    write(STDOUT_FILENO, "\x1b[2K\r", 5); /* Clear line & go to start */
    write(STDOUT_FILENO, msg, strlen(msg));
    /* Pause briefly so user sees it, or simply let it remain until next refresh. */
    tcdrain(STDOUT_FILENO);
}

void disableRawMode() {
    /* Restore original terminal attributes */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        perror("tcsetattr");
    /* Show the cursor again */
    write(STDOUT_FILENO, "\x1b[?25h", 6);
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
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

// Get terminal window size by tricking terminal into reporting cursor position.
int getWindowSize(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        return -1;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    /* Response should be ESC [ rows ; cols R */
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

/*** File I/O ***/

// Loads file content into E.buffer (line by line).
void editorOpen(const char *filename) {
    E.filename = strdup(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        const char *msg = "\x1b[2K\r[New file] Press any key to continue...";
        write(STDOUT_FILENO, msg, strlen(msg));
        char dummy;
        read(STDIN_FILENO, &dummy, 1);
        return;
    }
    
    /* FIX: If the editor buffer contains the initial empty line from initialization,
       free it so that the file's first line is displayed as line 1. */
    if (E.buffer.count == 1 && E.buffer.lines[0][0] == '\0') {
        free(E.buffer.lines[0]);
        E.buffer.count = 0;
    }
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        /* Strip trailing newline, if any */
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';

        // Expand buffer if needed
        if (E.buffer.count == E.buffer.capacity) {
            E.buffer.capacity *= 2;
            char **new_lines = realloc(E.buffer.lines, E.buffer.capacity * sizeof(char*));
            if (!new_lines) {
                editorStatusMessage("[ERROR] Out of memory while loading file.");
                break; /* Attempt partial load */
            }
            E.buffer.lines = new_lines;
        }
        E.buffer.lines[E.buffer.count++] = strdup(line);
    }
    fclose(fp);
}

void editorSave() {
    if (E.filename == NULL) {
        const char *msg = "\x1b[2K\r[ERROR] No filename provided!\n";
        write(STDOUT_FILENO, msg, strlen(msg));
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
    write(STDOUT_FILENO, msg, strlen(msg));
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
  Inserts a character into the line at the position E.cx,
  then increments E.cx by 1. This uses E.cx as the real
  “absolute” position in the line’s string.
*/
void insertChar(int c) {
    /* If the cursor is on the last line (below which is no line), create one. */
    if ((size_t)E.cy == E.buffer.count)
        appendEmptyLine(&E.buffer);

    if ((size_t)E.cy >= E.buffer.count) return; // safety check

    char *line = E.buffer.lines[E.cy];
    size_t len = strlen(line);

    /* If adding another character would exceed MAX_LINE_LENGTH, show msg and return. */
    if (len + 2 > MAX_LINE_LENGTH) {
        editorStatusMessage("[WARN] Line length limit reached. Insertion skipped.");
        return;
    }

    char *new_line = malloc(len + 2);
    if (!new_line) {
        editorStatusMessage("[ERROR] Out of memory during insert.");
        return;
    }

    /* Copy up to E.cx, insert c, then copy the rest. */
    memcpy(new_line, line, E.cx);
    new_line[E.cx] = c;
    memcpy(new_line + E.cx + 1, line + E.cx, len - E.cx + 1); // +1 for '\0'

    free(E.buffer.lines[E.cy]);
    E.buffer.lines[E.cy] = new_line;
    E.cx++;
    E.dirty = 1;
}

/*
  Deletes a character at E.cx-1 (a backspace),
  or merges lines if we are at start of line but not the first line.
*/
void deleteChar() {
    if ((size_t)E.cy >= E.buffer.count) return;
    char *line = E.buffer.lines[E.cy];
    size_t len = strlen(line);

    // If we are at the start of a line, and not on the first line, merge with previous line
    if (E.cx == 0 && E.cy > 0) {
        int prevLen = strlen(E.buffer.lines[E.cy - 1]);

        if ((size_t)(prevLen + len + 1) > MAX_LINE_LENGTH) {
            editorStatusMessage("[WARN] Merge would exceed line length limit. Deletion skipped.");
            return;
        }

        // Combine lines
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
        // Normal backspace in the middle or end of line
        char *new_line = malloc(len);
        if (!new_line) {
            editorStatusMessage("[ERROR] Out of memory during deleteChar.");
            return;
        }

        memcpy(new_line, line, E.cx - 1);
        memcpy(new_line + (E.cx - 1), line + E.cx, len - E.cx + 1);

        free(E.buffer.lines[E.cy]);
        E.buffer.lines[E.cy] = new_line;
        E.cx--;
    }
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
        if (e > (int)len) e = len;
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
        if (E.clipboard == NULL) {
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

        // Copy it first, which also clears E.sel_active
        copySelection();
        if (!E.clipboard) return; // out of memory or something went wrong

        // Now remove that text from the buffer
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
            memcpy(new_line, line, start.x);
            strcpy(new_line + start.x, line + end.x);
            free(E.buffer.lines[start.y]);
            E.buffer.lines[start.y] = new_line;
        } else {
            // Multi-line removal
            char *first_part = strndup(E.buffer.lines[start.y], start.x);
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
                /* If it exceeds the limit, truncate last_part and warn user. */
                last_part[MAX_LINE_LENGTH - strlen(first_part) - 1] = '\0';
                editorStatusMessage("[WARN] Merged line was truncated.");
            }
            char *merged = malloc(strlen(first_part) + strlen(last_part) + 1);
            if (!merged) {
                /* FIX: If malloc fails, we revert E.buffer.lines[start.y] to first_part
                   and free last_part to avoid memory leaks. */
                editorStatusMessage("[ERROR] Out of memory merging lines.");
                free(last_part);
                return;
            }
            strcpy(merged, first_part);
            strcat(merged, last_part);
            /* free first_part only once, as stated in code comment. */
            free(first_part);
            E.buffer.lines[start.y] = merged;
            free(last_part);
        }
        E.cx = start.x;
        E.cy = start.y;
        E.dirty = 1;
    }
}

/*
  Pastes the clipboard content at E.cx, E.cy.
*/
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
            return; // safety check
        }
        char *line = E.buffer.lines[E.cy];
        size_t linelen = strlen(line);
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

        memcpy(new_line, line, E.cx);
        strcpy(new_line + E.cx, clip);
        strcpy(new_line + E.cx + len, line + E.cx);

        free(E.buffer.lines[E.cy]);
        E.buffer.lines[E.cy] = new_line;
        E.cx += len;
    } else {
        // Multi-line paste
        if ((size_t)E.cy == E.buffer.count)
            appendEmptyLine(&E.buffer);
        if ((size_t)E.cy >= E.buffer.count) {
            free(clip);
            return;
        }

        char *current = E.buffer.lines[E.cy];
        size_t leftLen = E.cx;
        size_t rightLen = strlen(current) - E.cx;

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
        strcpy(right_part, current + E.cx);

        /* First line chunk from the clipboard */
        char *saveptr;
        char *line_token = strtok_r(clip, "\n", &saveptr);
        if (!line_token) {
            free(left_part);
            free(right_part);
            free(clip);
            return; // no tokens
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
                    /* We won't fully revert, but partial paste has already happened. */
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
            E.cx = lastLen;
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
        write(STDOUT_FILENO, "\x1b[s", 3); 
        write(STDOUT_FILENO, "\x1b[H", 3); 
        write(STDOUT_FILENO, status, strlen(status));
        write(STDOUT_FILENO, "\x1b[u", 3);

        int c = editorReadKey();
        if (c == '\r' || c == '\n') {
            write(STDOUT_FILENO, "\x1b[2K\r", 5);
            break;
        } else if (c == 127 || c == CTRL_KEY('h')) {
            if (buflen != 0) {
                buflen--;
                buf[buflen] = '\0';
            }
        /* Optional: press ESC to cancel? 
           if (c == 27) { 
               free(buf); 
               return NULL; 
           }
        */
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
            buf[buflen++] = c;
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
    write(STDOUT_FILENO, msg, strlen(msg));
}

/*** Scrolling and Rendering ***/
static char *expandTabs(const char *input) {
    int length = 0;
    int cap = MAX_LINE_LENGTH * 2; // Enough to hold expansions (safe margin).
    char *output = malloc(cap);
    if (!output) return NULL;
    output[0] = '\0';

    for (const char *p = input; *p != '\0'; p++) {
        if (*p == '\t') {
            for (int i = 0; i < TAB_STOP; i++) {
                if (length + 1 >= cap - 1) break;
                output[length++] = ' ';
            }
        } else {
            output[length++] = *p;
        }
        if (length >= cap - 1) break;
    }
    output[length] = '\0';
    return output;
}

static void printSubstringWithOptionalInvert(const char *line, int start, int length, int invert) {
    if (length <= 0) return;
    if (invert) write(STDOUT_FILENO, "\x1b[7m", 4);  
    write(STDOUT_FILENO, line + start, length);
    if (invert) write(STDOUT_FILENO, "\x1b[0m", 4);  
}

void renderLine(int row, Position *selStart, Position *selEnd) {
    int filerow = E.rowoff + row;
    char lineNum[8];
    snprintf(lineNum, sizeof(lineNum), "%4d ", filerow + 1);
    write(STDOUT_FILENO, lineNum, strlen(lineNum));

    if ((size_t)filerow >= E.buffer.count) {
        write(STDOUT_FILENO, "~", 1);
        return;
    }

    const char *rawLine = E.buffer.lines[filerow];
    char *expanded = expandTabs(rawLine);
    if (!expanded) return;
    int expandedLen = (int)strlen(expanded);

    int avail = E.screencols - 5; // line number columns used

    int leftEdge = E.coloff;
    if (leftEdge < expandedLen) {
        expandedLen -= leftEdge;
        if (expandedLen < 0) expandedLen = 0;
    } else {
        expandedLen = 0;
    }
    if (expandedLen > avail) expandedLen = avail;

    const char *renderPtr = expanded + E.coloff;
    if (!E.sel_active ||
        filerow < selStart->y || filerow > selEnd->y) {
        write(STDOUT_FILENO, renderPtr, expandedLen);
        free(expanded);
        return;
    }

    // There's an active selection that touches this line
    int rawLen = (int)strlen(rawLine);
    int *tabIndex = malloc(sizeof(int) * (rawLen + 1));
    if (!tabIndex) {
        write(STDOUT_FILENO, renderPtr, expandedLen);
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

    free(tabIndex);

    printSubstringWithOptionalInvert(renderPtr, 0, expSelBegin, 0);
    printSubstringWithOptionalInvert(renderPtr, expSelBegin, expSelEnd - expSelBegin, 1);
    printSubstringWithOptionalInvert(renderPtr, expSelEnd, expandedLen - expSelEnd, 0);

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

    write(STDOUT_FILENO, "\x1b[?25l\x1b[H", 9);

    Position selStart, selEnd;
    if (E.sel_active) {
        getSelectionBounds(&selStart, &selEnd);
    } else {
        selStart.x = selStart.y = -1;
        selEnd.x   = selEnd.y   = -1;
    }

    for (int y = 0; y < E.screenrows; y++) {
        renderLine(y, &selStart, &selEnd);
        write(STDOUT_FILENO, "\x1b[K\r\n", 5);
    }

    // Draw status bar
    char statusBar[256];
    snprintf(statusBar, sizeof(statusBar),
             "\x1b[7m[File: %s] [Lines: %zu] [Cursor: %d,%d]\x1b[0m",
             E.filename ? E.filename : "Untitled",
             E.buffer.count, E.cx, E.cy);
    write(STDOUT_FILENO, statusBar, strlen(statusBar));

    int cx_screen = (E.cx - E.coloff) + 6; 
    int cy_screen = (E.cy - E.rowoff) + 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy_screen, cx_screen);
    write(STDOUT_FILENO, buf, strlen(buf));

    write(STDOUT_FILENO, "\x1b[?25h", 6);
    tcdrain(STDOUT_FILENO);
}

/*** Input Processing ***/
int editorReadKey(void) {
    char c;
    while (read(STDIN_FILENO, &c, 1) != 1)
        ;
    if (c == '\x1b') {
        char seq[6];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == ';') {
                    if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
                    if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
                    if (seq[4] == 'A') return SHIFT_ARROW_UP;
                    if (seq[4] == 'B') return SHIFT_ARROW_DOWN;
                    if (seq[4] == 'C') return SHIFT_ARROW_RIGHT;
                    if (seq[4] == 'D') return SHIFT_ARROW_LEFT;
                } else {
                    if (seq[2] == 'A') return ARROW_UP;
                    if (seq[2] == 'B') return ARROW_DOWN;
                    if (seq[2] == 'C') return ARROW_RIGHT;
                    if (seq[2] == 'D') return ARROW_LEFT;
                }
            } else {
                if (seq[1] == 'A') return ARROW_UP;
                if (seq[1] == 'B') return ARROW_DOWN;
                if (seq[1] == 'C') return ARROW_RIGHT;
                if (seq[1] == 'D') return ARROW_LEFT;
            }
        }
        return '\x1b';
    } 
    else {
        return c;
    }
}

/*
  Main dispatch for each key pressed: navigation, editing, selection, etc.
*/
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

    case CTRL_KEY('c'):
        copySelection();
        break;

    case CTRL_KEY('x'):
        cutSelection();
        break;

    case CTRL_KEY('v'):
        pasteClipboard();
        break;

    // Basic arrow keys (no selection)
    case ARROW_LEFT:
        E.sel_active = 0;
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = strlen(E.buffer.lines[E.cy]);
        }
        break;

    case ARROW_RIGHT:
        E.sel_active = 0;
        if (E.cy < (int)E.buffer.count && (size_t)E.cx < strlen(E.buffer.lines[E.cy])) {
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
            E.cx = strlen(E.buffer.lines[E.cy]);
        }
        break;

    case ARROW_DOWN:
        E.sel_active = 0;
        if (E.cy < (int)E.buffer.count) {
            E.cy++;
        }
        if (E.cy < (int)E.buffer.count) {
            if ((size_t)E.cx > strlen(E.buffer.lines[E.cy])) {
                E.cx = strlen(E.buffer.lines[E.cy]);
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
            E.cx = strlen(E.buffer.lines[E.cy]);
        }
        break;

    case SHIFT_ARROW_RIGHT:
        if (!E.sel_active) {
            E.sel_anchor_x = E.cx;
            E.sel_anchor_y = E.cy;
            E.sel_active = 1;
        }
        if (E.cy < (int)E.buffer.count && (size_t)E.cx < strlen(E.buffer.lines[E.cy])) {
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
            E.cx = strlen(E.buffer.lines[E.cy]);
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
            if ((size_t)E.cx > strlen(E.buffer.lines[E.cy])) {
                E.cx = strlen(E.buffer.lines[E.cy]);
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
