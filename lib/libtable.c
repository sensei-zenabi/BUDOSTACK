#define _POSIX_C_SOURCE 200809L
/*
 * libtable.c
 *
 * Implements a dynamic table for a terminal-based spreadsheet program.
 *
 * Design principles:
 *  - The Table structure holds a dynamic 2D array of strings.
 *  - The first row is reserved for headers; the first column is always an "Index" column.
 *  - Provides functions to create/free the table, add rows/columns, edit cells,
 *    load/save CSV files, and now supports Excel-like formulas.
 *
 * Enhancements:
 *  - If a cell’s value begins with '=', it is interpreted as a formula.
 *  - The built-in parser supports numeric constants, arithmetic operators (+, -, *, /),
 *    parentheses, cell references (e.g., B2), and the functions SUM() and AVERAGE() over ranges.
 *  - A new printing function (table_print_highlight_ex) lets the caller choose whether to
 *    show raw cell values or to evaluate formulas on the fly.
 *
 * To compile: cc -std=c11 -Wall -Wextra -pedantic -o table_app table.c libtable.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>  // For strcasecmp

#define INITIAL_ROWS 1
#define INITIAL_COLS 1
#define MAX_CELL_LENGTH 256

// Define the Table structure and alias.
typedef struct Table {
    int rows;
    int cols;
    char ***cells;  // cells[row][col] is a dynamically allocated string.
} Table;

// Forward declaration for the formula evaluation function.
char *evaluate_formula(const Table *t, const char *formula);

/*
 * Basic table functions: create, free, print, access, add row/column,
 * delete row/column, save/load CSV.
 */

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
    t->cells[0][0] = strdup("Index");
    return t;
}

void table_free(Table *t) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        for (int j = 0; j < t->cols; j++) {
            if (t->cells[i][j])
                free(t->cells[i][j]);
        }
        free(t->cells[i]);
    }
    free(t->cells);
    free(t);
}

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

/*
 * The new printing function that can either display raw cell values
 * or, if a cell value begins with '=', evaluate it as a formula.
 * 'highlight_row' and 'highlight_col' indicate the currently selected cell.
 * 'show_formulas' if nonzero forces raw display.
 */
void table_print_highlight_ex(const Table *t, int highlight_row, int highlight_col, int show_formulas) {
    if (!t) return;
    for (int i = 0; i < t->rows; i++) {
        printf("\r");
        for (int j = 0; j < t->cols; j++) {
            char buffer[64];
            const char *cell_content = t->cells[i][j] ? t->cells[i][j] : "";
            if (!show_formulas && cell_content[0] == '=') {
                char *eval_result = evaluate_formula(t, cell_content);
                snprintf(buffer, sizeof(buffer), "%s", eval_result);
                free(eval_result);
            } else {
                snprintf(buffer, sizeof(buffer), "%s", cell_content);
            }
            if (i == highlight_row && j == highlight_col)
                printf("\033[7m");  // Start inverse video.
            printf("%-15s", buffer);
            if (i == highlight_row && j == highlight_col)
                printf("\033[0m");  // Reset attributes.
        }
        printf("\n");
    }
}

// The previous highlight-print simply calls the extended version.
void table_print_highlight(const Table *t, int highlight_row, int highlight_col) {
    table_print_highlight_ex(t, highlight_row, highlight_col, 0);
}

int table_get_rows(const Table *t) {
    return t ? t->rows : 0;
}

int table_get_cols(const Table *t) {
    return t ? t->cols : 0;
}

const char *table_get_cell(const Table *t, int row, int col) {
    if (!t || row < 0 || row >= t->rows || col < 0 || col >= t->cols)
        return "";
    return t->cells[row][col];
}

// Check if row and col are in bounds.
static int in_bounds(const Table *t, int row, int col) {
    return t && row >= 0 && row < t->rows && col >= 0 && col < t->cols;
}

// Set cell at (row, col) to given value.
int table_set_cell(Table *t, int row, int col, const char *value) {
    if (!t || !in_bounds(t, row, col)) return -1;
    if (col == 0) return -1; // do not edit index column
    free(t->cells[row][col]);
    t->cells[row][col] = strdup(value);
    return 0;
}

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
    t->cells[new_row][0] = strdup(index_str);
    for (int j = 1; j < t->cols; j++) {
        t->cells[new_row][j] = strdup("");
    }
    t->rows++;
    return 0;
}

int table_add_col(Table *t, const char *header) {
    if (!t) return -1;
    int new_col = t->cols;
    for (int i = 0; i < t->rows; i++) {
        char **new_row = realloc(t->cells[i], sizeof(char *) * (t->cols + 1));
        if (!new_row) return -1;
        t->cells[i] = new_row;
        if (i == 0)
            t->cells[i][new_col] = strdup(header);
        else
            t->cells[i][new_col] = strdup("");
    }
    t->cols++;
    return 0;
}

// Helper: Print a CSV field with necessary escaping.
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
            while (*p && !(*p == '\"' && (*(p + 1) != '\"'))) {
                if (*p == '\"' && *(p + 1) == '\"') {
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
        fields[num++] = strdup(buffer);
        if (*p == ',') p++;
    }
    *count = num;
    return fields;
}

Table *table_load_csv(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    char line[1024];
    char ***cells = NULL;
    int rows = 0, cols = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
    
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
                cells[rows][j] = strdup("");
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

/*
 * New Functions: Formula Evaluation
 *
 * A cell whose content begins with '=' is treated as a formula.
 * The parser supports numbers, basic arithmetic (+, -, *, /), parentheses,
 * cell references (for example "B2"), and functions SUM() and AVERAGE() with ranges.
 *
 * The Excel–style mapping is as follows:
 *   - Column letters: A corresponds to table column 1, B to 2, etc.
 *   - Row numbers: user-entered row number n refers to table row (n-1) (since row 0 is header).
 *
 * In case of errors (for example, syntax error, division by zero, etc.),
 * the evaluated result is "#ERR".
 */

// Global variable used by the parser to indicate an error.
static int formula_error = 0;

// Helper: Skip whitespace characters.
static void skip_whitespace(const char **s) {
    while (**s == ' ' || **s == '\t')
        (*s)++;
}

// Helper: Parse a cell reference from the current position.
// The cell reference consists of one or more letters (column)
// followed by one or more digits (row). The pointer *s is updated.
// On success, returns 1 and sets *row and *col (Excel style: e.g., A2 => row=2, col=1).
static int parse_cell_reference_pp(const char **s, int *row, int *col) {
    const char *start = *s;
    if (!isalpha(**s))
        return 0;
    int col_value = 0;
    while (isalpha(**s)) {
        col_value = col_value * 26 + (toupper(**s) - 'A' + 1);
        (*s)++;
    }
    if (!isdigit(**s)) {
        *s = start;
        return 0;
    }
    int row_value = 0;
    while (isdigit(**s)) {
        row_value = row_value * 10 + (**s - '0');
        (*s)++;
    }
    *row = row_value;
    *col = col_value;
    return 1;
}

// Forward declarations of recursive parsing functions.
static double parse_expression(const Table *t, const char **s);
static double parse_term(const Table *t, const char **s);
static double parse_factor(const Table *t, const char **s);

// parse_factor handles numbers, parentheses, cell references, and function calls.
static double parse_factor(const Table *t, const char **s) {
    skip_whitespace(s);
    double result = 0;
    if (**s == '(') {
        (*s)++;  // Skip '('
        result = parse_expression(t, s);
        skip_whitespace(s);
        if (**s == ')')
            (*s)++;
        else
            formula_error = 1;
        return result;
    } else if (isalpha(**s)) {
        // Look ahead to distinguish between a function call and a simple cell reference.
        const char *p = *s;
        char ident[32];
        int ident_len = 0;
        while (isalpha(*p) && ident_len < (int)sizeof(ident)-1) {
            ident[ident_len++] = *p;
            p++;
        }
        ident[ident_len] = '\0';
        skip_whitespace(&p);
        if (*p == '(') {
            // Function call: support SUM() and AVERAGE().
            *s = p;  // Advance main pointer to '('
            (*s)++;  // Skip '('
            skip_whitespace(s);
            int start_row, start_col, end_row, end_col;
            if (!parse_cell_reference_pp(s, &start_row, &start_col)) {
                formula_error = 1;
                return 0;
            }
            skip_whitespace(s);
            if (**s == ':') {
                (*s)++; // Skip ':'
                skip_whitespace(s);
                if (!parse_cell_reference_pp(s, &end_row, &end_col)) {
                    formula_error = 1;
                    return 0;
                }
            } else {
                // Single cell; range consists of one cell.
                end_row = start_row;
                end_col = start_col;
            }
            skip_whitespace(s);
            if (**s == ')')
                (*s)++; // Skip ')'
            else {
                formula_error = 1;
                return 0;
            }
            // Map Excel row/col to table indices.
            int tr1 = start_row - 1;  // table row index
            int tc1 = start_col;      // table column index
            int tr2 = end_row - 1;
            int tc2 = end_col;
            if (tr1 > tr2) { int temp = tr1; tr1 = tr2; tr2 = temp; }
            if (tc1 > tc2) { int temp = tc1; tc1 = tc2; tc2 = temp; }
            double sum = 0;
            int count = 0;
            for (int r = tr1; r <= tr2; r++) {
                for (int c = tc1; c <= tc2; c++) {
                    const char *cell_val = table_get_cell(t, r, c);
                    double num_val = 0;
                    if (cell_val && cell_val[0] == '=') {
                        char *eval_str = evaluate_formula(t, cell_val);
                        if (eval_str) {
                            num_val = atof(eval_str);
                            free(eval_str);
                        }
                    } else {
                        num_val = atof(cell_val);
                    }
                    sum += num_val;
                    count++;
                }
            }
            if (strcasecmp(ident, "AVERAGE") == 0) {
                if (count > 0)
                    return sum / count;
                else
                    return 0;
            } else {
                // Default to SUM if function name is not AVERAGE.
                return sum;
            }
        } else {
            // Not a function call: treat as a cell reference.
            int row, col;
            if (!parse_cell_reference_pp(s, &row, &col)) {
                formula_error = 1;
                return 0;
            }
            int table_row = row - 1;
            int table_col = col;
            const char *cell_val = table_get_cell(t, table_row, table_col);
            double cell_num = 0;
            if (cell_val && cell_val[0] == '=') {
                char *eval_str = evaluate_formula(t, cell_val);
                if (eval_str) {
                    cell_num = atof(eval_str);
                    free(eval_str);
                }
            } else {
                cell_num = atof(cell_val);
            }
            return cell_num;
        }
    } else {
        // Expect a number.
        char *endptr;
        result = strtod(*s, &endptr);
        if (endptr == *s) {
            formula_error = 1;
            return 0;
        }
        *s = endptr;
        return result;
    }
}

static double parse_term(const Table *t, const char **s) {
    double result = parse_factor(t, s);
    skip_whitespace(s);
    while (**s == '*' || **s == '/') {
        char op = **s;
        (*s)++;
        double factor = parse_factor(t, s);
        if (op == '*')
            result *= factor;
        else {
            if (factor == 0) {
                formula_error = 1;
                return 0;
            }
            result /= factor;
        }
        skip_whitespace(s);
    }
    return result;
}

static double parse_expression(const Table *t, const char **s) {
    double result = parse_term(t, s);
    skip_whitespace(s);
    while (**s == '+' || **s == '-') {
        char op = **s;
        (*s)++;
        double term = parse_term(t, s);
        if (op == '+')
            result += term;
        else
            result -= term;
        skip_whitespace(s);
    }
    return result;
}

/*
 * Evaluate a formula string.
 * If the input does not begin with '=', the input is simply duplicated.
 * Otherwise, the expression after '=' is parsed and evaluated.
 * Returns a newly allocated string containing the result (or "#ERR" on error).
 */
char *evaluate_formula(const Table *t, const char *formula) {
    formula_error = 0;
    if (!formula || formula[0] != '=') {
        return strdup(formula);
    }
    const char *expr = formula + 1; // skip '='
    double result = parse_expression(t, &expr);
    skip_whitespace(&expr);
    if (*expr != '\0')
        formula_error = 1;
    char *result_str = malloc(64);
    if (formula_error)
        snprintf(result_str, 64, "#ERR");
    else
        snprintf(result_str, 64, "%g", result);
    return result_str;
}

/*
 * New Functions: Delete a column or a row (with protection for the index/header)
 */
int table_delete_column(Table *t, int col) {
    if (!t || col <= 0 || col >= t->cols)
        return -1;
    for (int i = 0; i < t->rows; i++) {
        free(t->cells[i][col]);
        for (int j = col; j < t->cols - 1; j++) {
            t->cells[i][j] = t->cells[i][j + 1];
        }
        t->cells[i] = realloc(t->cells[i], sizeof(char *) * (t->cols - 1));
    }
    t->cols--;
    return 0;
}

int table_delete_row(Table *t, int row) {
    if (!t || row <= 0 || row >= t->rows)
        return -1;
    for (int j = 0; j < t->cols; j++) {
        free(t->cells[row][j]);
    }
    free(t->cells[row]);
    for (int i = row; i < t->rows - 1; i++) {
        t->cells[i] = t->cells[i + 1];
        if (i > 0) {
            char index_str[32];
            sprintf(index_str, "%d", i);
            free(t->cells[i][0]);
            t->cells[i][0] = strdup(index_str);
        }
    }
    t->rows--;
    t->cells = realloc(t->cells, sizeof(char **) * t->rows);
    return 0;
}

/* End of libtable.c */
