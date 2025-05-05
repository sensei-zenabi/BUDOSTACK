/* bw.c
 * Bandwidth and center freq from f1, f2:
 *   BW = f2 - f1, f0 = √(f1·f2)
 * Usage:
 *   bw -l f1(Hz) -h f2(Hz)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char *argv[]) {
    double f1 = NAN, f2 = NAN;
    int opt;
    while ((opt = getopt(argc, argv, "l:h:")) != -1) {
        switch (opt) {
        case 'l': f1 = atof(optarg); break;
        case 'h': f2 = atof(optarg); break;
        default:
            fprintf(stderr,"Usage: %s -l f1 -h f2\n", argv[0]);
            return 1;
        }
    }
    if (f1 <= 0 || f2 <= f1) {
        fprintf(stderr,"Error: require 0 < f1 < f2.\n");
        return 1;
    }
    double BW = f2 - f1;
    double f0 = sqrt(f1 * f2);
    printf("BW = %.6g Hz, f0 = %.6g Hz\n", BW, f0);
    return 0;
}
