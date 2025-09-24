#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure to hold all lines of a file
typedef struct {
    char **lines;
    size_t count;
} file_lines;

// Read a file into memory line by line
static file_lines read_lines(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        exit(EXIT_FAILURE);
    }
    file_lines fl = {NULL, 0};
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, f)) != -1) {
        char *dup = strdup(line);
        if (!dup) {
            perror("strdup");
            free(line);
            fclose(f);
            exit(EXIT_FAILURE);
        }
        char **tmp = realloc(fl.lines, (fl.count + 1) * sizeof(char *));
        if (!tmp) {
            perror("realloc");
            free(dup);
            free(line);
            fclose(f);
            exit(EXIT_FAILURE);
        }
        fl.lines = tmp;
        fl.lines[fl.count++] = dup;
    }
    free(line);
    fclose(f);
    return fl;
}

// Free memory used by file_lines
static void free_lines(file_lines *fl) {
    for (size_t i = 0; i < fl->count; ++i) {
        free(fl->lines[i]);
    }
    free(fl->lines);
    fl->lines = NULL;
    fl->count = 0;
}

// Build an LCS matrix for two arrays of lines
static size_t **build_lcs(const file_lines *a, const file_lines *b) {
    size_t **dp = malloc((a->count + 1) * sizeof(size_t *));
    if (!dp) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i <= a->count; ++i) {
        dp[i] = calloc(b->count + 1, sizeof(size_t));
        if (!dp[i]) {
            perror("calloc");
            exit(EXIT_FAILURE);
        }
    }
    for (size_t i = 1; i <= a->count; ++i) {
        for (size_t j = 1; j <= b->count; ++j) {
            if (strcmp(a->lines[i - 1], b->lines[j - 1]) == 0) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else if (dp[i - 1][j] >= dp[i][j - 1]) {
                dp[i][j] = dp[i - 1][j];
            } else {
                dp[i][j] = dp[i][j - 1];
            }
        }
    }
    return dp;
}

// Recursively print the diff using the LCS matrix
static void print_diff_rec(const file_lines *a, const file_lines *b,
                           size_t **dp, size_t i, size_t j) {
    if (i > 0 && j > 0 && strcmp(a->lines[i - 1], b->lines[j - 1]) == 0) {
        print_diff_rec(a, b, dp, i - 1, j - 1);
        printf("  %s", a->lines[i - 1]);
    } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
        print_diff_rec(a, b, dp, i, j - 1);
        printf("+ %s", b->lines[j - 1]);
    } else if (i > 0 && (j == 0 || dp[i][j - 1] < dp[i - 1][j])) {
        print_diff_rec(a, b, dp, i - 1, j);
        printf("- %s", a->lines[i - 1]);
    }
}

// Free the LCS matrix
static void free_lcs(size_t **dp, size_t rows) {
    for (size_t i = 0; i <= rows; ++i) {
        free(dp[i]);
    }
    free(dp);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: diff <file1> <file2>\n");
        return EXIT_FAILURE;
    }
    file_lines a = read_lines(argv[1]);
    file_lines b = read_lines(argv[2]);

    size_t **dp = build_lcs(&a, &b);

    printf("--- %s\n", argv[1]);
    printf("+++ %s\n", argv[2]);
    print_diff_rec(&a, &b, dp, a.count, b.count);

    free_lcs(dp, a.count);
    free_lines(&a);
    free_lines(&b);
    return EXIT_SUCCESS;
}

