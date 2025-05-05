/* cutoff.c
 * Calculate one missing quantity in RC filter cutoff: f_c = 1/(2·π·R·C)
 * Usage:
 *   cutoff [-f fc] [-r R] [-c C]
 * Provide exactly two of f_c (Hz), R (Ω), or C (F).
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Define π */
#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    double fc = -1, R = -1, C = -1;
    int opt;

    while ((opt = getopt(argc, argv, "f:r:c:")) != -1) {
        switch (opt) {
        case 'f': fc = atof(optarg); break;
        case 'r': R  = atof(optarg); break;
        case 'c': C  = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-f fc] [-r R] [-c C]\n", argv[0]);
            return 1;
        }
    }

    int count = (fc >= 0) + (R >= 0) + (C >= 0);
    if (count != 2) {
        fprintf(stderr, "Error: supply exactly two of fc, R, C.\n");
        return 1;
    }

    if (fc < 0) {
        if (R <= 0 || C <= 0) { fprintf(stderr, "Error: R and C must be positive.\n"); return 1; }
        fc = 1.0 / (2.0 * PI * R * C);
        printf("Cutoff f_c = 1/(2πRC) = %.6g Hz\n", fc);
    } else if (R < 0) {
        if (fc <= 0 || C <= 0) { fprintf(stderr, "Error: fc and C must be positive.\n"); return 1; }
        R = 1.0 / (2.0 * PI * fc * C);
        printf("Resistance R = 1/(2π·f_c·C) = %.6g Ω\n", R);
    } else {
        if (fc <= 0 || R <= 0) { fprintf(stderr, "Error: fc and R must be positive.\n"); return 1; }
        C = 1.0 / (2.0 * PI * fc * R);
        printf("Capacitance C = 1/(2π·f_c·R) = %.6g F\n", C);
    }

    return 0;
}
