/* qfactor.c
 * Compute quality factor Q = (1/R)·√(L/C)
 * Usage:
 *   qfactor -r R(Ω) -l L(H) -c C(F)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char *argv[]) {
    double R = -1, L = -1, C = -1;
    int opt;
    while ((opt = getopt(argc, argv, "r:l:c:")) != -1) {
        switch (opt) {
        case 'r': R = atof(optarg); break;
        case 'l': L = atof(optarg); break;
        case 'c': C = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -r R -l L -c C\n", argv[0]);
            return 1;
        }
    }
    if (R <= 0 || L <= 0 || C <= 0) {
        fprintf(stderr, "Error: R, L, and C must be positive.\n");
        return 1;
    }
    double Q = sqrt(L / C) / R;
    printf("Quality factor Q = %.6g\n", Q);
    return 0;
}
