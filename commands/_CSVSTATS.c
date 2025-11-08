#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr,
            "Usage: _CSVSTATS -file <path> -column <n> [-skipheader]\n"
            "Computes count, sum, mean, min, max, variance, and standard deviation\n"
            "for the given 1-based column index. Values are expected to be numeric and\n"
            "separated by ';'.\n");
}

static int parse_index(const char *value, size_t *out_index) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0')
        return -1;
    if (parsed <= 0)
        return -1;
    *out_index = (size_t)(parsed - 1);
    return 0;
}

static bool parse_double(const char *value, double *out) {
    if (value == NULL || *value == '\0')
        return false;
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

static char *extract_column(char *line, size_t column_index) {
    size_t column = 0;
    char *segment_start = line;
    for (char *cursor = line;; ++cursor) {
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

int main(int argc, char *argv[]) {
    const char *file_path = NULL;
    size_t column_index = 0;
    bool have_column = false;
    bool skip_header = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVSTATS: missing value for -file\n");
                return EXIT_FAILURE;
            }
            file_path = argv[i];
        } else if (strcmp(argv[i], "-column") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_CSVSTATS: missing value for -column\n");
                return EXIT_FAILURE;
            }
            if (parse_index(argv[i], &column_index) != 0) {
                fprintf(stderr, "_CSVSTATS: invalid column index '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
            have_column = true;
        } else if (strcmp(argv[i], "-skipheader") == 0) {
            skip_header = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "_CSVSTATS: unknown argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (file_path == NULL || !have_column) {
        usage();
        return EXIT_FAILURE;
    }

    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        perror("_CSVSTATS: fopen");
        return EXIT_FAILURE;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    bool skipped_header = false;
    size_t count = 0;
    double sum = 0.0;
    double mean = 0.0;
    double m2 = 0.0; /* sum of squares of differences from the current mean */
    double minimum = DBL_MAX;
    double maximum = -DBL_MAX;

    while ((line_len = getline(&line, &line_cap, file)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
            line[--line_len] = '\0';

        if (skip_header && !skipped_header) {
            skipped_header = true;
            continue;
        }

        char *value = extract_column(line, column_index);
        if (value == NULL) {
            fprintf(stderr, "_CSVSTATS: column %zu not present in '%s'\n", column_index + 1, file_path);
            free(line);
            fclose(file);
            return EXIT_FAILURE;
        }

        double number;
        bool ok = parse_double(value, &number);
        free(value);
        if (!ok)
            continue;

        count++;
        sum += number;

        double delta = number - mean;
        mean += delta / (double)count;
        m2 += delta * (number - mean);

        if (number < minimum)
            minimum = number;
        if (number > maximum)
            maximum = number;
    }

    if (ferror(file)) {
        perror("_CSVSTATS: getline");
        free(line);
        fclose(file);
        return EXIT_FAILURE;
    }

    free(line);
    if (fclose(file) != 0) {
        perror("_CSVSTATS: fclose");
        return EXIT_FAILURE;
    }

    if (count == 0) {
        fprintf(stderr, "_CSVSTATS: no numeric values found in column %zu\n", column_index + 1);
        return EXIT_FAILURE;
    }

    double variance = (count > 1) ? m2 / (double)(count - 1) : 0.0;
    double stddev = sqrt(variance);

    printf("count=%zu\n", count);
    printf("sum=%.17g\n", sum);
    printf("mean=%.17g\n", mean);
    printf("min=%.17g\n", minimum);
    printf("max=%.17g\n", maximum);
    printf("variance=%.17g\n", variance);
    printf("stddev=%.17g\n", stddev);

    return EXIT_SUCCESS;
}
