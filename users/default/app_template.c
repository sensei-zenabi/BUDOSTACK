/*
 * C Application Template
 *
 * Design Principles:
 * - Modularity: Separate functions for clear structure.
 * - Error Handling: Basic error checks are provided.
 * - Portability: Uses standard C libraries and POSIX-compliant functions.
 * - Simplicity: A minimal starting point for a console application.
 *
 * Compilation:
 * gcc -std=c11 -Wall -Wextra -pedantic main.c -o app
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For POSIX-compliant functions like sleep()

// Function to print usage information.
void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help   Show this help message and exit\n");
}

// Example function performing a simple operation (squaring a number).
int perform_operation(int arg) {
    return arg * arg;
}

int main(int argc, char *argv[]) {
    // Check command-line arguments for help option.
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        // Additional argument parsing can be implemented here.
    }

    // Main application logic.
    printf("Starting C App Template\n");

    // Example usage of a POSIX function: sleep for 1 second.
    sleep(1);

    // Example of calling a custom function.
    int input = 5;
    int result = perform_operation(input);
    printf("Operation result for %d is %d\n", input, result);

    // Application exit.
    return EXIT_SUCCESS;
}
