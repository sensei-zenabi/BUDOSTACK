/*
 * table.c
 *
 * Implements a dynamic table for a terminal-based spreadsheet program.
 *
 * Design principles:
 * - The Table structure holds a dynamic 2D array of strings.
 * - The first row is for headers; the first column is an "Index" column.
 * - Provides functions to create/free the table, add rows/columns, set cell values,
 *   print the table (with highlighted cell), load/save CSV files, and retrieve a cell's content.
 *
 * Uses only standard C (C11) and standard libraries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_ROWS 1
#define INITIAL_COLS 1
#define MAX_CELL_LENGTH 256

// Table structure: dynamic 2D array of strings.
typedef struct Table {
    int rows;
    int cols;
    char ***cells;
} Table;

// Helper: duplicate a string.
static char *str_duplicate(const char *s) {
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup)
        strcpy(dup, s);
    return dup;
}

// Helper: free a cell.
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
    t->cells[0] = malloc(sizeof(char *) * t->cols);
    if (!t->cells[0]) {
        free(t->cells);
        free(t);
        return NULL;
    }
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

// Print the table normally.
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

// Print the table with the cell at (highlight_row, highlight_col) highlighted.
void table_print_highlight(const Table *t, int highlight_row, int highlight_col) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        printf("\r");
        for (int j = 0; j < t->cols; j++) {
            if (i == highlight_row && j == highlight_col)
                printf("\033[7m");
            printf("%-15s", t->cells[i][j] ? t->cells[i][j] : "");
            if (i == highlight_row && j == highlight_col)
                printf("\033[0m");
        }
        printf("\n");
    }
}

// Accessor: get number of rows.
int table_get_rows(const Table *t) {
    return t ? t->rows : 0;
}

// Accessor: get number of columns.
int table_get_cols(const Table *t) {
    return t ? t->cols : 0;
}

// Accessor: get content of cell at (row, col).
const char *table_get_cell(const Table *t, int row, int col) {
    if (!t || row < 0 || row >= t->rows || col < 0 || col >= t->cols)
        return "";
    return t->cells[row][col];
}

// Helper: check bounds.
static int in_bounds(const Table *t, int row, int col) {
    return t && row >= 0 && row < t->rows && col >= 0 && col < t->cols;
}

// Set the cell at (row, col) to the given value.
// Editing column 0 (index) is disallowed.
int table_set_cell(Table *t, int row, int col, const char *value) {
    if (!t || !in_bounds(t, row, col)) return -1;
    if (col == 0) return -1;
    free_cell(t->cells[row][col]);
    t->cells[row][col] = str_duplicate(value);
    return 0;
}

// Add a new row at the end. The index column is set automatically.
int table_add_row(Table *t) {
    if (!t) return -1;
    int new_row = t->rows;
    char ***new_cells = realloc(t->cells, sizeof(char **) * (t->rows + 1));
    if (!new_cells) return -1;
    t->cells = new_cells;
    t->cells[new_row] = malloc(sizeof(char *) * t->cols);
    if (!t->cells[new_row]) return -1;
    char index_str[32];
    sprintf(index_str, "%d", new_row);
    t->cells[new_row][0] = str_duplicate(index_str);
    for (int j = 1; j < t->cols; j++) {
        t->cells[new_row][j] = str_duplicate("");
    }
    t->rows++;
    return 0;
}

// Add a new column at the end with the given header.
// Existing rows receive empty cells (header row gets the header text).
int table_add_col(Table *t, const char *header) {
    if (!t) return -1;
    int new_col = t->cols;
    for (int i = 0; i < t->rows; i++) {
        char **new_row = realloc(t->cells[i], sizeof(char *) * (t->cols + 1));
        if (!new_row) return -1;
        t->cells[i] = new_row;
        if (i == 0)
            t->cells[i][new_col] = str_duplicate(header);
        else
            t->cells[i][new_col] = str_duplicate("");
    }
    t->cols++;
    return 0;
}

// Helper: print a CSV field (escaping if needed).
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
                fputc('\"', f);
            fputc(*p, f);
        }
        fputc('\"', f);
    } else {
        fputs(field, f);
    }
}

// Save the table to a CSV file.
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

// Helper: Split a CSV line into fields.
static char **split_csv_line(const char *line, int *count) {
    char **fields = NULL;
    int capacity = 0, num = 0;
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

// Load a table from a CSV file.
Table *table_load_csv(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    char line[1024];
    char ***cells = NULL;
    int rows = 0, cols = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len-1] == '\n')
            line[len-1] = '\0';
        
        int field_count = 0;
        char **fields = split_csv_line(line, &field_count);
        if (rows == 0)
            cols = field_count;
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
