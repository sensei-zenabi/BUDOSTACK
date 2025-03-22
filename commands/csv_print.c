/*
Design principles:
- Single main file that handles argument parsing and calls the CSV visualization function.
- Uses only plain C (C11, standard libraries only).
- Externally defined functions are declared and then linked with the library source.
*/

#include <stdio.h>
#include <stdlib.h>

/* Declare external function from libcsvtrend.c */
extern void visualize_csv(const char *filename);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <csv_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    visualize_csv(argv[1]);
    return EXIT_SUCCESS;
}
