/* delay.c
 * Propagation delay tpd = length / (c · VF)
 * Usage:
 *   delay -l length_m -v VF(0 < VF ≤ 1)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>        /* for NAN */

#define C0 299792458.0   /* speed of light in m/s */

int main(int argc, char *argv[]) {
    double length = NAN, VF = NAN;
    int opt;

    /* parse command-line options */
    while ((opt = getopt(argc, argv, "l:v:")) != -1) {
        switch (opt) {
        case 'l': length = atof(optarg); break;
        case 'v': VF     = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -l length_m -v VF\n", argv[0]);
            return 1;
        }
    }

    /* validate inputs */
    if (length < 0 || VF <= 0 || VF > 1) {
        fprintf(stderr, "Error: length must be ≥0 and 0 < VF ≤ 1.\n");
        return 1;
    }

    /* compute and print propagation delay */
    double tpd = length / (C0 * VF);
    printf("Delay = %.6g s (%.6g ns)\n", tpd, tpd * 1e9);
    return 0;
}
