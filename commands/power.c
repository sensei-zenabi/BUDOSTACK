/* power.c
 * Calculate one missing quantity in P = V·I
 * Usage:
 *   power [-p power] [-v voltage] [-i current]
 * Provide exactly two; it computes the third.
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    double P = -1, V = -1, I = -1;
    int opt;

    while ((opt = getopt(argc, argv, "p:v:i:")) != -1) {
        switch (opt) {
        case 'p': P = atof(optarg); break;
        case 'v': V = atof(optarg); break;
        case 'i': I = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-p power] [-v voltage] [-i current]\n", argv[0]);
            return 1;
        }
    }

    int count = (P >= 0) + (V >= 0) + (I >= 0);
    if (count != 2) {
        fprintf(stderr, "Error: supply exactly two of P, V, I.\n");
        return 1;
    }

    if (P < 0) {
        P = V * I;
        printf("Power P = V * I = %.6g * %.6g = %.6g W\n", V, I, P);
    } else if (V < 0) {
        if (I == 0) { fprintf(stderr, "Error: current must be non-zero.\n"); return 1; }
        V = P / I;
        printf("Voltage V = P / I = %.6g / %.6g = %.6g V\n", P, I, V);
    } else {
        if (V == 0) { fprintf(stderr, "Error: voltage must be non-zero.\n"); return 1; }
        I = P / V;
        printf("Current I = P / V = %.6g / %.6g = %.6g A\n", P, V, I);
    }
    return 0;
}
