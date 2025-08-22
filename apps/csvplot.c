#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* csvplot.c
 *
 * Usage:
 *   csvplot <file.csv>
 *     -> plots column 1 (x) vs column 2 (y)
 *
 *   csvplot <file.csv> <xcol> <ycol1> [<ycol2> ...]
 *     -> plots each ycol against xcol.
 *        Columns are 0-based indices.
 *
 * Examples:
 *   csvplot data.csv
 *   csvplot data.csv 0 1
 *   csvplot data.csv 0 1 2 4
 *
 * Reads CSV with a header line, extracts the specified columns,
 * scales to terminal size, and draws an ASCII scatter plot.
 */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <file.csv> [xcol ycol1 [ycol2 ...]]\n",
                argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    /* Determine x column and y columns */
    int xcol;
    int ycount;
    int *ycols;

    if (argc == 2) {
        /* defaults: x = 0, single y = 1 */
        xcol  = 0;
        ycount = 1;
        ycols = malloc(sizeof *ycols);
        ycols[0] = 1;
    } else {
        /* at least: argv[2] = xcol, argv[3...] = ycols */
        xcol = atoi(argv[2]);
        ycount = argc - 3;
        if (ycount < 1) {
            fprintf(stderr,
                    "Must specify at least one y column when giving xcol.\n");
            return 1;
        }
        ycols = malloc(ycount * sizeof *ycols);
        for (int j = 0; j < ycount; j++) {
            ycols[j] = atoi(argv[3 + j]);
        }
    }

    /* Open file */
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return 1;
    }

    /* Skip header */
    char *line = NULL;
    size_t len = 0;
    if (getline(&line, &len, f) == -1) {
        fprintf(stderr, "Empty file or read error.\n");
        return 1;
    }

    /* Read and store data */
    double *xs = NULL;
    double **ys = NULL;
    size_t n = 0, cap = 0;

    while (getline(&line, &len, f) != -1) {
        int col = 0;
        char *tok = strtok(line, ",");
        double xv = 0.0;
        int have_x = 0;
        double *yv = malloc(ycount * sizeof *yv);
        int *have_y = malloc(ycount * sizeof *have_y);

        for (int j = 0; j < ycount; j++)
            have_y[j] = 0;

        while (tok) {
            if (col == xcol) {
                xv = atof(tok);
                have_x = 1;
            }
            for (int j = 0; j < ycount; j++) {
                if (col == ycols[j]) {
                    yv[j] = atof(tok);
                    have_y[j] = 1;
                }
            }
            tok = strtok(NULL, ",");
            col++;
        }

        /* only store rows with all requested values */
        int ok = have_x;
        for (int j = 0; j < ycount; j++)
            ok = ok && have_y[j];

        if (ok) {
            if (n == cap) {
                cap = cap ? cap * 2 : 128;
                xs = realloc(xs, cap * sizeof *xs);
                ys = realloc(ys, ycount * sizeof *ys);
                if (!xs || !ys) {
                    fprintf(stderr, "Memory allocation failure\n");
                    return 1;
                }
                for (int j = 0; j < ycount; j++) {
                    ys[j] = realloc(ys[j], cap * sizeof *ys[j]);
                    if (!ys[j]) {
                        fprintf(stderr, "Memory allocation failure\n");
                        return 1;
                    }
                }
            }
            xs[n] = xv;
            for (int j = 0; j < ycount; j++) {
                ys[j][n] = yv[j];
            }
            n++;
        }

        free(yv);
        free(have_y);
    }
    free(line);
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "No complete data rows found for given columns.\n");
        return 1;
    }

    /* Find ranges */
    double xmin = xs[0], xmax = xs[0];
    double ymin = ys[0][0], ymax = ys[0][0];
    for (size_t i = 0; i < n; i++) {
        if (xs[i] < xmin) xmin = xs[i];
        if (xs[i] > xmax) xmax = xs[i];
        for (int j = 0; j < ycount; j++) {
            double v = ys[j][i];
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
    }
    if (xmax == xmin) { xmax = xmin + 1; xmin -= 1; }
    if (ymax == ymin) { ymax = ymin + 1; ymin -= 1; }

    /* Terminal size */
    struct winsize w;
    int term_w = 80, term_h = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        term_w = w.ws_col;
        term_h = w.ws_row;
    }
    int plot_h = term_h - 4;  /* reserve rows for labels/legend */
    int plot_w = term_w - 5;  /* reserve cols for Y-axis and margin */

    /* Allocate plot grid */
    char **grid = malloc(plot_h * sizeof *grid);
    for (int i = 0; i < plot_h; i++) {
        grid[i] = malloc(plot_w);
        memset(grid[i], ' ', plot_w);
    }

    /* Marker characters for multiple series */
    char markers[] = { '*', 'o', '+', 'x', 's', 'd', '#' };
    int nmark = sizeof markers / sizeof *markers;

    /* Plot each point */
    for (int j = 0; j < ycount; j++) {
        for (size_t i = 0; i < n; i++) {
            int px = (int)((xs[i] - xmin) / (xmax - xmin) * (plot_w - 1));
            int py = (int)((ys[j][i] - ymin) / (ymax - ymin) * (plot_h - 1));
            int row = plot_h - 1 - py;
            int col = px;
            if (row >= 0 && row < plot_h && col >= 0 && col < plot_w) {
                grid[row][col] = markers[j % nmark];
            }
        }
    }

    /* Print Y range */
    printf("Y range: [%g .. %g]\n", ymin, ymax);

    /* Render grid */
    for (int i = 0; i < plot_h; i++) {
        printf("| ");               /* Y-axis */
        fwrite(grid[i], 1, plot_w, stdout);
        printf("\n");
        free(grid[i]);
    }
    free(grid);

    /* X-axis */
    printf("+-");
    for (int i = 0; i < plot_w; i++) putchar('-');
    printf("\n");

    /* X labels */
    char buf1[32], buf2[32];
    snprintf(buf1, sizeof buf1, "%g", xmin);
    snprintf(buf2, sizeof buf2, "%g", xmax);
    printf("  %s", buf1);
    int pad = plot_w - (int)strlen(buf1) - (int)strlen(buf2);
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", buf2);

    /* Legend */
    printf("Legend: ");
    for (int j = 0; j < ycount; j++) {
        printf("%c=col%d ", markers[j % nmark], ycols[j]);
    }
    printf("\n");

    /* Clean up */
    free(xs);
    for (int j = 0; j < ycount; j++) {
        free(ys[j]);
    }
    free(ys);
    free(ycols);

    return 0;
}
