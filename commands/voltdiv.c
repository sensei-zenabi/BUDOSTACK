/* voltdiv.c
 * Solve V_out = V_in·R2/(R1+R2). Supply three of Vin, Vout, R1, R2.
 * Usage:
 *   voltdiv -i Vin -o Vout -a R1 -b R2
 * Provide exactly three flags; it computes the fourth.
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>       /* for NAN and isnan() */

int main(int argc, char *argv[]) {
    double Vin  = NAN;
    double Vout = NAN;
    double R1   = NAN;
    double R2   = NAN;
    int opt;

    /* parse command-line options */
    while ((opt = getopt(argc, argv, "i:o:a:b:")) != -1) {
        switch (opt) {
        case 'i': Vin  = atof(optarg); break;
        case 'o': Vout = atof(optarg); break;
        case 'a': R1   = atof(optarg); break;
        case 'b': R2   = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s -i Vin -o Vout -a R1 -b R2\n", argv[0]);
            return 1;
        }
    }

    /* must supply exactly three of the four values */
    int count = (!isnan(Vin)) + (!isnan(Vout))
              + (!isnan(R1))  + (!isnan(R2));
    if (count != 3) {
        fprintf(stderr, "Error: supply exactly three of Vin, Vout, R1, R2.\n");
        return 1;
    }

    /* compute the missing quantity */
    if (isnan(Vout)) {
        /* Vout = Vin * R2 / (R1 + R2) */
        if (R1 + R2 == 0.0) {
            fprintf(stderr, "Error: R1 + R2 must be non-zero.\n");
            return 1;
        }
        Vout = Vin * R2 / (R1 + R2);
        printf("Vout = %.6g V\n", Vout);

    } else if (isnan(R1)) {
        /* R1 = R2 * (Vin/Vout - 1) */
        if (Vout == 0.0) {
            fprintf(stderr, "Error: Vout must be non-zero.\n");
            return 1;
        }
        R1 = R2 * (Vin / Vout - 1.0);
        printf("R1 = %.6g Ω\n", R1);

    } else if (isnan(R2)) {
        /* R2 = R1 * Vout / (Vin - Vout) */
        if (Vin == Vout) {
            fprintf(stderr, "Error: Vin and Vout must differ.\n");
            return 1;
        }
        R2 = R1 * Vout / (Vin - Vout);
        printf("R2 = %.6g Ω\n", R2);

    } else {
        /* Vin = Vout * (R1 + R2) / R2 */
        if (R2 == 0.0) {
            fprintf(stderr, "Error: R2 must be non-zero.\n");
            return 1;
        }
        Vin = Vout * (R1 + R2) / R2;
        printf("Vin = %.6g V\n", Vin);
    }

    return 0;
}
