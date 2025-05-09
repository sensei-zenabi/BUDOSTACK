#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* csvplot.c
 *
 * Usage: csvplot <file.csv> [xcol] [ycol]
 *   <file.csv>  -- path to input CSV (with header)
 *   [xcol]      -- 1-based index of column to use for X (default 1)
 *   [ycol]      -- 1-based index of column to use for Y (default 2)
 *
 * Reads all numeric rows, scales points to the current terminal size,
 * and renders an ASCII plot.
 */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.csv> [xcol] [ycol]\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int xcol = argc >= 3 ? atoi(argv[2]) - 1 : 0;
    int ycol = argc >= 4 ? atoi(argv[3]) - 1 : 1;
    if (xcol < 0 || ycol < 0) {
        fprintf(stderr, "Column indices must be positive integers.\n");
        return 1;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return 1;
    }

    /* Skip header line */
    char *line = NULL;
    size_t len = 0;
    if (getline(&line, &len, f) == -1) {
        fprintf(stderr, "Empty file or read error.\n");
        return 1;
    }

    /* Read data */
    double *xs = NULL, *ys = NULL;
    size_t n = 0, cap = 0;
    while (getline(&line, &len, f) != -1) {
        /* tokenise */
        int col = 0;
        char *tok = strtok(line, ",");
        double xv = 0, yv = 0;
        int have_x = 0, have_y = 0;
        while (tok) {
            if (col == xcol) {
                xv = atof(tok);
                have_x = 1;
            }
            if (col == ycol) {
                yv = atof(tok);
                have_y = 1;
            }
            tok = strtok(NULL, ",");
            col++;
        }
        if (have_x && have_y) {
            if (n == cap) {
                cap = cap ? cap * 2 : 128;
                xs = realloc(xs, cap * sizeof *xs);
                ys = realloc(ys, cap * sizeof *ys);
                if (!xs || !ys) {
                    fprintf(stderr, "Memory allocation failed\n");
                    return 1;
                }
            }
            xs[n] = xv;
            ys[n] = yv;
            n++;
        }
    }
    free(line);
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "No numeric data found in columns %d and %d.\n",
                xcol+1, ycol+1);
        return 1;
    }

    /* Find ranges */
    double xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
    for (size_t i = 1; i < n; i++) {
        if (xs[i] < xmin) xmin = xs[i];
        if (xs[i] > xmax) xmax = xs[i];
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (xmax == xmin) { xmax = xmin + 1; xmin -= 1; }
    if (ymax == ymin) { ymax = ymin + 1; ymin -= 1; }

    /* Get terminal size */
    struct winsize w;
    int term_w = 80, term_h = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        term_w = w.ws_col;
        term_h = w.ws_row;
    }
    /* Reserve 3 rows for labels/axes */
    int plot_h = term_h - 3;
    int plot_w = term_w - 2;  /* reserve 2 cols for Y-axis */

    /* Allocate grid */
    char **grid = malloc(plot_h * sizeof *grid);
    for (int i = 0; i < plot_h; i++) {
        grid[i] = malloc(plot_w);
        memset(grid[i], ' ', plot_w);
    }

    /* Plot points */
    for (size_t i = 0; i < n; i++) {
        int px = (int)((xs[i] - xmin) / (xmax - xmin) * (plot_w - 1));
        int py = (int)((ys[i] - ymin) / (ymax - ymin) * (plot_h - 1));
        /* flip y */
        int row = plot_h - 1 - py;
        int col = px;
        if (row >= 0 && row < plot_h && col >= 0 && col < plot_w)
            grid[row][col] = '*';
    }

    /* Draw Y range */
    printf("Y [%g .. %g]\n", ymin, ymax);

    /* Render grid */
    for (int i = 0; i < plot_h; i++) {
        /* Y-axis marker */
        printf("|");
        fwrite(grid[i], 1, plot_w, stdout);
        printf("\n");
        free(grid[i]);
    }
    free(grid);

    /* X-axis */
    printf("+");
    for (int i = 0; i < plot_w; i++) putchar('-');
    printf("\n");

    /* X labels */
    printf("  %g", xmin);
    /* pad */
    int lblw = snprintf(NULL, 0, "%g", xmin);
    int spaces = plot_w - lblw - (int)snprintf(NULL, 0, "%g", xmax);
    for (int i = 0; i < spaces; i++) putchar(' ');
    printf("%g\n", xmax);

    free(xs);
    free(ys);
    return 0;
}
