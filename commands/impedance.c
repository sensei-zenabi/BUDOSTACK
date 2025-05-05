/* impedance.c
 * Compute series R–L–C impedance magnitude & phase:
 * Z = R + j(2πfL – 1/(2πfC))
 * Usage:
 *   impedance -r R(Ω) -l L(H) -c C(F) -f freq(Hz)
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    double R = NAN, L = NAN, C = NAN, f = NAN;
    int opt;
    while ((opt = getopt(argc, argv, "r:l:c:f:")) != -1) {
        switch (opt) {
        case 'r': R = atof(optarg); break;
        case 'l': L = atof(optarg); break;
        case 'c': C = atof(optarg); break;
        case 'f': f = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -r R -l L -c C -f freq\n", argv[0]);
            return 1;
        }
    }
    if (R < 0 || L <= 0 || C <= 0 || f <= 0) {
        fprintf(stderr, "Error: R≥0, L>0, C>0, f>0 required.\n");
        return 1;
    }
    double Xl = 2.0 * PI * f * L;
    double Xc = 1.0 / (2.0 * PI * f * C);
    double imag = Xl - Xc;
    double mag  = sqrt(R*R + imag*imag);
    double phase = atan2(imag, R) * 180.0 / PI;
    printf("Impedance |Z| = %.6g Ω, ∠Z = %.6g°\n", mag, phase);
    return 0;
}
