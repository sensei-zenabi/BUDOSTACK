/* resonant.c
 * Compute resonant frequency f0 = 1/(2·π·√(L·C))
 * Usage:
 *   resonant -l L(H) -c C(F)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    double L = -1, C = -1;
    int opt;
    while ((opt = getopt(argc, argv, "l:c:")) != -1) {
        switch (opt) {
        case 'l': L = atof(optarg); break;
        case 'c': C = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -l L -c C\n", argv[0]);
            return 1;
        }
    }
    if (L <= 0 || C <= 0) {
        fprintf(stderr, "Error: L and C must be positive.\n");
        return 1;
    }
    double f0 = 1.0 / (2.0 * PI * sqrt(L * C));
    printf("Resonant frequency f0 = %.6g Hz\n", f0);
    return 0;
}
