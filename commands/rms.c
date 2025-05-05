/* rms.c
 * Convert sinusoid peak, peak-to-peak, and RMS:
 * Vrms = Vpeak/√2, Vpp = 2·Vpeak
 * Usage:
 *   rms -t [p|P|r] -v value
 *   -t p = Vpeak in, -t P = Vpp in, -t r = Vrms in
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

/* define √2 since M_SQRT2 isn’t standard */
#define SQRT2 1.41421356237309504880

int main(int argc, char *argv[]) {
    char type = 0;
    double v = NAN;
    int opt;

    /* parse options */
    while ((opt = getopt(argc, argv, "t:v:")) != -1) {
        switch (opt) {
        case 't': type = optarg[0]; break;
        case 'v': v    = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -t [p|P|r] -v value\n", argv[0]);
            return 1;
        }
    }

    /* validate */
    if ((type!='p' && type!='P' && type!='r') || v < 0) {
        fprintf(stderr, "Error: specify -t p|P|r and v≥0.\n");
        return 1;
    }

    double vp, vpp, vr;

    /* compute all three */
    if (type == 'p') {
        vp  = v;
        vr  = vp / SQRT2;
        vpp = 2.0 * vp;
    } else if (type == 'P') {
        vpp = v;
        vp  = vpp / 2.0;
        vr  = vp / SQRT2;
    } else {  /* type == 'r' */
        vr  = v;
        vp  = vr * SQRT2;
        vpp = 2.0 * vp;
    }

    /* output */
    printf("Vpeak = %.6g, Vpp = %.6g, Vrms = %.6g\n", vp, vpp, vr);
    return 0;
}
