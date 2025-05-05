/* resistor.c
 * Compute series and parallel equivalent for two resistors.
 * Usage:
 *   resistor -a R1 -b R2
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    double r1 = -1, r2 = -1;
    int opt;

    while ((opt = getopt(argc, argv, "a:b:")) != -1) {
        switch (opt) {
        case 'a': r1 = atof(optarg); break;
        case 'b': r2 = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -a R1 -b R2\n", argv[0]);
            return 1;
        }
    }

    if (r1 < 0 || r2 < 0) {
        fprintf(stderr, "Error: both R1 and R2 must be non-negative.\n");
        return 1;
    }

    double series   = r1 + r2;
    double parallel = (r1 == 0 || r2 == 0) ? 0 : (r1 * r2) / (r1 + r2);

    printf("Series:   R_eq = %.6g + %.6g = %.6g Ω\n", r1, r2, series);
    printf("Parallel: R_eq = (R1·R2)/(R1+R2) = %.6g Ω\n", parallel);
    return 0;
}
