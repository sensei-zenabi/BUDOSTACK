/*
 * main.c
 *
 * Interactive terminal-based spreadsheet program using arrow keys.
 *
 * Design principles:
 * - Interactive interface in raw mode, using ANSI escape sequences to clear the screen,
 *   reposition the cursor, and hide the blinking cursor.
 * - Uses system("stty raw -echo") and system("stty cooked echo") to toggle raw mode.
 * - The currently selected cell is highlighted using inverse video (ANSI code \033[7m).
 * - Supports arrow key navigation, editing (header or data cell), adding rows/columns, and saving.
 * - Uses functions from table.c (declared as externs) to manage table data without exposing internals.
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

// Global table pointer and current selection.
// We now allow the header row (row 0) to be selected so that columns can be renamed.
Table *g_table = NULL;
int cur_row = 0;   // start at header row
int cur_col = 1;   // first editable column (column 0 is the index)

// Terminal control functions using system("stty ...")
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
    printf("\033[H");   // Move cursor to home position
    fflush(stdout);
}

// Move the cursor to the given row and column (1-indexed).
void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

// Print usage instructions at the top of the screen.
void print_instructions(void) {
    // \r ensures each new line starts at column 0.
    printf("\rArrow keys: move  |  'e': edit cell  |  'a': add row  |  'c': add column\n");
    printf("\rCtrl+S: save  |  Ctrl+Q: quit\n\n");
    fflush(stdout);
}

// Temporarily switch to cooked mode to get a line of input.
void get_line(char *buffer, size_t size) {
    disable_raw_mode();
    fgets(buffer, size, stdin);
    size_t len = strlen(buffer);
    if (len && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';
    enable_raw_mode();
}

// Edit the current cell.
// For header row (row 0, col > 0), this renames the column header.
// For data cells (row > 0), it edits the cell's contents.
// The index column (col 0) is not editable.
void edit_cell(void) {
    if (cur_col == 0) return; // never edit index column
    char input[MAX_INPUT];
    // Position the prompt near the bottom.
    move_cursor(20, 1);
    if (cur_row == 0)
        printf("\rRename header for column %d: ", cur_col);
    else
        printf("\rEnter new value for cell (%d, %d): ", cur_row, cur_col);
    fflush(stdout);
    get_line(input, MAX_INPUT);
    table_set_cell(g_table, cur_row, cur_col, input);
}

// Save the table to a CSV file.
void save_table(void) {
    char filename[MAX_INPUT];
    move_cursor(22, 1);
    printf("\rEnter filename to save: ");
    fflush(stdout);
    get_line(filename, MAX_INPUT);
    if (table_save_csv(g_table, filename) == 0)
        printf("\rTable saved to '%s'.", filename);
    else
        printf("\rError saving table to '%s'.", filename);
    printf("\rPress any key to continue...");
    fflush(stdout);
    getchar();
}

int main(int argc, char *argv[]) {
    // Load an existing CSV file if provided as a command-line argument; otherwise create a new table.
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
    // Note: header row (row 0) already exists.
    if (table_get_rows(g_table) < 2)
        table_add_row(g_table);
    if (table_get_cols(g_table) < 2)
        table_add_col(g_table, "Column1");
    
    // Set initial selection to header row, first data column.
    cur_row = 0;
    cur_col = 1;
    
    // Enable raw mode and hide the cursor for interactive key detection.
    enable_raw_mode();
    hide_cursor();
    
    int running = 1;
    while (running) {
        clear_screen();
        print_instructions();
        // Print the table with the current cell highlighted.
        table_print_highlight(g_table, cur_row, cur_col);
        
        int c = getchar();
        if (c == 27) {  // Escape sequence (arrow keys)
            int next1 = getchar();
            if (next1 == '[') {
                int next2 = getchar();
                // Allow movement into header row (row 0) now.
                if (next2 == 'A') {       // Up arrow
                    if (cur_row > 0)
                        cur_row--;
                } else if (next2 == 'B') {  // Down arrow
                    if (cur_row < table_get_rows(g_table) - 1)
                        cur_row++;
                } else if (next2 == 'C') {  // Right arrow
                    if (cur_col < table_get_cols(g_table) - 1)
                        cur_col++;
                } else if (next2 == 'D') {  // Left arrow
                    if (cur_col > 1)  // Do not allow leaving index column.
                        cur_col--;
                }
            }
        } else if (c == 'e') {
            edit_cell();
        } else if (c == 'a') {
            table_add_row(g_table);
            // If we were in header row, move selection to first cell of new row.
            cur_row = table_get_rows(g_table) - 1;
            cur_col = (cur_col < table_get_cols(g_table)) ? cur_col : table_get_cols(g_table) - 1;
        } else if (c == 'c') {
            char header[MAX_INPUT];
            move_cursor(20, 1);
            printf("\rEnter header for new column: ");
            fflush(stdout);
            get_line(header, MAX_INPUT);
            table_add_col(g_table, header);
        } else if (c == CTRL_KEY('S')) {
            save_table();
        } else if (c == CTRL_KEY('Q')) {
            running = 0;
        }
    }
    
    // Before exiting, restore cooked mode and show the cursor.
    show_cursor();
    disable_raw_mode();
    clear_screen();
    table_free(g_table);
    return EXIT_SUCCESS;
}
