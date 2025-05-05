/* dbm.c
 * Convert between watts and dBm:
 *   dBm = 10·log10(P/1mW), P = 1e-3·10^(dBm/10)
 * Usage:
 *   dbm -p P_W | -d dBm -v value
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char *argv[]) {
    int opt; double P = NAN, d = NAN, v = NAN;
    while ((opt = getopt(argc, argv, "p:d:v:")) != -1) {
        switch (opt) {
        case 'p': P =  atof(optarg); break;
        case 'd': d =  atof(optarg); break;
        case 'v': v =  atof(optarg); break;
        default:
            fprintf(stderr,"Usage: %s [-p P_W | -d dBm] -v value\n", argv[0]);
            return 1;
        }
    }
    if (!isnan(P) == !isnan(d) || isnan(v)) {
        fprintf(stderr,"Error: supply exactly one of -p or -d and -v value.\n");
        return 1;
    }
    if (!isnan(P)) {
        /* ignore v */
        double out = 10.0*log10(P/1e-3);
        printf("%.6g dBm\n", out);
    } else {
        /* v is the dBm value */
        double out = 1e-3 * pow(10.0, v/10.0);
        printf("%.6g W\n", out);
    }
    return 0;
}
