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

/* Print usage summary */
static void print_usage(void) {
    fprintf(stderr, "Usage: csvclean [-h | --help | -help] <input.csv> [output.csv]\n");
}

/* Print detailed help on how cleaning is done */
static void print_help(void) {
    printf(
        "csvclean - clean up a CSV file of numeric data\n\n"
        "Usage:\n"
        "  csvclean [-h | --help | -help] <input.csv> [output.csv]\n\n"
        "Cleaning steps:\n"
        "  1. Strip CR/LF and skip empty lines.\n"
        "  2. On first non-empty row:\n"
        "       • If any field is non-numeric, treat row as header and output it.\n"
        "       • Otherwise treat as data and set expected column count.\n"
        "  3. For subsequent rows:\n"
        "       • Skip rows with mismatched column counts.\n"
        "       • Trim whitespace and verify all fields numeric.\n"
        "       • Output cleaned numeric rows.\n\n"
        "Examples:\n"
        "  csvclean data.csv\n"
        "  csvclean data.csv cleaned.csv\n"
    );
}

int main(int argc, char *argv[]) {
    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 ||
         strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "-help") == 0)) {
        print_help();
        return EXIT_SUCCESS;
    }
    if (argc < 2 || argc > 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *inname  = argv[1];
    const char *outname = (argc == 3 ? argv[2] : NULL);
    FILE *fin = fopen(inname, "r");
    if (!fin) { perror("Failed to open input"); return EXIT_FAILURE; }
    FILE *fout = outname ? fopen(outname, "w") : stdout;
    if (outname && !fout) { perror("Failed to open output"); fclose(fin); return EXIT_FAILURE; }

    char line[16384];
    size_t expected_cols = 0;
    int header_done = 0;

    while (fgets(line, sizeof(line), fin)) {
        /* strip CR/LF and skip empty lines */
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        /* count fields */
        size_t cols = 1;
        for (char *p = line; *p; p++)
            if (*p == ';') cols++;

        /* first data/header row */
        if (!header_done) {
            /* copy and test numeric */
            char *copy = malloc(strlen(line) + 1);
            if (!copy) { perror("malloc"); break; }
            strcpy(copy, line);
            int ok = 1;
            char *saveptr = NULL, *tok = strtok_r(copy, ";", &saveptr);
            while (tok) {
                if (!is_numeric(trim(tok))) { ok = 0; break; }
                tok = strtok_r(NULL, ";", &saveptr);
            }
            free(copy);

            expected_cols = cols;
            header_done = 1;

            if (!ok) {
                /* treat as header: output trimmed */
                char *hdr = malloc(strlen(line) + 1);
                if (!hdr) { perror("malloc"); break; }
                strcpy(hdr, line);
                saveptr = NULL; tok = strtok_r(hdr, ";", &saveptr);
                int first = 1;
                while (tok) {
                    if (!first) fputc(';', fout);
                    fputs(trim(tok), fout);
                    first = 0;
                    tok = strtok_r(NULL, ";", &saveptr);
                }
                fputc('\n', fout);
                free(hdr);
                continue;
            }
            /* if ok, fall through to numeric processing */
        }

        /* skip mismatched columns */
        if (cols != expected_cols) continue;

        /* numeric validation */
        char *copy = malloc(strlen(line) + 1);
        if (!copy) { perror("malloc"); break; }
        strcpy(copy, line);
        int ok = 1;
        char *saveptr = NULL, *tok = strtok_r(copy, ";", &saveptr);
        while (tok) {
            if (!is_numeric(trim(tok))) { ok = 0; break; }
            tok = strtok_r(NULL, ";", &saveptr);
        }
        free(copy);
        if (!ok) continue;

        /* emit cleaned row */
        char *line2 = malloc(strlen(line) + 1);
        if (!line2) { perror("malloc"); break; }
        strcpy(line2, line);
        saveptr = NULL; tok = strtok_r(line2, ";", &saveptr);
        int first = 1;
        while (tok) {
            if (!first) fputc(';', fout);
            fputs(trim(tok), fout);
            first = 0;
            tok = strtok_r(NULL, ";", &saveptr);
        }
        fputc('\n', fout);
        free(line2);
    }

    fclose(fin);
    if (outname) fclose(fout);
    return EXIT_SUCCESS;
}
