/*
Design principles:
- The library is self-contained and uses dynamic memory to read CSV data.
- A two–pass strategy is used: first, we parse and store all rows to determine maximum width per column.
- Unicode box-drawing characters are used for borders.
- Only standard C libraries are used (stdio.h, stdlib.h, string.h, ctype.h).
- All functions and dynamic memory are properly commented to ease maintenance.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_ROW_CAPACITY   16
#define INITIAL_CELL_CAPACITY   8
#define MAX_LINE_LENGTH       1024

/* Structure to hold one CSV row */
typedef struct {
    char **cells;   /* Array of cell strings */
    int cell_count; /* Number of cells in this row */
} CSVRow;

/* Dynamic array of CSVRow */
typedef struct {
    CSVRow *rows;
    int count;
    int capacity;
} CSVData;

/* Helper function to trim leading and trailing whitespace */
static char *trim(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0)
        return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = '\0';
    return str;
}

/* Custom implementation of strdup, as strdup is not part of C11 standard */
static char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

/* Append a row to CSVData */
static void append_row(CSVData *data, CSVRow row) {
    if (data->count >= data->capacity) {
        data->capacity *= 2;
        data->rows = realloc(data->rows, data->capacity * sizeof(CSVRow));
        if (!data->rows) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
    }
    data->rows[data->count++] = row;
}

/* Free CSVData */
static void free_csv_data(CSVData *data) {
    for (int i = 0; i < data->count; i++) {
        for (int j = 0; j < data->rows[i].cell_count; j++) {
            free(data->rows[i].cells[j]);
        }
        free(data->rows[i].cells);
    }
    free(data->rows);
}

/* Parse a CSV line into a CSVRow structure.
   Note: This simple parser splits on commas and does not handle quoted fields. */
static CSVRow parse_csv_line(char *line) {
    CSVRow row;
    row.cells = NULL;
    row.cell_count = 0;
    int cell_capacity = INITIAL_CELL_CAPACITY;
    row.cells = malloc(cell_capacity * sizeof(char *));
    if (!row.cells) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    
    char *token = strtok(line, ",");
    while (token != NULL) {
        if (row.cell_count >= cell_capacity) {
            cell_capacity *= 2;
            row.cells = realloc(row.cells, cell_capacity * sizeof(char *));
            if (!row.cells) {
                fprintf(stderr, "Memory allocation failed.\n");
                exit(EXIT_FAILURE);
            }
        }
        char *cell = my_strdup(trim(token));
        if (!cell) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        row.cells[row.cell_count++] = cell;
        token = strtok(NULL, ",");
    }
    return row;
}

/* 
Helper to print a horizontal border line.
This function is now defined at file scope to avoid nested functions,
as required by standard C.
*/
static void print_border(const char *left, const char *mid, const char *right, int max_cols, int *col_widths, const char *h_line) {
    printf("%s", left);
    for (int j = 0; j < max_cols; j++) {
        for (int k = 0; k < col_widths[j] + 2; k++) {
            printf("%s", h_line);
        }
        if (j < max_cols - 1)
            printf("%s", mid);
    }
    printf("%s\n", right);
}

/* Visualize CSV file in terminal with Unicode box-drawing borders */
void visualize_csv(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return;
    }
    
    CSVData data;
    data.count = 0;
    data.capacity = INITIAL_ROW_CAPACITY;
    data.rows = malloc(data.capacity * sizeof(CSVRow));
    if (!data.rows) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    
    char line[MAX_LINE_LENGTH];
    int max_cols = 0;
    /* Read file and parse CSV */
    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline if present */
        line[strcspn(line, "\r\n")] = '\0';
        /* Skip empty lines */
        if (line[0] == '\0') continue;
        CSVRow row = parse_csv_line(line);
        if (row.cell_count > max_cols) {
            max_cols = row.cell_count;
        }
        append_row(&data, row);
    }
    fclose(fp);
    
    if (data.count == 0) {
        fprintf(stderr, "No data found in CSV file.\n");
        free_csv_data(&data);
        return;
    }
    
    /* Create an array for maximum column widths */
    int *col_widths = calloc(max_cols, sizeof(int));
    if (!col_widths) {
        fprintf(stderr, "Memory allocation failed.\n");
        free_csv_data(&data);
        exit(EXIT_FAILURE);
    }
    
    /* Compute maximum width for each column */
    for (int i = 0; i < data.count; i++) {
        CSVRow row = data.rows[i];
        for (int j = 0; j < row.cell_count; j++) {
            int len = (int)strlen(row.cells[j]);
            if (len > col_widths[j]) {
                col_widths[j] = len;
            }
        }
    }
    
    /* Define Unicode box-drawing characters */
    const char *top_left     = "┌";
    const char *top_mid      = "┬";
    const char *top_right    = "┐";
    const char *mid_left     = "├";
    const char *mid_mid      = "┼";
    const char *mid_right    = "┤";
    const char *bottom_left  = "└";
    const char *bottom_mid   = "┴";
    const char *bottom_right = "┘";
    const char *h_line       = "─";
    const char *v_line       = "│";
    
    /* Print top border */
    print_border(top_left, top_mid, top_right, max_cols, col_widths, h_line);
    
    /* Print each row with vertical borders.
       For the first row, also print a separator border if more rows exist. */
    for (int i = 0; i < data.count; i++) {
        CSVRow row = data.rows[i];
        printf("%s", v_line);
        for (int j = 0; j < max_cols; j++) {
            char *cell = (j < row.cell_count) ? row.cells[j] : "";
            /* Print cell with padding */
            printf(" %-*s ", col_widths[j], cell);
            printf("%s", v_line);
        }
        printf("\n");
        if (i == 0 && data.count > 1) {
            /* Print separator after header row */
            print_border(mid_left, mid_mid, mid_right, max_cols, col_widths, h_line);
        }
    }
    
    /* Print bottom border */
    print_border(bottom_left, bottom_mid, bottom_right, max_cols, col_widths, h_line);
    
    free(col_widths);
    free_csv_data(&data);
}
