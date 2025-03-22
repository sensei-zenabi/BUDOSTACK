/*
 * table.c
 *
 * Implements a dynamic table to be used by a terminal-based spreadsheet program.
 *
 * Design principles:
 * - The Table structure holds a dynamic 2D array of strings.
 * - The first row is reserved for headers; the first column is always an "Index" column.
 * - Functions provide operations to create/free the table, add rows/columns, set cell values,
 *   print the table, and load/save CSV files.
 * - Accessor functions table_get_rows() and table_get_cols() allow other modules (such as main.c)
 *   to query the table dimensions without exposing internal structure details.
 * - The new function table_print_highlight() prints the table with a selected cell highlighted.
 *
 * This implementation uses only standard C (C11) and standard libraries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_ROWS 1
#define INITIAL_COLS 1
#define MAX_CELL_LENGTH 256

// Table structure: cells is a 2D dynamic array (rows x cols)
typedef struct Table {
    int rows;
    int cols;
    char ***cells; // cells[row][col] is a dynamically allocated string
} Table;

// Helper to allocate a copy of a string.
static char *str_duplicate(const char *s) {
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup)
        strcpy(dup, s);
    return dup;
}

// Helper to free a cell string.
static void free_cell(char *s) {
    free(s);
}

// Create an empty table with one header row and one index column.
Table *table_create(void) {
    Table *t = malloc(sizeof(Table));
    if (!t) return NULL;
    t->rows = INITIAL_ROWS;
    t->cols = INITIAL_COLS;
    t->cells = malloc(sizeof(char **) * t->rows);
    if (!t->cells) {
        free(t);
        return NULL;
    }
    // Allocate header row.
    t->cells[0] = malloc(sizeof(char *) * t->cols);
    if (!t->cells[0]) {
        free(t->cells);
        free(t);
        return NULL;
    }
    // Set the index column header.
    t->cells[0][0] = str_duplicate("Index");
    return t;
}

// Free all memory associated with the table.
void table_free(Table *t) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        for (int j = 0; j < t->cols; j++) {
            if (t->cells[i][j])
                free_cell(t->cells[i][j]);
        }
        free(t->cells[i]);
    }
    free(t->cells);
    free(t);
}

// Print the table to standard output.
void table_print(const Table *t) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        printf("\r");
        for (int j = 0; j < t->cols; j++) {
            printf("%-15s", t->cells[i][j] ? t->cells[i][j] : "");
        }
        printf("\n");
    }
}

// Print the table with a highlighted cell (inverse video).
void table_print_highlight(const Table *t, int highlight_row, int highlight_col) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        printf("\r");
        for (int j = 0; j < t->cols; j++) {
            if (i == highlight_row && j == highlight_col)
                printf("\033[7m");  // Start inverse video
            printf("%-15s", t->cells[i][j] ? t->cells[i][j] : "");
            if (i == highlight_row && j == highlight_col)
                printf("\033[0m");  // Reset attributes
        }
        printf("\n");
    }
}

// Accessor function to get the number of rows.
int table_get_rows(const Table *t) {
    return t ? t->rows : 0;
}

// Accessor function to get the number of columns.
int table_get_cols(const Table *t) {
    return t ? t->cols : 0;
}

// Ensure that row and column indexes are within bounds.
static int in_bounds(const Table *t, int row, int col) {
    return t && row >= 0 && row < t->rows && col >= 0 && col < t->cols;
}

// Set the cell at (row, col) to the given value. For index column (col 0) disallow changes.
int table_set_cell(Table *t, int row, int col, const char *value) {
    if (!t || !in_bounds(t, row, col)) return -1;
    if (col == 0) {
        // Disallow editing index column.
        return -1;
    }
    // Free old value.
    free_cell(t->cells[row][col]);
    t->cells[row][col] = str_duplicate(value);
    return 0;
}

// Add a new row at the end of the table. The first cell (index column) is automatically set.
int table_add_row(Table *t) {
    if (!t) return -1;
    int new_row = t->rows;
    // Reallocate table->cells to hold one more row.
    char ***new_cells = realloc(t->cells, sizeof(char **) * (t->rows + 1));
    if (!new_cells) return -1;
    t->cells = new_cells;
    // Allocate new row.
    t->cells[new_row] = malloc(sizeof(char *) * t->cols);
    if (!t->cells[new_row]) return -1;
    // Allocate index cell.
    char index_str[32];
    sprintf(index_str, "%d", new_row); // row number as string
    t->cells[new_row][0] = str_duplicate(index_str);
    // For other columns, initialize with empty string.
    for (int j = 1; j < t->cols; j++) {
        t->cells[new_row][j] = str_duplicate("");
    }
    t->rows++;
    return 0;
}

// Add a new column at the end of the table with the provided header. All existing rows get empty cells.
int table_add_col(Table *t, const char *header) {
    if (!t) return -1;
    int new_col = t->cols;
    // For each row, reallocate the row to have one more cell.
    for (int i = 0; i < t->rows; i++) {
        char **new_row = realloc(t->cells[i], sizeof(char *) * (t->cols + 1));
        if (!new_row) return -1;
        t->cells[i] = new_row;
        if (i == 0) {
            // Header row: set header for new column.
            t->cells[i][new_col] = str_duplicate(header);
        } else {
            // Data rows: initialize as empty.
            t->cells[i][new_col] = str_duplicate("");
        }
    }
    t->cols++;
    return 0;
}

// Helper to escape a CSV field if needed.
static void fprint_csv_field(FILE *f, const char *field) {
    int need_quotes = 0;
    for (const char *p = field; *p; p++) {
        if (*p == ',' || *p == '\"' || *p == '\n') {
            need_quotes = 1;
            break;
        }
    }
    if (need_quotes) {
        fputc('\"', f);
        for (const char *p = field; *p; p++) {
            if (*p == '\"')
                fputc('\"', f); // double the quote
            fputc(*p, f);
        }
        fputc('\"', f);
    } else {
        fputs(field, f);
    }
}

// Save the table to a CSV file. Returns 0 on success.
int table_save_csv(const Table *t, const char *filename) {
    if (!t || !filename) return -1;
    FILE *f = fopen(filename, "w");
    if (!f) return -1;
    for (int i = 0; i < t->rows; i++) {
        for (int j = 0; j < t->cols; j++) {
            if (t->cells[i][j])
                fprint_csv_field(f, t->cells[i][j]);
            if (j < t->cols - 1)
                fputc(',', f);
        }
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

// Split a CSV line into fields. Returns an array of strings and sets *count to the number of fields.
// The returned array and each field must be freed by the caller.
static char **split_csv_line(const char *line, int *count) {
    char **fields = NULL;
    int capacity = 0;
    int num = 0;
    const char *p = line;
    while (*p) {
        if (num >= capacity) {
            capacity = capacity ? capacity * 2 : 4;
            char **temp = realloc(fields, capacity * sizeof(char *));
            if (!temp) break;
            fields = temp;
        }
        char buffer[MAX_CELL_LENGTH] = {0};
        int buf_index = 0;
        if (*p == '\"') {
            p++;
            while (*p && !(*p == '\"' && (*(p+1) != '\"'))) {
                if (*p == '\"' && *(p+1) == '\"') {
                    buffer[buf_index++] = '\"';
                    p += 2;
                } else {
                    buffer[buf_index++] = *p;
                    p++;
                }
                if (buf_index >= MAX_CELL_LENGTH - 1) break;
            }
            if (*p == '\"') p++;
        } else {
            while (*p && *p != ',') {
                buffer[buf_index++] = *p;
                p++;
                if (buf_index >= MAX_CELL_LENGTH - 1) break;
            }
        }
        buffer[buf_index] = '\0';
        fields[num++] = str_duplicate(buffer);
        if (*p == ',') p++;
    }
    *count = num;
    return fields;
}

// Load a table from a CSV file. Returns a new Table pointer on success.
Table *table_load_csv(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    char line[1024];
    char ***cells = NULL;
    int rows = 0, cols = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = '\0';
        
        int field_count = 0;
        char **fields = split_csv_line(line, &field_count);
        if (rows == 0) {
            cols = field_count;
        }
        char ***temp = realloc(cells, sizeof(char **) * (rows + 1));
        if (!temp) break;
        cells = temp;
        cells[rows] = malloc(sizeof(char *) * cols);
        for (int j = 0; j < cols; j++) {
            if (j < field_count)
                cells[rows][j] = fields[j];
            else
                cells[rows][j] = str_duplicate("");
        }
        free(fields);
        rows++;
    }
    fclose(f);
    if (rows == 0) return NULL;
    
    Table *t = malloc(sizeof(Table));
    if (!t) return NULL;
    t->rows = rows;
    t->cols = cols;
    t->cells = cells;
    return t;
}
