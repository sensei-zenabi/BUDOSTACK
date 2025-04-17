#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

/* compare two doubles for qsort */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* print statistics for a single column */
static void print_stats(double *data, size_t n, size_t col_number) {
    if (n == 0) {
        printf("Column %zu: no data\n\n", col_number);
        return;
    }
    /* mean */
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += data[i];
    double mean = sum / n;
    /* sample variance & std‑dev */
    double ssq = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = data[i] - mean;
        ssq += d*d;
    }
    double variance = (n > 1 ? ssq/(n - 1) : 0.0);
    double stddev   = sqrt(variance);
    /* median */
    double *copy = malloc(n * sizeof(double));
    if (!copy) {
        fprintf(stderr, "Memory error\n");
        return;
    }
    memcpy(copy, data, n * sizeof(double));
    qsort(copy, n, sizeof(double), cmp_double);
    double median;
    if (n % 2 == 1) {
        median = copy[n/2];
    } else {
        median = (copy[n/2 - 1] + copy[n/2]) / 2.0;
    }
    free(copy);
    /* output */
    printf("Column %zu:\n", col_number);
    printf("  Count               : %zu\n", n);
    printf("  Mean                : %.6f\n", mean);
    printf("  Median              : %.6f\n", median);
    printf("  Sample Variance     : %.6f\n", variance);
    printf("  Sample Std Deviation: %.6f\n\n", stddev);
}

/* parse one CSV line (buf) into numeric data;
   if target_col>0, only that 1-based column is stored in data[0],
   otherwise all columns are stored in data[0..num_cols-1].
   Returns -1 on parse error. */
static int parse_line(char *buf, size_t row, size_t num_cols, int target_col,
                      double **data, size_t *sizes, size_t *caps) {
    char *p = buf;
    for (size_t col = 1; col <= num_cols; col++) {
        /* isolate field */
        char *start = p;
        char *end   = start + strcspn(start, ",");
        char saved  = *end;
        *end = '\0';
        /* parse/store if requested */
        if (target_col <= 0 || (int)col == target_col) {
            char *endptr = NULL;
            errno = 0;
            double val = strtod(start, &endptr);
            if (errno || *endptr != '\0') {
                fprintf(stderr,
                        "Invalid number '%s' in row %zu, column %zu\n",
                        start, row, col);
                return -1;
            }
            /* choose storage index */
            size_t idx = (target_col > 0 ? 0 : col - 1);
            if (sizes[idx] >= caps[idx]) {
                caps[idx] *= 2;
                data[idx] = realloc(data[idx], caps[idx] * sizeof(double));
                if (!data[idx]) {
                    perror("realloc");
                    return -1;
                }
            }
            data[idx][sizes[idx]++] = val;
        }
        if (saved == '\0') break;
        p = end + 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr,
                "Usage: %s <csv_file> [column_number]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *fn = argv[1];
    int target_col = -1;
    if (argc == 3) {
        target_col = atoi(argv[2]);
        if (target_col < 1) {
            fprintf(stderr, "Invalid column number: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }
    FILE *fp = fopen(fn, "r");
    if (!fp) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    char line[8192];
    /* read first row */
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Empty file or read error\n");
        fclose(fp);
        return EXIT_FAILURE;
    }
    /* strip CR/LF */
    line[strcspn(line, "\r\n")] = '\0';

    /* count columns */
    size_t len = strlen(line), num_cols = 1;
    for (size_t i = 0; i < len; i++)
        if (line[i] == ',') num_cols++;

    if (target_col > 0 && (size_t)target_col > num_cols) {
        fprintf(stderr,
                "Column number %d out of range (1..%zu)\n",
                target_col, num_cols);
        fclose(fp);
        return EXIT_FAILURE;
    }

    /* how many series we store */
    size_t store_cols = (target_col > 0 ? 1 : num_cols);

    /* allocate storage */
    double **data = malloc(store_cols * sizeof(double*));
    size_t *sizes = malloc(store_cols * sizeof(size_t));
    size_t *caps  = malloc(store_cols * sizeof(size_t));
    if (!data || !sizes || !caps) {
        perror("malloc");
        fclose(fp);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < store_cols; i++) {
        sizes[i] = 0;
        caps[i]   = 128;
        data[i]   = malloc(caps[i] * sizeof(double));
        if (!data[i]) {
            perror("malloc");
            fclose(fp);
            return EXIT_FAILURE;
        }
    }

    /* parse row #1 */
    if (parse_line(line, 1, num_cols, target_col,
                   data, sizes, caps) != 0) {
        fclose(fp);
        return EXIT_FAILURE;
    }

    /* parse remaining rows */
    size_t row = 2;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (parse_line(line, row, num_cols, target_col,
                       data, sizes, caps) != 0) {
            fclose(fp);
            return EXIT_FAILURE;
        }
        row++;
    }
    fclose(fp);

    /* output results */
    if (target_col > 0) {
        print_stats(data[0], sizes[0], (size_t)target_col);
    } else {
        for (size_t i = 0; i < store_cols; i++)
            print_stats(data[i], sizes[i], i + 1);
    }

    /* clean up */
    for (size_t i = 0; i < store_cols; i++)
        free(data[i]);
    free(data);
    free(sizes);
    free(caps);
    return EXIT_SUCCESS;
}
