#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum comparison_operator {
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE
};

static void usage(void) {
    fprintf(stderr,
            "Usage: _CSVFILTER -file <path> -column <n> -op <eq|ne|lt|le|gt|ge> -value <value>\n"
            "        [-numeric] [-skipheader] [-keepheader] [-output <path>]\n"
            "Filter rows in a ';' separated CSV. Column indices are 1-based.\n"
            "When -numeric is set, comparisons treat the column and value as numbers.\n"
            "-skipheader skips the first row during comparisons, while -keepheader\n"
            "prints it before the filtered results.\n");
}

static int parse_index(const char *value, size_t *out_index) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0' || parsed <= 0)
        return -1;
    *out_index = (size_t)(parsed - 1);
    return 0;
}

static bool parse_operator(const char *value, enum comparison_operator *out_op) {
    if (strcmp(value, "eq") == 0) {
        *out_op = OP_EQ;
    } else if (strcmp(value, "ne") == 0) {
        *out_op = OP_NE;
    } else if (strcmp(value, "lt") == 0) {
        *out_op = OP_LT;
    } else if (strcmp(value, "le") == 0) {
        *out_op = OP_LE;
    } else if (strcmp(value, "gt") == 0) {
        *out_op = OP_GT;
    } else if (strcmp(value, "ge") == 0) {
        *out_op = OP_GE;
    } else {
        return false;
    }
    return true;
}

static bool parse_double(const char *value, double *out) {
    char *endptr = NULL;
    errno = 0;
    double parsed = strtod(value, &endptr);
    if (errno != 0 || endptr == value)
        return false;
    while (*endptr != '\0') {
        if (*endptr != ' ' && *endptr != '\t')
            return false;
        endptr++;
    }
    *out = parsed;
    return true;
}

static char *duplicate_column(const char *line, size_t column_index) {
    size_t column = 0;
    const char *segment_start = line;
    for (const char *cursor = line;; ++cursor) {
        if (*cursor == ';' || *cursor == '\0') {
            if (column == column_index) {
                size_t length = (size_t)(cursor - segment_start);
                char *value = malloc(length + 1);
                if (value == NULL)
                    return NULL;
                if (length > 0)
                    memcpy(value, segment_start, length);
                value[length] = '\0';
                return value;
            }
            column++;
            if (*cursor == '\0')
                break;
            segment_start = cursor + 1;
        }
    }
    return NULL;
}

static bool compare_numeric(double lhs, double rhs, enum comparison_operator op) {
    switch (op) {
    case OP_EQ:
        return lhs == rhs;
    case OP_NE:
        return lhs != rhs;
    case OP_LT:
        return lhs < rhs;
    case OP_LE:
        return lhs <= rhs;
    case OP_GT:
        return lhs > rhs;
    case OP_GE:
        return lhs >= rhs;
    }
    return false;
}

static bool compare_string(const char *lhs, const char *rhs, enum comparison_operator op) {
    int cmp = strcmp(lhs, rhs);
    switch (op) {
    case OP_EQ:
        return cmp == 0;
    case OP_NE:
        return cmp != 0;
    case OP_LT:
        return cmp < 0;
    case OP_LE:
        return cmp <= 0;
    case OP_GT:
        return cmp > 0;
    case OP_GE:
        return cmp >= 0;
    }
    return false;
}

int main(int argc, char *argv[]) {
    const char *file_path = NULL;
    const char *output_path = NULL;
    const char *value = NULL;
    size_t column_index = 0;
    bool have_column = false;
    bool have_value = false;
    bool numeric = false;
    bool skip_header = false;
    bool keep_header = false;
    enum comparison_operator op = OP_EQ;
    bool have_operator = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVFILTER: missing value for -file\n");
                return EXIT_FAILURE;
            }
            file_path = argv[i];
        } else if (strcmp(argv[i], "-column") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVFILTER: missing value for -column\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], &column_index) != 0) {
                fprintf(stderr, "_CSVFILTER: invalid column index '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
            have_column = true;
        } else if (strcmp(argv[i], "-op") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVFILTER: missing value for -op\n");
                return EXIT_FAILURE;
            }
            if (!parse_operator(argv[i], &op)) {
                fprintf(stderr, "_CSVFILTER: unknown operator '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
            have_operator = true;
        } else if (strcmp(argv[i], "-value") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVFILTER: missing value for -value\n");
                return EXIT_FAILURE;
            }
            value = argv[i];
            have_value = true;
        } else if (strcmp(argv[i], "-numeric") == 0) {
            numeric = true;
        } else if (strcmp(argv[i], "-skipheader") == 0) {
            skip_header = true;
        } else if (strcmp(argv[i], "-keepheader") == 0) {
            keep_header = true;
        } else if (strcmp(argv[i], "-output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVFILTER: missing value for -output\n");
                return EXIT_FAILURE;
            }
            output_path = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_CSVFILTER: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (file_path == NULL || !have_column || !have_operator || !have_value) {
        usage();
        return EXIT_FAILURE;
    }

    FILE *input = fopen(file_path, "r");
    if (input == NULL) {
        perror("_CSVFILTER: fopen input");
        return EXIT_FAILURE;
    }

    FILE *output = stdout;
    if (output_path != NULL) {
        output = fopen(output_path, "w");
        if (output == NULL) {
            perror("_CSVFILTER: fopen output");
            fclose(input);
            return EXIT_FAILURE;
        }
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    bool header_skipped = false;
    double numeric_value = 0.0;

    if (numeric) {
        if (!parse_double(value, &numeric_value)) {
            fprintf(stderr, "_CSVFILTER: value '%s' is not numeric\n", value);
            free(line);
            if (output != stdout)
                fclose(output);
            fclose(input);
            return EXIT_FAILURE;
        }
    }

    while ((line_len = getline(&line, &line_cap, input)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
            line[--line_len] = '\0';

        if (skip_header && !header_skipped) {
            header_skipped = true;
            if (keep_header && fprintf(output, "%s\n", line) < 0) {
                perror("_CSVFILTER: fprintf header");
                free(line);
                if (output != stdout)
                    fclose(output);
                fclose(input);
                return EXIT_FAILURE;
            }
            if (!keep_header)
                continue;
        }

        char *column_value = duplicate_column(line, column_index);
        if (column_value == NULL) {
            fprintf(stderr, "_CSVFILTER: column %zu not present in '%s'\n", column_index + 1, file_path);
            free(line);
            if (output != stdout)
                fclose(output);
            fclose(input);
            return EXIT_FAILURE;
        }

        bool match = false;
        if (numeric) {
            double parsed;
            if (parse_double(column_value, &parsed))
                match = compare_numeric(parsed, numeric_value, op);
        } else {
            match = compare_string(column_value, value, op);
        }
        free(column_value);

        if (!match)
            continue;

        if (fprintf(output, "%s\n", line) < 0) {
            perror("_CSVFILTER: fprintf");
            free(line);
            if (output != stdout)
                fclose(output);
            fclose(input);
            return EXIT_FAILURE;
        }
    }

    if (ferror(input)) {
        perror("_CSVFILTER: getline");
        free(line);
        if (output != stdout)
            fclose(output);
        fclose(input);
        return EXIT_FAILURE;
    }

    free(line);
    if (output != stdout && fclose(output) != 0) {
        perror("_CSVFILTER: fclose output");
        fclose(input);
        return EXIT_FAILURE;
    }
    if (fclose(input) != 0) {
        perror("_CSVFILTER: fclose input");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
