/* ohm.c
 * Calculate one missing quantity in Ohm’s law: V = I·R
 * Usage: 
 *   ohm [-v voltage] [-i current] [-r resistance]
 * Provide exactly two of the three; it computes the third.
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    double V = -1, I = -1, R = -1;
    int opt;

    while ((opt = getopt(argc, argv, "v:i:r:")) != -1) {
        switch (opt) {
        case 'v': V = atof(optarg); break;
        case 'i': I = atof(optarg); break;
        case 'r': R = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-v voltage] [-i current] [-r resistance]\n", argv[0]);
            return 1;
        }
    }

    int count = (V >= 0) + (I >= 0) + (R >= 0);
    if (count != 2) {
        fprintf(stderr, "Error: supply exactly two of V, I, R.\n");
        return 1;
    }

    if (V < 0) {
        V = I * R;
        printf("Voltage V = I * R = %.6g * %.6g = %.6g V\n", I, R, V);
    } else if (I < 0) {
        if (R == 0) { fprintf(stderr, "Error: resistance must be non-zero.\n"); return 1; }
        I = V / R;
        printf("Current I = V / R = %.6g / %.6g = %.6g A\n", V, R, I);
    } else {
        if (I == 0) { fprintf(stderr, "Error: current must be non-zero.\n"); return 1; }
        R = V / I;
        printf("Resistance R = V / I = %.6g / %.6g = %.6g Ω\n", V, I, R);
    }
    return 0;
}
