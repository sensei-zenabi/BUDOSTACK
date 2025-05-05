/* tau.c
 * Calculate one missing quantity in the RC time constant: τ = R · C
 * Usage:
 *   tau [-t tau] [-r R] [-c C]
 * Provide exactly two of τ (seconds), R (ohms), or C (farads).
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    double t = -1, R = -1, C = -1;
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "t:r:c:")) != -1) {
        switch (opt) {
        case 't': t = atof(optarg); break;
        case 'r': R = atof(optarg); break;
        case 'c': C = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-t tau] [-r R] [-c C]\n", argv[0]);
            return 1;
        }
    }

    /* must supply exactly two */
    int count = (t >= 0) + (R >= 0) + (C >= 0);
    if (count != 2) {
        fprintf(stderr, "Error: supply exactly two of tau, R, C.\n");
        return 1;
    }

    /* compute missing */
    if (t < 0) {
        t = R * C;
        printf("Tau τ = R·C = %.6g·%.6g = %.6g s\n", R, C, t);
    } else if (R < 0) {
        if (C == 0) { fprintf(stderr, "Error: capacitance must be non-zero.\n"); return 1; }
        R = t / C;
        printf("Resistance R = τ/C = %.6g/%.6g = %.6g Ω\n", t, C, R);
    } else {
        if (R == 0) { fprintf(stderr, "Error: resistance must be non-zero.\n"); return 1; }
        C = t / R;
        printf("Capacitance C = τ/R = %.6g/%.6g = %.6g F\n", t, R, C);
    }

    return 0;
}
