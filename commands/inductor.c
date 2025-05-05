/* inductor.c
 * Calculate inductive reactance Xl = 2·π·f·L
 * Usage:
 *   inductor -l inductance(H) -f frequency(Hz)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Define π since M_PI isn’t guaranteed by <math.h> on all platforms */
#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    double L = -1, f = -1;
    int opt;

    /* parse command-line options */
    while ((opt = getopt(argc, argv, "l:f:")) != -1) {
        switch (opt) {
        case 'l': L = atof(optarg); break;
        case 'f': f = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -l inductance -f frequency\n", argv[0]);
            return 1;
        }
    }

    /* validate inputs */
    if (L <= 0 || f <= 0) {
        fprintf(stderr, "Error: inductance and frequency must be positive.\n");
        return 1;
    }

    /* compute and print inductive reactance */
    double Xl = 2.0 * PI * f * L;
    printf("Inductive reactance Xl = 2·π·f·L = %.6g Ω\n", Xl);
    return 0;
}
