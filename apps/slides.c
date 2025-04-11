/*
This is a terminal–based ASCII “slides” presentation program.
Usage: slides myslides.sld

Requirements met:
 - The presentation uses only ASCII.
 - When started with “slides myslides.sld” the file is loaded (or created if not found).
 - In both presentation and edit modes, the whole slide (the entire inner region) is displayed.
 - When in presentation mode, if CTRL+H is pressed, a help screen is shown with all the shortcuts and instructions.
   The help screen remains until CTRL+H is pressed again.
 - The active slide number is shown at the bottom right as “x/x”.
 - A centralized banner shows whether the app is in PRESENTATION MODE or EDIT MODE.
 - In presentation mode, pressing CTRL+E enters edit mode.
 - In edit mode, pressing CTRL+E (or ESC) exits edit mode returning to presentation mode.
 - In presentation or edit mode, pressing CTRL+Q quits the app.
 - In edit mode, arrow keys allow moving the editing cursor, printable characters modify the slide’s content,
   and the borders remain intact.
 - In edit mode, pressing CTRL+S saves the slides (writing all slides to disk) and displays a “Slideset saved!”
   message in the bottom left corner for roughly 3 seconds.
 - In edit mode, pressing CTRL+Z undoes changes (restoring the slide’s state from when edit mode was entered).
 - In presentation mode, pressing CTRL+N adds a new (blank) slide after the current slide.
 - In presentation mode, pressing CTRL+D deletes the current slide (except when it is the first slide).
 - Slides are loaded and saved from a file where individual slides are separated by a delimiter line "----".
 
This implementation uses plain C with –std=c11, standard C libraries, and POSIX–compliant functions.
*/

#define _POSIX_C_SOURCE 200112L

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>

/* 
   The following prototypes are added because with _POSIX_C_SOURCE=200112L, 
   the functions dprintf, getline and strdup may not be declared.
*/
int dprintf(int fd, const char *format, ...);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
char *strdup(const char *s);

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT
};

/* Global variables for terminal state and dimensions */
struct termios orig_termios;
int g_term_rows, g_term_cols;
/* 
   g_content_width and g_content_height define the size (in characters) of the slide’s full area,
   i.e. the complete inner region (the terminal minus the border).
   g_content_offset_x and g_content_offset_y mark where this area begins.
*/
int g_content_width, g_content_height;
int g_content_offset_x, g_content_offset_y;

/* Global variable for help mode.
   When g_help_mode is 1, the help screen is being displayed.
*/
int g_help_mode = 0;

/* Global variables for slides */
typedef struct {
    char **lines;      // Array of g_content_height strings (each of length g_content_width)
    char **undo_lines; // Backup copy for undo in edit mode (or NULL if none)
} Slide;

Slide **g_slides = NULL;
int g_slide_count = 0;
int g_current_slide = 0;
const char *g_filename = NULL; // slides file name

/* Global state variables for modes */
int g_edit_mode = 0;  // 0 = presentation mode; 1 = edit mode
int g_quit = 0;       // flag to indicate quitting

/************************************
 * Terminal raw mode helper functions
 ************************************/

/* Disable raw mode and restore original terminal settings */
void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Enable raw mode (disables canonical mode, echo, etc.) */
void enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/* Read a key from STDIN (handles escape sequences for arrow keys) */
int readKey(void) {
    char c;
    ssize_t nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        /* if interrupted by signal, try again */
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

/************************************
 * Terminal size and dimensions setup
 ************************************/

/* Get terminal window size */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/*
  Compute dimensions for the program.
  Set the full inner region (editing area and presentation area) to be the whole terminal minus the border.
*/
void computeDimensions(void) {
    if (getWindowSize(&g_term_rows, &g_term_cols) == -1) {
        perror("getWindowSize");
        exit(1);
    }
    int full_width = g_term_cols - 2;   // account for left/right borders
    int full_height = g_term_rows - 2;    // account for top/bottom borders
    g_content_width = full_width;
    g_content_height = full_height;
    g_content_offset_x = 2;
    g_content_offset_y = 2;
}

/************************************
 * Drawing functions (using ANSI escape codes)
 ************************************/

/* Clear the screen and reposition the cursor at the top–left */
void clearScreen(void) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/* Draw the outer border covering the whole terminal */
void drawBorder(void) {
    int r, c;
    /* Draw the top border */
    write(STDOUT_FILENO, "+", 1);
    for (c = 2; c < g_term_cols; c++) {
        write(STDOUT_FILENO, "-", 1);
    }
    write(STDOUT_FILENO, "+", 1);

    /* Draw the sides for rows 2 .. g_term_rows-1 */
    char line[g_term_cols + 1];
    for (r = 2; r < g_term_rows; r++) {
        line[0] = '|';
        memset(&line[1], ' ', g_term_cols - 2);
        line[g_term_cols - 1] = '|';
        line[g_term_cols] = '\0';
        dprintf(STDOUT_FILENO, "\x1b[%d;1H%s", r, line);
    }

    /* Draw the bottom border */
    dprintf(STDOUT_FILENO, "\x1b[%d;1H+", g_term_rows);
    for (c = 2; c < g_term_cols; c++) {
        write(STDOUT_FILENO, "-", 1);
    }
    write(STDOUT_FILENO, "+", 1);
}

/*
 * Draw the full slide content.
 * In both presentation and edit modes, the slide buffer covers the entire inner region.
 */
void drawFullSlideContent(Slide *slide) {
    for (int i = 0; i < g_content_height; i++) {
        dprintf(STDOUT_FILENO, "\x1b[%d;%dH%-*s", 
            g_content_offset_y + i, 
            g_content_offset_x, 
            g_content_width, 
            slide->lines[i]);
    }
}

/* Draw the slide indicator (e.g., "2/5") at bottom–right inside the border */
void drawSlideIndicator(void) {
    char indicator[32];
    snprintf(indicator, sizeof(indicator), "%d/%d", g_current_slide + 1, g_slide_count);
    int len = strlen(indicator);
    int row = g_term_rows;
    int col = g_term_cols - len; // within bottom border
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH%s", row, col, indicator);
}

/* Draw the centered mode banner on row 2 */
void drawModeBanner(void) {
    const char *banner = g_edit_mode ? "EDIT MODE" : "PRESENTATION MODE";
    int len = strlen(banner);
    int col = (g_term_cols - len) / 2 + 1;
    dprintf(STDOUT_FILENO, "\x1b[2;%dH%s", col, banner);
}

/* Refresh the screen in presentation mode: draw border, full slide content, slide indicator, and mode banner. */
void refreshPresentationScreen(void) {
    clearScreen();
    drawBorder();
    drawFullSlideContent(g_slides[g_current_slide]);
    drawSlideIndicator();
    drawModeBanner();
    /* Move the cursor out of the way */
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH", g_term_rows, g_term_cols);
}

/* Refresh the screen in edit mode and position the editing cursor.
   Here the entire editable area (full inner region) is drawn.
*/
void refreshEditScreen(int cur_row, int cur_col) {
    clearScreen();
    drawBorder();
    drawFullSlideContent(g_slides[g_current_slide]);
    drawModeBanner();
    /* Position the cursor in the editing area */
    int term_row = g_content_offset_y + cur_row;
    int term_col = g_content_offset_x + cur_col;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH", term_row, term_col);
}

/************************************
 * Help screen functionality.
 *
 * When the user (in presentation mode) presses CTRL+H, the help screen is displayed.
 * The help screen lists all shortcuts and instructions and remains until CTRL+H is pressed again.
 ************************************/

/* Display help information on the screen */
void displayHelp(void) {
    clearScreen();
    drawBorder();
    /* Display help text starting at a fixed position inside the border */
    int row = 4, col = 4;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHHELP MENU - Shortcuts and Instructions", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dH--------------------------------------", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+E : Toggle between Presentation and Edit mode", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+Q : Quit the app", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+S : Save slides (in Edit mode)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+Z : Undo changes (in Edit mode)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHARROW KEYS : Navigate slides (Presentation) or editing cursor (Edit)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+N : Add a new slide (after current slide)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+D : Delete the current slide (except first slide)", row, col);
    row++;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHCTRL+H : Toggle Help Menu", row, col);
    row += 2;
    dprintf(STDOUT_FILENO, "\x1b[%d;%dHPress CTRL+H again to return.", row, col);
}

/* Enter help mode: display the help screen until CTRL+H is pressed again */
void enterHelpMode(void) {
    g_help_mode = 1;
    while (g_help_mode) {
        displayHelp();
        int ch = readKey();
        if (ch == CTRL_KEY('H')) {
            g_help_mode = 0;
        }
    }
    /* After exiting help mode, clear the screen to refresh the presentation view */
    clearScreen();
}

/************************************
 * Slide file load/save functions
 *
 * Slides are stored in a file with each slide separated by a delimiter "----".
 * Each slide is loaded into a fixed–sized buffer of g_content_height rows
 * and g_content_width columns (the full inner region).
 ************************************/

/* Allocate and initialize a new blank slide */
Slide *newBlankSlide(void) {
    int i;
    Slide *s = malloc(sizeof(Slide));
    s->lines = malloc(sizeof(char*) * g_content_height);
    s->undo_lines = NULL;
    for (i = 0; i < g_content_height; i++) {
        s->lines[i] = malloc(g_content_width + 1);
        memset(s->lines[i], ' ', g_content_width);
        s->lines[i][g_content_width] = '\0';
    }
    return s;
}

/* Free a slide from memory */
void freeSlide(Slide *s) {
    int i;
    for (i = 0; i < g_content_height; i++) {
        free(s->lines[i]);
    }
    free(s->lines);
    if (s->undo_lines) {
        for (i = 0; i < g_content_height; i++) {
            free(s->undo_lines[i]);
        }
        free(s->undo_lines);
    }
    free(s);
}

/* Load slides from file.
   If the file does not exist or is empty, create one blank slide.
   Slides are separated by a delimiter line "----".
 */
void loadSlides(const char *filename) {
    FILE *fp = fopen(filename, "r");
    Slide **slides = NULL;
    int slideCount = 0;
    if (!fp) {
        /* File not found; create a blank slide */
        g_slide_count = 1;
        g_slides = malloc(sizeof(Slide *));
        g_slides[0] = newBlankSlide();
        return;
    }
    
    char *line = NULL;
    size_t len = 0;
    char *buffer[g_content_height];
    int bufCount = 0;
    while (getline(&line, &len, fp) != -1) {
        /* Remove newline if present */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        /* If delimiter is encountered, finish the current slide */
        if (strcmp(line, "----") == 0) {
            Slide *s = malloc(sizeof(Slide));
            s->lines = malloc(sizeof(char*) * g_content_height);
            s->undo_lines = NULL;
            int i;
            for (i = 0; i < g_content_height; i++) {
                s->lines[i] = malloc(g_content_width + 1);
                if (i < bufCount) {
                    int copyLen = strlen(buffer[i]);
                    if (copyLen > g_content_width) copyLen = g_content_width;
                    memcpy(s->lines[i], buffer[i], copyLen);
                    if (copyLen < g_content_width) {
                        memset(s->lines[i] + copyLen, ' ', g_content_width - copyLen);
                    }
                } else {
                    memset(s->lines[i], ' ', g_content_width);
                }
                s->lines[i][g_content_width] = '\0';
            }
            for (int i = 0; i < bufCount; i++) {
                free(buffer[i]);
            }
            bufCount = 0;
            slides = realloc(slides, sizeof(Slide*) * (slideCount + 1));
            slides[slideCount] = s;
            slideCount++;
        } else {
            if (bufCount < g_content_height) {
                buffer[bufCount] = strdup(line);
                bufCount++;
            }
        }
    }
    free(line);
    fclose(fp);
    
    /* If there is leftover content, create a slide from it */
    if (bufCount > 0) {
        Slide *s = malloc(sizeof(Slide));
        s->lines = malloc(sizeof(char*) * g_content_height);
        s->undo_lines = NULL;
        for (int i = 0; i < g_content_height; i++) {
            s->lines[i] = malloc(g_content_width + 1);
            if (i < bufCount) {
                int copyLen = strlen(buffer[i]);
                if (copyLen > g_content_width) copyLen = g_content_width;
                memcpy(s->lines[i], buffer[i], copyLen);
                if (copyLen < g_content_width) {
                    memset(s->lines[i] + copyLen, ' ', g_content_width - copyLen);
                }
            } else {
                memset(s->lines[i], ' ', g_content_width);
            }
            s->lines[i][g_content_width] = '\0';
        }
        for (int i = 0; i < bufCount; i++) {
            free(buffer[i]);
        }
        slides = realloc(slides, sizeof(Slide*) * (slideCount + 1));
        slides[slideCount] = s;
        slideCount++;
    }
    if (slideCount == 0) {
        slideCount = 1;
        slides = malloc(sizeof(Slide*));
        slides[0] = newBlankSlide();
    }
    g_slides = slides;
    g_slide_count = slideCount;
}

/* Save all slides to the file.
   Each slide is written as g_content_height lines followed by a delimiter "----"
   (except after the last slide).
 */
void saveSlides(void) {
    FILE *fp = fopen(g_filename, "w");
    if (!fp) return;
    for (int s = 0; s < g_slide_count; s++) {
        for (int i = 0; i < g_content_height; i++) {
            fprintf(fp, "%.*s\n", g_content_width, g_slides[s]->lines[i]);
        }
        if (s < g_slide_count - 1)
            fprintf(fp, "----\n");
    }
    fclose(fp);
}

/************************************
 * Edit mode functionality.
 *
 * In edit mode, the user can edit the active slide’s full text area (the entire inner region).
 * CTRL+S saves the slides (with a temporary message),
 * CTRL+Z undoes changes,
 * CTRL+E (or ESC) exits edit mode (returning to presentation mode),
 * CTRL+Q quits the app,
 * and the arrow keys move the editing cursor.
 * Note: In edit mode, CTRL+H remains bound to backspace.
 ************************************/
void enterEditMode(void) {
    g_edit_mode = 1;
    Slide *slide = g_slides[g_current_slide];
    int cur_row = 0, cur_col = 0;
    int ch, i;
    
    /* Create an undo backup (deep copy of slide content) */
    if (slide->undo_lines) {
        for (i = 0; i < g_content_height; i++) {
            free(slide->undo_lines[i]);
        }
        free(slide->undo_lines);
    }
    slide->undo_lines = malloc(sizeof(char*) * g_content_height);
    for (i = 0; i < g_content_height; i++) {
        slide->undo_lines[i] = strdup(slide->lines[i]);
    }
    
    while (1) {
        refreshEditScreen(cur_row, cur_col);
        ch = readKey();
        if (ch == CTRL_KEY('S')) {
            /* Save the slides and display a message at bottom left */
            saveSlides();
            dprintf(STDOUT_FILENO, "\x1b[%d;2HSlideset saved!", g_term_rows - 1);
            fsync(STDOUT_FILENO);
            sleep(3);  /* Display the message for roughly 3 seconds */
        } else if (ch == CTRL_KEY('Z')) {
            /* Undo: restore from backup */
            for (i = 0; i < g_content_height; i++) {
                strncpy(slide->lines[i], slide->undo_lines[i], g_content_width);
            }
        } else if (ch == CTRL_KEY('E') || ch == 27) {
            /* CTRL+E or ESC: exit edit mode */
            break;
        } else if (ch == CTRL_KEY('Q')) {
            g_quit = 1;
            break;
        } else if (ch == ARROW_UP) {
            if (cur_row > 0) cur_row--;
        } else if (ch == ARROW_DOWN) {
            if (cur_row < g_content_height - 1) cur_row++;
        } else if (ch == ARROW_LEFT) {
            if (cur_col > 0) cur_col--;
        } else if (ch == ARROW_RIGHT) {
            if (cur_col < g_content_width - 1) cur_col++;
        } else if (isprint(ch)) {
            slide->lines[cur_row][cur_col] = ch;
            if (cur_col < g_content_width - 1)
                cur_col++;
            else if (cur_row < g_content_height - 1) {
                cur_col = 0;
                cur_row++;
            }
        } else if (ch == 127 || ch == CTRL_KEY('H')) {
            /* Handle backspace in edit mode */
            if (cur_col > 0) {
                cur_col--;
                slide->lines[cur_row][cur_col] = ' ';
            } else if (cur_row > 0) {
                cur_row--;
                cur_col = g_content_width - 1;
                slide->lines[cur_row][cur_col] = ' ';
            }
        }
    }
    
    /* Clear the undo backup before exiting edit mode */
    for (i = 0; i < g_content_height; i++) {
        free(slide->undo_lines[i]);
    }
    free(slide->undo_lines);
    slide->undo_lines = NULL;
    
    /* Clear the screen before returning to presentation mode */
    clearScreen();
    g_edit_mode = 0;
}

/************************************
 * Main presentation loop
 ************************************/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s slides_file\n", argv[0]);
        exit(1);
    }
    g_filename = argv[1];
    
    /* Compute dimensions for the full inner region */
    computeDimensions();
    
    /* Load slides from file; slides are allocated to the full inner (editable) area */
    loadSlides(g_filename);
    
    enableRawMode();
    
    while (!g_quit) {
        if (!g_edit_mode)
            refreshPresentationScreen();
        
        int c = readKey();

        /* When not in edit mode, if CTRL+H is pressed, toggle help mode */
        if (!g_edit_mode && c == CTRL_KEY('H')) {
            enterHelpMode();
            continue;
        }
        
        if (c == CTRL_KEY('Q')) {
            g_quit = 1;
            break;
        } else if (c == CTRL_KEY('E')) {
            /* Enter edit mode for the current slide */
            enterEditMode();
            if (g_quit) break;
        } else if (c == ARROW_RIGHT) {
            if (g_current_slide < g_slide_count - 1)
                g_current_slide++;
        } else if (c == ARROW_LEFT) {
            if (g_current_slide > 0)
                g_current_slide--;
        }
        /* Add new slide with CTRL+N (insert after current slide) */
        else if (c == CTRL_KEY('N')) {
            Slide *new_slide = newBlankSlide();
            g_slides = realloc(g_slides, sizeof(Slide*) * (g_slide_count + 1));
            /* Shift slides after the current one to make room */
            for (int i = g_slide_count; i > g_current_slide + 1; i--) {
                g_slides[i] = g_slides[i - 1];
            }
            g_slides[g_current_slide + 1] = new_slide;
            g_slide_count++;
            g_current_slide++; // Move to the new slide
        }
        /* Delete current slide with CTRL+D (do not allow deleting the first slide) */
        else if (c == CTRL_KEY('D')) {
            if (g_current_slide > 0) {
                freeSlide(g_slides[g_current_slide]);
                for (int i = g_current_slide; i < g_slide_count - 1; i++) {
                    g_slides[i] = g_slides[i + 1];
                }
                g_slide_count--;
                g_slides = realloc(g_slides, sizeof(Slide*) * g_slide_count);
                if (g_current_slide >= g_slide_count)
                    g_current_slide = g_slide_count - 1;
            }
        }
    }
    
    clearScreen();
    disableRawMode();
    
    /* Free all allocated slides */
    for (int i = 0; i < g_slide_count; i++) {
        freeSlide(g_slides[i]);
    }
    free(g_slides);
    return 0;
}
