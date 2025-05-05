/* capacitor.c
 * Calculate capacitive reactance Xc = 1/(2·π·f·C)
 * Usage:
 *   capacitor -c capacitance(F) -f frequency(Hz)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

/* Define π since M_PI isn’t guaranteed by <math.h> on all platforms */
#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    double C = -1, f = -1;
    int opt;

    /* parse command‐line options */
    while ((opt = getopt(argc, argv, "c:f:")) != -1) {
        switch (opt) {
        case 'c': C = atof(optarg); break;
        case 'f': f = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -c capacitance -f frequency\n", argv[0]);
            return 1;
        }
    }

    /* validate inputs */
    if (C <= 0 || f <= 0) {
        fprintf(stderr, "Error: capacitance and frequency must be positive.\n");
        return 1;
    }

    /* compute and print capacitive reactance */
    double Xc = 1.0 / (2.0 * PI * f * C);
    printf("Capacitive reactance Xc = 1/(2·π·f·C) = %.6g Ω\n", Xc);
    return 0;
}
