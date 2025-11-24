/*
 * table.c
 *
 * Interactive terminal–based spreadsheet program with live cell editing
 * and Excel–like capabilities.
 *
 * New and enhanced capabilities:
 *  - In‐cell formula evaluation: if a cell’s content begins with '=', it is treated
 *    as a formula supporting basic arithmetic, cell references, SUM() and AVERAGE().
 *  - Toggling the display: CTRL+F switches between raw formula view and evaluated view.
 *  - A new help/shortcut bar is provided to display the available key commands.
 *  - Navigation and editing commands remain as before.
 *  - Displays external row numbers and column letters (Excel–like).
 *  - Copy–paste now automatically adjusts relative cell references. To prevent adjustment,
 *    the user may use the '$' character (e.g. "$A$1").
 *
 * To compile: cc -std=c11 -Wall -Wextra -pedantic -o table_app table.c libtable.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../lib/terminal_layout.h"
#define CTRL_KEY(k) ((k) & 0x1F)
#define MAX_INPUT 256

// Extern declarations from libtable.c
typedef struct Table Table;
extern Table *table_create(void);
extern void table_free(Table *table);
extern int table_set_cell(Table *table, int row, int col, const char *value);
extern int table_add_row(Table *table);
extern int table_add_col(Table *table, const char *header);
extern int table_save_csv(const Table *table, const char *filename);
extern Table *table_load_csv(const char *filename);
extern int table_get_rows(const Table *table);
extern int table_get_cols(const Table *table);
extern void table_print_highlight(const Table *table, int highlight_row, int highlight_col);
extern void table_print_highlight_ex(const Table *table, int highlight_row, int highlight_col, int show_formulas,
                                     int data_row_offset, int data_col_offset, int max_data_rows, int max_data_cols);
extern const char *table_get_cell(const Table *table, int row, int col);
extern int table_delete_column(Table *table, int col);
extern int table_delete_row(Table *table, int row);
// Declaration of the new adjustment function.
extern char *adjust_cell_references(const char *src, int delta_row, int delta_col);

// Global table pointer and current selection.
// Header row (row 0) and index column (col 0) are protected.
Table *g_table = NULL;
int cur_row = 0;   // start at header row
int cur_col = 1;   // first editable column (col 0 is index)

// Clipboard for copy/cut/paste
// When copying a cell that may need relative adjustment, the clipboard will store a
// prefix "CELLREF:row:col:" before the cell content.
static char clipboard[1024] = {0};

// Global flag: if set, display raw formulas instead of evaluated results.
int show_formulas = 0;

// New global flag to control display of the help/shortcut bar.
static int show_help = 0;

// Viewport offsets for scrolling the table view.
static int data_row_offset = 0;
static int data_col_offset = 0;

/*
 * Terminal control functions using system("stty ...")
 */
void enable_raw_mode(void) {
    if (system("stty raw -echo") != 0) {
        perror("stty raw");
    }
}

void disable_raw_mode(void) {
    if (system("stty cooked echo") != 0) {
        perror("stty cooked");
    }
}

void hide_cursor(void) {
    printf("\033[?25l");
    fflush(stdout);
}

void show_cursor(void) {
    printf("\033[?25h");
    fflush(stdout);
}

void clear_screen(void) {
    printf("\033[2J");  // Clear screen
    printf("\033[H");   // Move cursor home
    fflush(stdout);
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

static int get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        if (rows) {
            *rows = BUDOSTACK_TARGET_ROWS;
        }
        if (cols) {
            *cols = BUDOSTACK_TARGET_COLS;
        }
        return 0;
    }
    if (rows) {
        *rows = ws.ws_row;
    }
    if (cols) {
        *cols = ws.ws_col;
    }
    budostack_clamp_terminal_size(rows, cols);
    return 1;
}

static void print_help_bar(void) {
    typedef struct {
        const char *label;
        const char *detail;
    } HelpEntry;
    static const HelpEntry detailed_help[] = {
        {"Detailed Shortcuts:", ""},
        {"Nav", "Home/End +/-5 cols   PgUp/PgDn +/-10 rows   Arrows move cursor"},
        {"Edit", "Ctrl+R add row   Ctrl+N add col   Ctrl+S save   Ctrl+Q quit"},
        {"Formulas", "Ctrl+F toggle view; type '=' to enter expressions"},
        {"Cells", "Del clear   Ctrl+D del col   Ctrl+E del row   Ctrl+C copy   Ctrl+X cut"},
        {"Paste", "Ctrl+V paste   (Ctrl+T hides help)"}
    };
    static const char compact_help[] = "Press CTRL+T for help.";
    const int total_lines = (int)(sizeof(detailed_help) / sizeof(detailed_help[0]));
    int term_rows = 0;

    if (!get_terminal_size(&term_rows, NULL) || term_rows <= 0) {
        term_rows = BUDOSTACK_TARGET_ROWS;
    }

    int start_row = term_rows - total_lines + 1;
    if (start_row < 1) {
        start_row = 1;
    }

    for (int i = 0; i < total_lines; ++i) {
        move_cursor(start_row + i, 1);
        if (show_help) {
            if (detailed_help[i].detail[0] == '\0') {
                printf("\r%s\033[K", detailed_help[i].label);
            } else {
                printf("\r%-10s %s\033[K", detailed_help[i].label, detailed_help[i].detail);
            }
        } else if (i == total_lines - 1) {
            printf("\r%s\033[K", compact_help);
        } else {
            printf("\r\033[K");
        }
    }
    fflush(stdout);
}

/*
 * Save the table as a CSV file.
 */
void save_table(void) {
    char filename[MAX_INPUT];
    move_cursor(24, 1);
    printf("\rEnter filename to save: ");
    fflush(stdout);
    disable_raw_mode();
    if (!fgets(filename, MAX_INPUT, stdin))
        return;
    size_t len = strlen(filename);
    if (len && filename[len - 1] == '\n')
        filename[len - 1] = '\0';
    enable_raw_mode();
    if (table_save_csv(g_table, filename) == 0)
        printf("\rTable saved to '%s'.", filename);
    else
        printf("\rError saving table to '%s'.", filename);
    printf("\rPress any key to continue...");
    fflush(stdout);
    getchar();
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        g_table = table_load_csv(argv[1]);
        if (!g_table) {
            printf("Failed to load '%s'. Creating a new table.\n", argv[1]);
            g_table = table_create();
        }
    } else {
        g_table = table_create();
    }
    if (!g_table) {
        fprintf(stderr, "Error creating table.\n");
        return EXIT_FAILURE;
    }
    // Only auto-add a row and a column when starting a new table.
    if (argc != 2) {
        if (table_get_rows(g_table) < 2)
            table_add_row(g_table);
        if (table_get_cols(g_table) < 2)
            table_add_col(g_table, "Column 1");
    }

    cur_row = 0;
    cur_col = 1;

    enable_raw_mode();
    hide_cursor();

    int running = 1;
    while (running) {
        int term_rows = 0;
        int term_cols = 0;
        if (!get_terminal_size(&term_rows, &term_cols) || term_rows <= 0 || term_cols <= 0) {
            term_rows = BUDOSTACK_TARGET_ROWS;
            term_cols = BUDOSTACK_TARGET_COLS;
        }

        const int help_lines = 6;  // Number of lines reserved by print_help_bar()
        int usable_rows = term_rows - help_lines;
        if (usable_rows < 2)
            usable_rows = 2;

        const int cell_width = 15;
        int visible_data_cols = (term_cols - cell_width) / cell_width;
        if (visible_data_cols < 1)
            visible_data_cols = 1;

        int visible_data_rows = usable_rows - 1; // Leave room for header row
        if (visible_data_rows < 1)
            visible_data_rows = 1;

        int total_data_rows = table_get_rows(g_table) - 1;
        if (total_data_rows < 0)
            total_data_rows = 0;

        int total_data_cols = table_get_cols(g_table) - 1;
        if (total_data_cols < 0)
            total_data_cols = 0;

        if (cur_row == 0) {
            data_row_offset = 0;
        } else {
            int data_row_index = cur_row - 1;
            if (data_row_index < data_row_offset)
                data_row_offset = data_row_index;
            else if (data_row_index >= data_row_offset + visible_data_rows)
                data_row_offset = data_row_index - visible_data_rows + 1;

            int max_row_offset = total_data_rows - visible_data_rows;
            if (max_row_offset < 0)
                max_row_offset = 0;
            if (data_row_offset > max_row_offset)
                data_row_offset = max_row_offset;
        }

        int data_col_index = cur_col - 1;
        if (data_col_index < data_col_offset)
            data_col_offset = data_col_index;
        else if (data_col_index >= data_col_offset + visible_data_cols)
            data_col_offset = data_col_index - visible_data_cols + 1;

        int max_col_offset = total_data_cols - visible_data_cols;
        if (max_col_offset < 0)
            max_col_offset = 0;
        if (data_col_offset > max_col_offset)
            data_col_offset = max_col_offset;

        clear_screen();
        // Print the table within the current viewport
        table_print_highlight_ex(g_table, cur_row, cur_col, show_formulas,
                                 data_row_offset, data_col_offset, visible_data_rows, visible_data_cols);
        // Print the improved help/shortcut bar below the table.
        print_help_bar();

        int c = getchar();
        if (c == 27) {  // ESC sequence for arrows/extended keys
            int second = getchar();
            if (second == '[') {
                int third = getchar();
                if (third >= '0' && third <= '9') {
                    int num = third - '0';
                    int ch;
                    while ((ch = getchar()) >= '0' && ch <= '9')
                        num = num * 10 + (ch - '0');
                    if (ch == '~') {
                        if (num == 1) { // HOME key
                            cur_col = (cur_col - 5 < 1) ? 1 : cur_col - 5;
                        } else if (num == 4) { // END key
                            int maxcol = table_get_cols(g_table) - 1;
                            cur_col = (cur_col + 5 > maxcol) ? maxcol : cur_col + 5;
                        } else if (num == 5) { // PGUP
                            cur_row = (cur_row - 10 < 0) ? 0 : cur_row - 10;
                        } else if (num == 6) { // PGDN
                            int maxrow = table_get_rows(g_table) - 1;
                            cur_row = (cur_row + 10 > maxrow) ? maxrow : cur_row + 10;
                        } else if (num == 3) { // DEL key: clear current cell
                            table_set_cell(g_table, cur_row, cur_col, "");
                        }
                    }
                } else if (third == 'A') {       // Up arrow
                    if (cur_row > 0)
                        cur_row--;
                } else if (third == 'B') {       // Down arrow
                    if (cur_row < table_get_rows(g_table) - 1)
                        cur_row++;
                } else if (third == 'C') {       // Right arrow
                    if (cur_col < table_get_cols(g_table) - 1)
                        cur_col++;
                } else if (third == 'D') {       // Left arrow
                    if (cur_col > 1)
                        cur_col--;
                } else if (third == 'H') {       // Terminal sends ESC [ H for Home
                    cur_col = (cur_col - 5 < 1) ? 1 : cur_col - 5;
                } else if (third == 'F') {       // or ESC [ F for End
                    int maxcol = table_get_cols(g_table) - 1;
                    cur_col = (cur_col + 5 > maxcol) ? maxcol : cur_col + 5;
                }
            }
            continue;
        } else if (c == CTRL_KEY('S')) {
            save_table();
        } else if (c == CTRL_KEY('Q')) {
            running = 0;
        } else if (c == CTRL_KEY('R')) {  // Add row
            table_add_row(g_table);
            cur_row = table_get_rows(g_table) - 1;
            if (cur_col >= table_get_cols(g_table))
                cur_col = table_get_cols(g_table) - 1;
        } else if (c == CTRL_KEY('N')) {  // Add column (with default header)
            int new_col_number = table_get_cols(g_table);  // includes index column
            char default_header[MAX_INPUT];
            snprintf(default_header, sizeof(default_header), "Column %d", new_col_number);
            table_add_col(g_table, default_header);
        } else if (c == CTRL_KEY('D')) {  // Delete column
            if (cur_col > 0) { // Do not delete index column
                if (table_delete_column(g_table, cur_col) == 0) {
                    int maxcol = table_get_cols(g_table) - 1;
                    if (cur_col > maxcol)
                        cur_col = maxcol;
                }
            }
        } else if (c == CTRL_KEY('E')) {  // Delete row
            if (cur_row > 0) { // Do not delete header row
                if (table_delete_row(g_table, cur_row) == 0) {
                    int maxrow = table_get_rows(g_table) - 1;
                    if (cur_row > maxrow)
                        cur_row = maxrow;
                }
            }
        } else if (c == CTRL_KEY('c')) {  // Copy cell
            const char *content = table_get_cell(g_table, cur_row, cur_col);
            if (content && (content[0] == '=' || isalpha(content[0]) || content[0] == '$')) {
                snprintf(clipboard, sizeof(clipboard), "CELLREF:%d:%d:%s", cur_row, cur_col, content);
            } else {
                strncpy(clipboard, content, sizeof(clipboard)-1);
                clipboard[sizeof(clipboard)-1] = '\0';
            }
        } else if (c == CTRL_KEY('x')) {  // Cut cell
            const char *content = table_get_cell(g_table, cur_row, cur_col);
            if (content && (content[0] == '=' || isalpha(content[0]) || content[0] == '$')) {
                snprintf(clipboard, sizeof(clipboard), "CELLREF:%d:%d:%s", cur_row, cur_col, content);
            } else {
                strncpy(clipboard, content, sizeof(clipboard)-1);
                clipboard[sizeof(clipboard)-1] = '\0';
            }
            table_set_cell(g_table, cur_row, cur_col, "");
        } else if (c == CTRL_KEY('v')) {  // Paste
            if (strncmp(clipboard, "CELLREF:", 8) == 0) {
                int src_row = 0, src_col = 0;
                const char *p = clipboard + 8;
                src_row = atoi(p);
                p = strchr(p, ':');
                if (p) {
                    src_col = atoi(p+1);
                    p = strchr(p+1, ':');
                }
                if (p) {
                    const char *cell_content = p + 1;
                    int delta_row = cur_row - src_row;
                    int delta_col = cur_col - src_col;
                    char *adjusted = adjust_cell_references(cell_content, delta_row, delta_col);
                    table_set_cell(g_table, cur_row, cur_col, adjusted);
                    free(adjusted);
                } else {
                    table_set_cell(g_table, cur_row, cur_col, clipboard);
                }
            } else {
                table_set_cell(g_table, cur_row, cur_col, clipboard);
            }
        } else if (c == CTRL_KEY('F')) {  // Toggle formula view
            show_formulas = !show_formulas;
        } else if (c == CTRL_KEY('T')) {  // Toggle help/shortcut bar
            show_help = !show_help;
            continue;
        } else if (c == 127 || c == 8) {  // Backspace
            const char *curr = table_get_cell(g_table, cur_row, cur_col);
            char buffer[1024] = {0};
            strncpy(buffer, curr, sizeof(buffer)-1);
            int blen = strlen(buffer);
            if (blen > 0)
                buffer[blen-1] = '\0';
            table_set_cell(g_table, cur_row, cur_col, buffer);
        } else if (c >= 32 && c < 127) {  // Printable characters (live editing)
            const char *curr = table_get_cell(g_table, cur_row, cur_col);
            char buffer[1024] = {0};
            snprintf(buffer, sizeof(buffer), "%s%c", curr, c);
            table_set_cell(g_table, cur_row, cur_col, buffer);
        }
        // Other non-printable keys are ignored.
    }

    show_cursor();
    disable_raw_mode();
    clear_screen();
    table_free(g_table);
    return EXIT_SUCCESS;
}
