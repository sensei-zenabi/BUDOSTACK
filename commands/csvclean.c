#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Trim leading/trailing whitespace in place */
static char *trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* Return 1 if s is a valid floating‑point literal (no leftover chars) */
static int is_numeric(const char *s) {
    char *endptr;
    errno = 0;
    strtod(s, &endptr);
    if (errno || *endptr != '\0') return 0;
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <input.csv> [output.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *inname  = argv[1];
    const char *outname = (argc == 3 ? argv[2] : NULL);

    FILE *fin  = fopen(inname, "r");
    if (!fin) {
        perror("Failed to open input");
        return EXIT_FAILURE;
    }
    FILE *fout = outname ? fopen(outname, "w") : stdout;
    if (outname && !fout) {
        perror("Failed to open output");
        fclose(fin);
        return EXIT_FAILURE;
    }

    char line[16384];
    size_t expected_cols = 0;
    size_t row = 0;

    while (fgets(line, sizeof(line), fin)) {
        row++;
        /* strip CR/LF */
        line[strcspn(line, "\r\n")] = '\0';
        /* skip empty lines */
        if (line[0] == '\0') continue;

        /* count fields in this row */
        size_t cols = 1;
        for (char *p = line; *p; p++)
            if (*p == ',') cols++;

        /* set expected on first non-empty row */
        if (expected_cols == 0) {
            expected_cols = cols;
        }
        if (cols != expected_cols) {
            continue;  /* mismatched column count */
        }

        /* validate every field is numeric */
        /* make a writable copy */
        size_t len = strlen(line) + 1;
        char *copy = malloc(len);
        if (!copy) {
            perror("malloc");
            break;
        }
        strcpy(copy, line);
        int ok = 1;
        char *saveptr = NULL, *tok = strtok_r(copy, ",", &saveptr);
        while (tok) {
            char *f = trim(tok);
            if (!is_numeric(f)) {
                ok = 0;
                break;
            }
            tok = strtok_r(NULL, ",", &saveptr);
        }
        free(copy);
        if (!ok) {
            continue;  /* non-numeric field */
        }

        /* emit cleaned line: each field trimmed */
        char *line2 = malloc(len);
        if (!line2) {
            perror("malloc");
            break;
        }
        strcpy(line2, line);
        saveptr = NULL; tok = strtok_r(line2, ",", &saveptr);
        int first = 1;
        while (tok) {
            char *f = trim(tok);
            if (!first) fputs(",", fout);
            fputs(f, fout);
            first = 0;
            tok = strtok_r(NULL, ",", &saveptr);
        }
        fputc('\n', fout);
        free(line2);
    }

    if (fin)  fclose(fin);
    if (fout && outname) fclose(fout);
    return EXIT_SUCCESS;
}
