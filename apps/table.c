/*
 * main.c
 *
 * Interactive terminal-based spreadsheet program with live cell editing.
 *
 * New capabilities:
 * - Navigation with arrow keys plus extended keys:
 *      HOME: move left 5 columns  
 *      END: move right 5 columns  
 *      PGUP: move up 10 rows  
 *      PGDN: move down 10 rows  
 *      DEL: clear current cell
 * - CTRL+, deletes current column (except index column)
 * - CTRL+. deletes current row (except header row)
 * - Clipboard shortcuts:
 *      CTRL+c: copy cell content  
 *      CTRL+x: cut cell content  
 *      CTRL+v: paste into current cell
 *
 * Other shortcuts (all in live-edit mode):
 *      CTRL+S: save, CTRL+Q: quit, CTRL+R: add row, CTRL+N: add column (with default header)
 * - Printable keys are directly appended to cell content;
 *   backspace deletes the last character.
 *
 * Uses ANSI escape sequences to clear the screen, reposition the cursor,
 * and hide the blinking cursor.
 *
 * To compile: cc -std=c11 -Wall -Wextra -pedantic -o table_app main.c table.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CTRL_KEY(k) ((k) & 0x1F)
#define MAX_INPUT 256

// Extern declarations from table.c
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
extern int table_delete_column(Table *table, int col);
extern int table_delete_row(Table *table, int row);

// Global table pointer and current selection.
// Header row (row 0) and index column (col 0) are protected (not deleted/edited).
Table *g_table = NULL;
int cur_row = 0;   // start at header row
int cur_col = 1;   // first editable column (col 0 is index)

static char clipboard[1024] = {0};  // Clipboard for copy/cut/paste

// Terminal control functions using system("stty ...")
void enable_raw_mode(void) {
    system("stty raw -echo");
}

void disable_raw_mode(void) {
    system("stty cooked echo");
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
    printf("\033[H");   // Move cursor to home position
    fflush(stdout);
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

void print_instructions(void) {
    printf("\rCTRL+R: add row  |  CTRL+N: add column  |  CTRL+S: save  |  CTRL+Q: quit\n");
    printf("\rHOME: ←5 cols  |  END: →5 cols  |  PGUP: ↑10 rows  |  PGDN: ↓10 rows  |  DEL: clear cell\n");
    printf("\rCTRL+,: delete column  |  CTRL+.: delete row\n");
    printf("\rCTRL+c: copy  |  CTRL+x: cut  |  CTRL+v: paste\n");
    printf("\rArrow keys: move  (live editing: type to modify cell, backspace to delete)\n\n");
    fflush(stdout);
}

void save_table(void) {
    char filename[MAX_INPUT];
    move_cursor(24, 1);
    printf("\rEnter filename to save: ");
    fflush(stdout);
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
    if (table_get_rows(g_table) < 2)
        table_add_row(g_table);
    if (table_get_cols(g_table) < 2)
        table_add_col(g_table, "Column 1");
    
    cur_row = 0;
    cur_col = 1;
    
    enable_raw_mode();
    hide_cursor();
    
    int running = 1;
    while (running) {
        clear_screen();
        print_instructions();
        table_print_highlight(g_table, cur_row, cur_col);
        
        int c = getchar();
        if (c == 27) {  // ESC: potential arrow or extended key sequence
            int second = getchar();
            if (second == '[') {
                int third = getchar();
                if (third >= '0' && third <= '9') {
                    // Extended sequence: accumulate number.
                    int num = third - '0';
                    int ch;
                    while ((ch = getchar()) >= '0' && ch <= '9') {
                        num = num * 10 + (ch - '0');
                    }
                    // Expect a '~'
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
                } else if (third == 'H') {       // Some terminals send ESC [ H for Home
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
        } else if (c == CTRL_KEY('N')) {  // Add column with default header
            int new_col_number = table_get_cols(g_table);  // includes index column
            char default_header[MAX_INPUT];
            snprintf(default_header, sizeof(default_header), "Column %d", new_col_number);
            table_add_col(g_table, default_header);
        } else if (c == CTRL_KEY(',')) {  // Delete column
            if (cur_col > 0) { // Do not delete index column
                if (table_delete_column(g_table, cur_col) == 0) {
                    int maxcol = table_get_cols(g_table) - 1;
                    if (cur_col > maxcol)
                        cur_col = maxcol;
                }
            }
        } else if (c == CTRL_KEY('.')) {  // Delete row
            if (cur_row > 0) { // Do not delete header row
                if (table_delete_row(g_table, cur_row) == 0) {
                    int maxrow = table_get_rows(g_table) - 1;
                    if (cur_row > maxrow)
                        cur_row = maxrow;
                }
            }
        } else if (c == CTRL_KEY('c')) {  // Copy cell
            const char *content = table_get_cell(g_table, cur_row, cur_col);
            strncpy(clipboard, content, sizeof(clipboard)-1);
            clipboard[sizeof(clipboard)-1] = '\0';
        } else if (c == CTRL_KEY('x')) {  // Cut cell
            const char *content = table_get_cell(g_table, cur_row, cur_col);
            strncpy(clipboard, content, sizeof(clipboard)-1);
            clipboard[sizeof(clipboard)-1] = '\0';
            table_set_cell(g_table, cur_row, cur_col, "");
        } else if (c == CTRL_KEY('v')) {  // Paste
            table_set_cell(g_table, cur_row, cur_col, clipboard);
        } else if (c == 127 || c == 8) {  // Backspace
            const char *curr = table_get_cell(g_table, cur_row, cur_col);
            char buffer[1024] = {0};
            strncpy(buffer, curr, sizeof(buffer)-1);
            int blen = strlen(buffer);
            if (blen > 0)
                buffer[blen-1] = '\0';
            table_set_cell(g_table, cur_row, cur_col, buffer);
        } else if (c >= 32 && c < 127) {  // Printable characters
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
