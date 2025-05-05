/* time_constant_net.c
 * Compute net R or C for sequence of S:…;P:…;S:… from -n string
 * Usage:
 *   tc_net -t [R|C] -n "S:100,200;P:50,50;S:300"
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static double eq_series(const double *a, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}
static double eq_parallel(const double *a, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) {
        if (a[i] == 0) return 0;
        s += 1.0 / a[i];
    }
    return 1.0 / s;
}

int main(int argc, char *argv[]) {
    char *net = NULL, *type = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "t:n:")) != -1) {
        switch (opt) {
        case 't': type = optarg; break;
        case 'n': net  = optarg; break;
        default:
            fprintf(stderr,"Usage: %s -t R|C -n \"S:…;P:…;…\"\n", argv[0]);
            return 1;
        }
    }
    if (!net || !type) {
        fprintf(stderr,"Error: -t R|C and -n network required.\n");
        return 1;
    }
    double acc = 0;
    char *tok, *saveptr = NULL;
    tok = strtok_r(net, ";", &saveptr);
    while (tok) {
        char mode = tok[0];
        if (mode!='S'&&mode!='P') {
            fprintf(stderr,"Error: each block must start S: or P:.\n");
            return 1;
        }
        char *nums = tok+2;
        int cnt = 1;
        for (char *p = nums; *p; p++) if (*p==',') cnt++;
        double *vals = malloc(cnt * sizeof(double));
        int i = 0;
        char *nsp, *nxt = strtok_r(nums, ",", &nsp);
        while (nxt) {
            vals[i++] = atof(nxt);
            nxt = strtok_r(NULL, ",", &nsp);
        }
        double block = (mode=='S' ? eq_series(vals,i) : eq_parallel(vals,i));
        free(vals);
        if (acc==0 && tok == strtok_r(NULL, ";", &saveptr)) {
            /* first block */
            acc = block;
        } else {
            /* combine with accumulated */
            double arr[2] = { acc, block };
            acc = (mode=='S' ? eq_series(arr,2) : eq_parallel(arr,2));
        }
        tok = strtok_r(NULL, ";", &saveptr);
    }
    printf("Equivalent %s = %.6g\n", type, acc);
    return 0;
}
