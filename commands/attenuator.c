/* attenuator.c
 * Design T- or π-pad for dB attenuation into Z0.
 * Usage:
 *   attenuator -t [t|p] -d dB -z Z0
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

/* use t = T-pad, p = pi-pad */
int main(int argc, char *argv[]) {
    char type = 0;
    double dB = NAN, Z0 = NAN;
    int opt;
    while ((opt = getopt(argc, argv, "t:d:z:")) != -1) {
        switch (opt) {
        case 't': type = optarg[0]; break;
        case 'd': dB   = atof(optarg); break;
        case 'z': Z0   = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -t [t|p] -d dB -z Z0\n", argv[0]);
            return 1;
        }
    }
    if ((type!='t'&&type!='p') || Z0<=0 || dB<0) {
        fprintf(stderr,"Error: -t t|p, dB≥0, Z0>0 required.\n");
        return 1;
    }
    /* voltage ratio K = 10^(dB/20) */
    double K = pow(10.0, dB/20.0);
    /* for both: Rs = Z0*(K-1)/(K+1), Rp = Z0*(K^2-1)/(2K) */
    double Rs = Z0*(K-1)/(K+1);
    double Rp = Z0*(K*K - 1.0)/(2.0*K);

    if (type == 't') {
        printf("T-pad: series each = %.6g Ω, shunt = %.6g Ω\n", Rs, Rp);
    } else {
        printf("Π-pad: shunt each = %.6g Ω, series = %.6g Ω\n", Rs, Rp);
    }
    return 0;
}
