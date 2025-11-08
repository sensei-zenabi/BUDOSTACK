#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_index(const char *value, const char *name, size_t *out_index) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_FROMCSV: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }
    if (parsed <= 0) {
        fprintf(stderr, "_FROMCSV: %s must be greater than 0\n", name);
        return -1;
    }

    *out_index = (size_t)(parsed - 1);
    return 0;
}

static int read_cell(const char *path, size_t target_row, size_t target_column, char **out_value) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        perror("_FROMCSV: fopen");
        return -1;
    }

    if (out_value != NULL)
        *out_value = NULL;

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    size_t current_row = 0;
    int result = -1;

    while ((line_len = getline(&line, &line_cap, file)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
            line[--line_len] = '\0';

        if (current_row == target_row) {
            const char *segment_start = line;
            size_t column = 0;

            for (ssize_t i = 0; ; ++i) {
                if (i == line_len || line[i] == ';') {
                    size_t length = (size_t)(line + i - segment_start);
                    if (column == target_column) {
                        char *value = malloc(length + 1);
                        if (value == NULL) {
                            fprintf(stderr, "_FROMCSV: memory allocation failed while reading '%s'\n", path);
                        } else {
                            if (length > 0)
                                memcpy(value, segment_start, length);
                            value[length] = '\0';
                            *out_value = value;
                            result = 0;
                        }
                        goto cleanup;
                    }
                    column++;
                    if (i == line_len)
                        break;
                    segment_start = line + i + 1;
                }
            }

            fprintf(stderr, "_FROMCSV: column %zu not found in '%s'\n", target_column + 1, path);
            goto cleanup;
        }

        current_row++;
    }

    if (ferror(file))
        perror("_FROMCSV: getline");
    else
        fprintf(stderr, "_FROMCSV: row %zu not found in '%s'\n", target_row + 1, path);

cleanup:
    free(line);
    if (fclose(file) != 0) {
        perror("_FROMCSV: fclose");
        result = -1;
    }

    return result;
}

int main(int argc, char *argv[]) {
    const char *file_path = NULL;
    size_t row_index = 0;
    size_t column_index = 0;
    int have_row = 0;
    int have_column = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_FROMCSV: missing value for -file\n");
                return EXIT_FAILURE;
            }
            file_path = argv[i];
        } else if (strcmp(argv[i], "-row") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_FROMCSV: missing value for -row\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], "-row", &row_index) != 0)
                return EXIT_FAILURE;
            have_row = 1;
        } else if (strcmp(argv[i], "-column") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_FROMCSV: missing value for -column\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], "-column", &column_index) != 0)
                return EXIT_FAILURE;
            have_column = 1;
        } else {
            fprintf(stderr, "_FROMCSV: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (file_path == NULL || !have_row || !have_column) {
        fprintf(stderr, "_FROMCSV: usage: _FROMCSV -file <path> -column <n> -row <n>\n");
        return EXIT_FAILURE;
    }

    char *value = NULL;
    if (read_cell(file_path, row_index, column_index, &value) != 0) {
        free(value);
        return EXIT_FAILURE;
    }

    if (value == NULL) {
        fprintf(stderr, "_FROMCSV: failed to retrieve value\n");
        return EXIT_FAILURE;
    }

    if (printf("%s\n", value) < 0) {
        perror("_FROMCSV: printf");
        free(value);
        return EXIT_FAILURE;
    }

    free(value);
    return EXIT_SUCCESS;
}
