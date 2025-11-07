#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **cells;
    size_t cell_count;
} CsvRow;

typedef struct {
    CsvRow *rows;
    size_t row_count;
} CsvDocument;

static void free_row(CsvRow *row) {
    if (row == NULL)
        return;

    for (size_t i = 0; i < row->cell_count; ++i)
        free(row->cells[i]);
    free(row->cells);
    row->cells = NULL;
    row->cell_count = 0;
}

static void free_document(CsvDocument *doc) {
    if (doc == NULL)
        return;

    for (size_t i = 0; i < doc->row_count; ++i)
        free_row(&doc->rows[i]);
    free(doc->rows);
    doc->rows = NULL;
    doc->row_count = 0;
}

static int append_cell(CsvRow *row, const char *value, size_t len) {
    char *cell = malloc(len + 1);
    if (cell == NULL)
        return -1;

    if (len > 0)
        memcpy(cell, value, len);
    cell[len] = '\0';

    char **new_cells = realloc(row->cells, (row->cell_count + 1) * sizeof(*new_cells));
    if (new_cells == NULL) {
        free(cell);
        return -1;
    }

    row->cells = new_cells;
    row->cells[row->cell_count] = cell;
    row->cell_count += 1;
    return 0;
}

static int read_csv(const char *path, CsvDocument *doc) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            doc->rows = NULL;
            doc->row_count = 0;
            return 0;
        }
        perror("_TOCSV: fopen");
        return -1;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    size_t capacity = 0;
    size_t count = 0;
    CsvRow *rows = NULL;

    while ((line_len = getline(&line, &line_cap, file)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
            line[--line_len] = '\0';

        CsvRow row = {NULL, 0};
        const char *start = line;
        for (ssize_t i = 0; i <= line_len; ++i) {
            if (i == line_len || line[i] == ';') {
                size_t len = (size_t)(line + i - start);
                if (append_cell(&row, start, len) != 0) {
                    free_row(&row);
                    free(line);
                    for (size_t j = 0; j < count; ++j)
                        free_row(&rows[j]);
                    free(rows);
                    fclose(file);
                    fprintf(stderr, "_TOCSV: memory allocation failed while reading '%s'\n", path);
                    return -1;
                }
                start = line + i + 1;
            }
        }
        if (count == capacity) {
            size_t new_cap = capacity == 0 ? 4 : capacity * 2;
            CsvRow *new_rows = realloc(rows, new_cap * sizeof(*new_rows));
            if (new_rows == NULL) {
                free_row(&row);
                free(line);
                for (size_t j = 0; j < count; ++j)
                    free_row(&rows[j]);
                free(rows);
                fclose(file);
                fprintf(stderr, "_TOCSV: memory allocation failed while reading '%s'\n", path);
                return -1;
            }
            rows = new_rows;
            capacity = new_cap;
        }

        rows[count++] = row;
    }

    free(line);

    if (ferror(file)) {
        perror("_TOCSV: getline");
        for (size_t i = 0; i < count; ++i)
            free_row(&rows[i]);
        free(rows);
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0) {
        perror("_TOCSV: fclose");
        for (size_t i = 0; i < count; ++i)
            free_row(&rows[i]);
        free(rows);
        return -1;
    }

    doc->rows = rows;
    doc->row_count = count;
    return 0;
}

static int ensure_rows(CsvDocument *doc, size_t target) {
    if (doc->row_count >= target)
        return 0;

    CsvRow *new_rows = realloc(doc->rows, target * sizeof(*new_rows));
    if (new_rows == NULL)
        return -1;

    for (size_t i = doc->row_count; i < target; ++i) {
        new_rows[i].cells = NULL;
        new_rows[i].cell_count = 0;
    }

    doc->rows = new_rows;
    doc->row_count = target;
    return 0;
}

static int ensure_columns(CsvRow *row, size_t target) {
    if (row->cell_count >= target)
        return 0;

    char **new_cells = realloc(row->cells, target * sizeof(*new_cells));
    if (new_cells == NULL)
        return -1;

    row->cells = new_cells;

    for (size_t i = row->cell_count; i < target; ++i) {
        row->cells[i] = malloc(1);
        if (row->cells[i] == NULL) {
            for (size_t j = row->cell_count; j < i; ++j) {
                free(row->cells[j]);
                row->cells[j] = NULL;
            }
            return -1;
        }
        row->cells[i][0] = '\0';
    }

    row->cell_count = target;
    return 0;
}

static int set_cell(CsvDocument *doc, size_t row_index, size_t column_index, const char *value) {
    if (ensure_rows(doc, row_index + 1) != 0)
        return -1;

    CsvRow *row = &doc->rows[row_index];
    if (ensure_columns(row, column_index + 1) != 0)
        return -1;

    size_t value_len = strlen(value);
    char *copy = malloc(value_len + 1);
    if (copy == NULL)
        return -1;

    memcpy(copy, value, value_len + 1);
    free(row->cells[column_index]);
    row->cells[column_index] = copy;
    return 0;
}

static int write_csv(const char *path, const CsvDocument *doc) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("_TOCSV: fopen");
        return -1;
    }

    for (size_t i = 0; i < doc->row_count; ++i) {
        const CsvRow *row = &doc->rows[i];
        for (size_t j = 0; j < row->cell_count; ++j) {
            if (j > 0 && fputc(';', file) == EOF) {
                perror("_TOCSV: fputc");
                fclose(file);
                return -1;
            }
            if (fputs(row->cells[j], file) == EOF) {
                perror("_TOCSV: fputs");
                fclose(file);
                return -1;
            }
        }
        if (fputc('\n', file) == EOF) {
            perror("_TOCSV: fputc");
            fclose(file);
            return -1;
        }
    }

    if (fclose(file) != 0) {
        perror("_TOCSV: fclose");
        return -1;
    }

    return 0;
}

static int parse_index(const char *value, const char *name, size_t *out_index) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_TOCSV: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }
    if (parsed <= 0) {
        fprintf(stderr, "_TOCSV: %s must be greater than 0\n", name);
        return -1;
    }

    *out_index = (size_t)(parsed - 1);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *file_path = NULL;
    const char *value = NULL;
    size_t row_index = 0;
    size_t column_index = 0;
    int have_row = 0;
    int have_column = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TOCSV: missing value for -file\n");
                return EXIT_FAILURE;
            }
            file_path = argv[i];
        } else if (strcmp(argv[i], "-row") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TOCSV: missing value for -row\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], "-row", &row_index) != 0)
                return EXIT_FAILURE;
            have_row = 1;
        } else if (strcmp(argv[i], "-column") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TOCSV: missing value for -column\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], "-column", &column_index) != 0)
                return EXIT_FAILURE;
            have_column = 1;
        } else if (strcmp(argv[i], "-value") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_TOCSV: missing value for -value\n");
                return EXIT_FAILURE;
            }
            value = argv[i];
        } else {
            fprintf(stderr, "_TOCSV: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (file_path == NULL || value == NULL || !have_row || !have_column) {
        fprintf(stderr, "_TOCSV: usage: _TOCSV -file <path> -column <n> -row <n> -value <text>\n");
        return EXIT_FAILURE;
    }

    CsvDocument doc = {NULL, 0};
    if (read_csv(file_path, &doc) != 0) {
        free_document(&doc);
        return EXIT_FAILURE;
    }

    if (set_cell(&doc, row_index, column_index, value) != 0) {
        fprintf(stderr, "_TOCSV: failed to update cell\n");
        free_document(&doc);
        return EXIT_FAILURE;
    }

    if (write_csv(file_path, &doc) != 0) {
        free_document(&doc);
        return EXIT_FAILURE;
    }

    free_document(&doc);
    return EXIT_SUCCESS;
}
