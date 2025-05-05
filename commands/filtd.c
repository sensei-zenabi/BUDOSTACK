/* filtd.c
 * Pick R & C for RC low-pass or RL high-pass at fc from E12/E24.
 * Usage:
 *   filtd -f fc -t [lp|hp] -s [E12|E24]
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>       /* for strcmp() */

#define PI 3.14159265358979323846

/* E12 and E24 mantissas */
static const double E12[] = {1.0,1.2,1.5,1.8,2.2,2.7,3.3,3.9,4.7,5.6,6.8,8.2};
static const double E24[] = {
  1.0,1.1,1.2,1.3,1.5,1.6,1.8,2.0,2.2,2.4,2.7,3.0,
  3.3,3.6,3.9,4.3,4.7,5.1,5.6,6.2,6.8,7.5,8.2,9.1
};

static double nearest(double val, const double *series, int n) {
    int decade = (int)floor(log10(val));
    double best = series[0] * pow(10, decade);
    double mindiff = fabs(best - val);
    for (int d = decade-3; d <= decade+3; d++) {
        double mul = pow(10, d);
        for (int i = 0; i < n; i++) {
            double v = series[i] * mul;
            double diff = fabs(v - val);
            if (diff < mindiff) {
                mindiff = diff;
                best = v;
            }
        }
    }
    return best;
}

int main(int argc, char *argv[]) {
    double fc = NAN;
    char *ser = NULL, *typ = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "f:t:s:")) != -1) {
        switch (opt) {
        case 'f': fc  = atof(optarg); break;
        case 't': typ = optarg;       break;
        case 's': ser = optarg;       break;
        default:
            fprintf(stderr, "Usage: %s -f fc -t lp|hp -s E12|E24\n", argv[0]);
            return 1;
        }
    }

    if (fc <= 0 || !typ || !ser ||
        (strcmp(typ, "lp") != 0 && strcmp(typ, "hp") != 0) ||
        (strcmp(ser, "E12") != 0 && strcmp(ser, "E24") != 0)) {
        fprintf(stderr, "Error: fc>0, -t lp|hp, -s E12|E24 required.\n");
        return 1;
    }

    const double *series = (strcmp(ser, "E24") == 0 ? E24 : E12);
    int n = (strcmp(ser, "E24") == 0 ? 24 : 12);

    if (strcmp(typ, "lp") == 0) {
        /* Low-pass: choose R≈10kΩ, compute C */
        double R_ideal = 10000.0;
        double C_ideal = 1.0 / (2.0 * PI * R_ideal * fc);
        double R = nearest(R_ideal, series, n);
        double C = nearest(C_ideal, series, n);
        printf("LP: R ≈ %.3g Ω, C ≈ %.3g F (fc=1/(2πRC)=%.3g Hz)\n",
               R, C, 1.0 / (2.0 * PI * R * C));
    } else {
        /* High-pass: choose C≈10nF, compute L */
        double C_ideal = 1e-8;
        double L_ideal = 1.0 / (2.0 * PI * fc * C_ideal);
        double C = nearest(C_ideal, series, n);
        double L = nearest(L_ideal, series, n);
        printf("HP: L ≈ %.3g H, C ≈ %.3g F (fc=1/(2π√(LC))=%.3g Hz)\n",
               L, C, 1.0 / (2.0 * PI * sqrt(L * C)));
    }

    return 0;
}
