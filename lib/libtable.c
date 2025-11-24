#define _POSIX_C_SOURCE 200809L
/*
 * libtable.c
 *
 * Implements a dynamic table for a terminal‐based spreadsheet program.
 *
 * Design principles:
 *  - The Table structure holds a dynamic 2D array of strings.
 *  - The first row is reserved for headers; the first column is always an "Index" column.
 *  - Provides functions to create/free the table, add rows/columns, edit cells,
 *    load/save CSV files, and now supports Excel–like formulas.
 *
 * Enhancements:
 *  - If a cell’s value begins with '=', it is interpreted as a formula.
 *  - The built-in parser supports numeric constants, arithmetic operators (+, -, *, /),
 *    parentheses, cell references (e.g., B2 or $A$1), and the functions SUM() and AVERAGE() over ranges.
 *  - A new printing function (table_print_highlight_ex) now displays Excel–like row numbers and
 *    column letters outside the table grid.
 *  - A new function adjust_cell_references() adjusts cell references in a formula when cells are copy-pasted.
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

/*
 * Helper: Convert a table column index to an Excel–like letter.
 * Note: table columns are 1–indexed for formulas (column 1 = A).
 */
static void get_column_label(int col, char *buf, size_t buf_size) {
    int n = col;
    char temp[16];
    int i = 0;
    while (n > 0 && i < (int)sizeof(temp) - 1) {
        int rem = (n - 1) % 26;
        temp[i++] = 'A' + rem;
        n = (n - 1) / 26;
    }
    temp[i] = '\0';
    // Reverse the string.
    int len = i;
    for (int j = 0; j < len; j++) {
        if (j < (int)buf_size - 1)
            buf[j] = temp[len - 1 - j];
    }
    buf[((size_t)len < buf_size ? (size_t)len : buf_size - 1)] = '\0';
}

/*
 * Updated printing function:
 *
 * This function prints the table using absolute cursor positioning so that
 * each cell has a fixed background region of width 15 characters.
 *
 * - The header row and the row labels are printed with fixed width.
 * - For each data cell, before printing its content we move the cursor to its
 *   fixed starting position.
 * - For a non–selected cell, we simply print its full content (which may overflow).
 * - For the selected cell, we print the first 15 characters (i.e. the cell's
 *   allocated region) in inverse video (highlight), and then print any overflow
 *   (if present) normally.
 *
 * This way, even if an earlier cell (alphabetically) has long text that overflows,
 * the highlighting for the selected cell always covers exactly its 15–character region.
 */
void table_print_highlight_ex(const Table *t, int highlight_row, int highlight_col, int show_formulas,
                              int data_row_offset, int data_col_offset, int max_data_rows, int max_data_cols) {
    if (!t) return;

    const int cell_width = 15;
    if (max_data_rows < 1)
        max_data_rows = 1;
    if (max_data_cols < 1)
        max_data_cols = 1;

    if (data_row_offset < 0)
        data_row_offset = 0;
    if (data_col_offset < 0)
        data_col_offset = 0;

    int total_data_rows = t->rows - 1;
    if (total_data_rows < 0)
        total_data_rows = 0;
    int total_data_cols = t->cols - 1;
    if (total_data_cols < 0)
        total_data_cols = 0;

    if (data_row_offset > total_data_rows)
        data_row_offset = total_data_rows;
    if (data_col_offset > total_data_cols)
        data_col_offset = total_data_cols;

    int start_col = data_col_offset + 1;
    int end_col = start_col + max_data_cols - 1;
    if (end_col > t->cols - 1)
        end_col = t->cols - 1;

    int start_row = data_row_offset + 1; // data rows start at 1
    int end_row = start_row + max_data_rows - 1;
    if (end_row > t->rows - 1)
        end_row = t->rows - 1;

    // Clear the screen and move cursor to top-left.
    printf("\033[2J");    // clear screen
    printf("\033[H");     // home

    // Print header row in fixed positions.
    // The top-left corner is left blank.
    printf("%-15s", "");
    for (int j = start_col; j <= end_col; j++) {
        char col_label[16];
        get_column_label(j, col_label, sizeof(col_label));
        if (highlight_row == 0 && highlight_col == j) {
            printf("\033[7m%-15.15s\033[0m", col_label);
        } else {
            printf("%-15.15s", col_label);
        }
    }
    printf("\n");

    // Now print each row with absolute positioning.
    // Assume header is line 1; data rows start at line 2.
    for (int i = start_row; i <= end_row; i++) {
        // Terminal row for this table row.
        int term_row = (i - start_row) + 2;

        // Print row label (fixed 15 columns) at column 1.
        printf("\033[%d;1H", term_row);
        char row_label[16];
        snprintf(row_label, sizeof(row_label), "%d", i);
        if (highlight_col == 0 && highlight_row == i) {
            printf("\033[7m%-15s\033[0m", row_label);
        } else {
            printf("%-15s", row_label);
        }

        // For each cell in the row (from column 1 onward).
        for (int j = start_col; j <= end_col; j++) {
            // Compute the fixed starting column for cell j.
            // Row label occupies 15 characters. Each cell has a fixed slot of 15.
            int term_col = 15 + (j - start_col) * cell_width + 1;
            // Move the cursor to the cell's fixed starting position.
            printf("\033[%d;%dH", term_row, term_col);

            // Prepare the cell content.
            char buffer[1024];
            const char *cell_content = t->cells[i][j] ? t->cells[i][j] : "";
            if (!show_formulas && cell_content[0] == '=') {
                char *eval_result = evaluate_formula(t, cell_content);
                snprintf(buffer, sizeof(buffer), "%s", eval_result);
                free(eval_result);
            } else {
                snprintf(buffer, sizeof(buffer), "%s", cell_content);
            }

            if (i == highlight_row && j == highlight_col) {
                // For the selected cell, print its allocated cell region (15 characters)
                // using inverse video (highlight).
                char cell_fixed[16];
                strncpy(cell_fixed, buffer, 15);
                cell_fixed[15] = '\0';
                printf("\033[7m%-15s\033[0m", cell_fixed);
                // Then, if the content is longer than 15 characters,
                // print the overflow normally (it will visually extend beyond the cell region).
                if (strlen(buffer) > 15) {
                    printf("%s", buffer + 15);
                }
            } else {
                // For non–selected cells, print the full content.
                printf("%s", buffer);
            }
        }
        // End of row.
        printf("\n");
    }
}

// The previous highlight-print simply calls the extended version.
void table_print_highlight(const Table *t, int highlight_row, int highlight_col) {
    table_print_highlight_ex(t, highlight_row, highlight_col, 0, 0, 0, t ? t->rows : 0, t ? t->cols : 0);
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

/*
 * Updated: Split a CSV line into fields.
 * This version handles trailing commas by adding an empty field.
 */
static char **split_csv_line(const char *line, int *count) {
    char **fields = NULL;
    int capacity = 0, num = 0;
    const char *p = line;
    while (1) {
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
        if (*p == ',') {
            p++;  // Skip the comma.
            // If the comma is the last character, add an empty field.
            if (*p == '\0') {
                if (num >= capacity) {
                    capacity = capacity ? capacity * 2 : 4;
                    char **temp = realloc(fields, capacity * sizeof(char *));
                    if (!temp) break;
                    fields = temp;
                }
                fields[num++] = strdup("");
            }
        } else {
            break;
        }
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
        else if (field_count > cols)
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
 * cell references (for example "B2" or "$A$1"), and functions SUM() and AVERAGE() with ranges.
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

// Modified: Parse a cell reference from the current position.
// Now skips any leading '$' characters so that "$A$1" is parsed as A1.
// FIX: After reading the column letters, also skip an optional '$' before row digits.
static int parse_cell_reference_pp(const char **s, int *row, int *col) {
    const char *start = *s;
    // Skip any '$' for column.
    while (**s == '$')
         (*s)++;
    if (!isalpha(**s))
        return 0;
    int col_value = 0;
    while (isalpha(**s)) {
        col_value = col_value * 26 + (toupper(**s) - 'A' + 1);
        (*s)++;
    }
    // Skip any '$' before row number.
    if (**s == '$')
         (*s)++;
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
        (*s)++;  // Skip '('.
        result = parse_expression(t, s);
        skip_whitespace(s);
        if (**s == ')')
            (*s)++;
        else
            formula_error = 1;
        return result;
    } else if (isalpha(**s) || **s == '$') {
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
            *s = p;  // Advance main pointer to '('.
            (*s)++;  // Skip '('.
            skip_whitespace(s);
            int start_row, start_col, end_row, end_col;
            if (!parse_cell_reference_pp(s, &start_row, &start_col)) {
                formula_error = 1;
                return 0;
            }
            skip_whitespace(s);
            if (**s == ':') {
                (*s)++; // Skip ':'.
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
                (*s)++; // Skip ')'.
            else {
                formula_error = 1;
                return 0;
            }
            // Map Excel row/col to table indices.
            int tr1 = start_row - 1;  // table row index.
            int tc1 = start_col;      // table column index.
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
    const char *expr = formula + 1; // skip '='.
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
 * New Function: Adjust cell references in a formula or cell value.
 * Scans through the input string 'src' and for every cell reference token
 * (possibly with optional '$') adjusts its row and/or column if they are not absolute.
 * delta_row and delta_col are added to the parsed row and column numbers for each
 * reference that is not marked as absolute.
 *
 * Returns a newly allocated string with the adjusted contents.
 */
char *adjust_cell_references(const char *src, int delta_row, int delta_col) {
    size_t len = strlen(src);
    size_t bufsize = len + 1;
    char *result = malloc(bufsize);
    if (!result) return NULL;
    size_t rpos = 0;
    const char *p = src;
    while (*p) {
        // Look for a token that may be a cell reference.
        if ((*p == '$') || isalpha(*p)) {
            const char *start = p;
            int col_abs = 0, row_abs = 0;
            // Check for optional '$' before column letters.
            if (*p == '$') {
                col_abs = 1;
                p++;
            }
            if (!isalpha(*p)) { // not a valid ref; back up.
                result[rpos++] = *start;
                p = start + 1;
                continue;
            }
            // Parse column letters.
            int col = 0;
            while (isalpha(*p)) {
                col = col * 26 + (toupper(*p) - 'A' + 1);
                p++;
            }
            // Check for optional '$' before row digits.
            if (*p == '$') {
                row_abs = 1;
                p++;
            }
            if (!isdigit(*p)) {
                // Not a valid cell reference; copy the characters and continue.
                p = start;
                result[rpos++] = *p++;
                continue;
            }
            int row = 0;
            while (isdigit(*p)) {
                row = row * 10 + (*p - '0');
                p++;
            }
            // Compute new column and row.
            int new_col = col;
            int new_row = row;
            if (!col_abs)
                new_col += delta_col;
            if (!row_abs)
                new_row += delta_row;
            if (new_col < 1) new_col = 1;
            if (new_row < 1) new_row = 1;
            // Convert new_col to letters.
            char col_buf[16];
            int temp = new_col;
            char temp_buf[16];
            int temp_index = 0;
            while (temp > 0) {
                int rem = (temp - 1) % 26;
                temp_buf[temp_index++] = 'A' + rem;
                temp = (temp - 1) / 26;
            }
            for (int i = 0; i < temp_index; i++) {
                col_buf[i] = temp_buf[temp_index - 1 - i];
            }
            col_buf[temp_index] = '\0';
            // Build adjusted token.
            char new_token[64];
            char col_part[32];
            if (col_abs)
                snprintf(col_part, sizeof(col_part), "$%s", col_buf);
            else
                snprintf(col_part, sizeof(col_part), "%s", col_buf);
            char row_part[32];
            if (row_abs)
                snprintf(row_part, sizeof(row_part), "$%d", new_row);
            else
                snprintf(row_part, sizeof(row_part), "%d", new_row);
            snprintf(new_token, sizeof(new_token), "%s%s", col_part, row_part);
            size_t token_len_new = strlen(new_token);
            // Ensure buffer has room.
            while (rpos + token_len_new + 1 > bufsize) {
                bufsize *= 2;
                result = realloc(result, bufsize);
                if (!result) return NULL;
            }
            memcpy(result + rpos, new_token, token_len_new);
            rpos += token_len_new;
        } else {
            result[rpos++] = *p++;
            if (rpos + 1 >= bufsize) {
                bufsize *= 2;
                result = realloc(result, bufsize);
                if (!result) return NULL;
            }
        }
    }
    result[rpos] = '\0';
    return result;
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
