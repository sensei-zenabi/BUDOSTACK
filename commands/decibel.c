/* decibel.c
 * Convert between linear ratio and decibels:
 *   power:   dB = 10·log10(ratio)
 *   voltage: dB = 20·log10(ratio)
 * Usage:
 *   decibel -t [p|v] (-r ratio | -d decibels)
 * Provide one of -r or -d, plus type -t p (power) or v (voltage).
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char *argv[]) {
    char type = 0;
    double ratio = NAN, db = NAN;
    int opt;

    while ((opt = getopt(argc, argv, "t:r:d:")) != -1) {
        switch (opt) {
        case 't': type = optarg[0]; break;
        case 'r': ratio = atof(optarg); break;
        case 'd': db    = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -t [p|v] (-r ratio | -d decibels)\n", argv[0]);
            return 1;
        }
    }

    if ((type!='p' && type!='v') || ((isnan(ratio)) == (isnan(db)))) {
        fprintf(stderr, "Error: specify -t p or v, and exactly one of -r or -d.\n");
        return 1;
    }

    if (!isnan(ratio)) {
        if (ratio <= 0) { fprintf(stderr, "Error: ratio must be positive.\n"); return 1; }
        if (type == 'p')
            db = 10.0 * log10(ratio);
        else
            db = 20.0 * log10(ratio);
        printf("=> %.6g\n", db);
    } else {
        /* converting dB to ratio */
        if (type == 'p')
            ratio = pow(10.0, db / 10.0);
        else
            ratio = pow(10.0, db / 20.0);
        printf("=> %.6g\n", ratio);
    }

    return 0;
}
