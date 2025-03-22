/*
 * main.c
 *
 * Interactive terminal-based spreadsheet program with live cell editing.
 *
 * Design principles:
 * - Interactive interface in raw mode with live editing:
 *   non-CTRL keys are used to modify cell content directly (including space and backspace).
 * - Shortcuts are now CTRL+<key>:
 *      CTRL+S: Save, CTRL+Q: Quit, CTRL+R: Add row, CTRL+N: Add column.
 * - Arrow keys are used for navigation.
 * - Uses ANSI escape sequences to clear the screen, reposition the cursor,
 *   and hide the blinking cursor.
 * - Uses functions from table.c (which now also provides table_get_cell()).
 *
 * To compile: cc -std=c11 -Wall -Wextra -pedantic -o table_app main.c table.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CTRL_KEY(k) ((k) & 0x1F)
#define MAX_INPUT 256

// Extern declarations for table functions from table.c
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
extern const char *table_get_cell(const Table *table, int row, int col);

// Global table pointer and current selection.
// We allow the header row (row 0) to be edited (except for the index column).
Table *g_table = NULL;
int cur_row = 0;   // start at header row
int cur_col = 1;   // first editable column (column 0 is the index)

// Terminal control functions (using system("stty ...") for raw mode)
void enable_raw_mode(void) {
    system("stty raw -echo");
}

void disable_raw_mode(void) {
    system("stty cooked echo");
}

// Hide and show the cursor using ANSI escape codes.
void hide_cursor(void) {
    printf("\033[?25l");
    fflush(stdout);
}

void show_cursor(void) {
    printf("\033[?25h");
    fflush(stdout);
}

// Clear the terminal screen and reposition the cursor to the top-left.
void clear_screen(void) {
    printf("\033[2J");  // Clear screen
    printf("\033[H");   // Move cursor to home
    fflush(stdout);
}

// Move the cursor to the given row and column (1-indexed).
void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

// Print usage instructions at the top.
void print_instructions(void) {
    printf("\rCTRL+R: add row  |  CTRL+N: add column  |  CTRL+S: save  |  CTRL+Q: quit\n");
    printf("\rArrow keys: move  (live editing: type to modify cell, use backspace to delete)\n\n");
    fflush(stdout);
}

// Save the table to a CSV file.
void save_table(void) {
    char filename[MAX_INPUT];
    move_cursor(22, 1);
    printf("\rEnter filename to save: ");
    fflush(stdout);
    // Switch temporarily to cooked mode to get the line.
    disable_raw_mode();
    fgets(filename, MAX_INPUT, stdin);
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
    // Load CSV file if given; otherwise create new table.
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
    // Ensure at least one data row and one data column exist.
    if (table_get_rows(g_table) < 2)
        table_add_row(g_table);
    if (table_get_cols(g_table) < 2)
        table_add_col(g_table, "Column1");
    
    // Set initial selection to header row, first data column.
    cur_row = 0;
    cur_col = 1;
    
    // Enable raw mode and hide cursor.
    enable_raw_mode();
    hide_cursor();
    
    int running = 1;
    while (running) {
        clear_screen();
        print_instructions();
        table_print_highlight(g_table, cur_row, cur_col);
        
        int c = getchar();
        if (c == 27) {  // Escape sequence for arrow keys.
            int next1 = getchar();
            if (next1 == '[') {
                int next2 = getchar();
                if (next2 == 'A') {       // Up
                    if (cur_row > 0)
                        cur_row--;
                } else if (next2 == 'B') {  // Down
                    if (cur_row < table_get_rows(g_table) - 1)
                        cur_row++;
                } else if (next2 == 'C') {  // Right
                    if (cur_col < table_get_cols(g_table) - 1)
                        cur_col++;
                } else if (next2 == 'D') {  // Left
                    if (cur_col > 1)  // Do not leave index column.
                        cur_col--;
                }
            }
        } else if (c == CTRL_KEY('S')) {
            save_table();
        } else if (c == CTRL_KEY('Q')) {
            running = 0;
        } else if (c == CTRL_KEY('R')) {  // Add row
            table_add_row(g_table);
            cur_row = table_get_rows(g_table) - 1;
            if (cur_col >= table_get_cols(g_table))
                cur_col = table_get_cols(g_table) - 1;
        } else if (c == CTRL_KEY('N')) {  // Add column (New column)
            char header[MAX_INPUT];
            move_cursor(22, 1);
            printf("\rEnter header for new column: ");
            fflush(stdout);
            disable_raw_mode();
            fgets(header, MAX_INPUT, stdin);
            size_t len = strlen(header);
            if (len && header[len - 1] == '\n')
                header[len - 1] = '\0';
            enable_raw_mode();
            table_add_col(g_table, header);
        } else if (c == 127 || c == 8) {  // Backspace
            const char *curr = table_get_cell(g_table, cur_row, cur_col);
            char buffer[1024] = {0};
            strncpy(buffer, curr, sizeof(buffer)-1);
            int blen = strlen(buffer);
            if (blen > 0)
                buffer[blen-1] = '\0';
            table_set_cell(g_table, cur_row, cur_col, buffer);
        } else if (c >= 32 && c < 127) {  // Printable characters.
            const char *curr = table_get_cell(g_table, cur_row, cur_col);
            char buffer[1024] = {0};
            snprintf(buffer, sizeof(buffer), "%s%c", curr, c);
            table_set_cell(g_table, cur_row, cur_col, buffer);
        }
        // Other non-printable keys are ignored.
    }
    
    // Restore terminal state.
    show_cursor();
    disable_raw_mode();
    clear_screen();
    table_free(g_table);
    return EXIT_SUCCESS;
}
