/*
 * run.c
 *
 * This program implements a simple command named "run" that accepts a command
 * and its arguments and executes it via the OS shell.
 *
 * Design principles:
 * - Plain C using -std=c11 and only standard libraries.
 * - Simple command-line argument parsing with dynamic command string construction.
 * - Uses the system() function to delegate execution to the OS shell.
 * - Provides error handling for missing arguments and allocation failure.
 *
 * To compile (assuming a Linux environment):
 *     gcc -std=c11 -o run run.c
 *
 * Usage:
 *     ./run <command> [arguments...]
 *
 * Fix:
 *     Since the outer shell strips user-supplied quotes, we reassemble the
 *     command string by quoting any argument that contains whitespace.
 *     This ensures that multiword arguments (like commit messages) are preserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Check if an argument contains any whitespace */
static int has_whitespace(const char *arg) {
    while (*arg) {
        if (isspace((unsigned char)*arg))
            return 1;
        arg++;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Ensure at least one command is provided.
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [arguments...]\n", argv[0]);
        return 1;
    }

    /* First, calculate the total length needed for the new command string.
       For each argument, if it contains whitespace, we add 2 extra characters for the quotes.
       Also, add one extra character per argument for a space between them.
    */
    size_t total_length = 0;
    for (int i = 1; i < argc; i++) {
        total_length += strlen(argv[i]);
        if (has_whitespace(argv[i])) {
            total_length += 2; // for the surrounding quotes
        }
        if (i < argc - 1) {
            total_length++; // for the space
        }
    }

    // Allocate the command string.
    char *command = malloc(total_length + 1);
    if (!command) {
        perror("malloc");
        return 1;
    }
    command[0] = '\0';

    // Reassemble the command string.
    for (int i = 1; i < argc; i++) {
        if (has_whitespace(argv[i])) {
            strcat(command, "\"");
            strcat(command, argv[i]);
            strcat(command, "\"");
        } else {
            strcat(command, argv[i]);
        }
        if (i < argc - 1) {
            strcat(command, " ");
        }
    }

    // For debugging, you can print the constructed command string:
    // printf("Command string: %s\n", command);

    // Execute the command string using system().
    int ret = system(command);
    if (ret == -1) {
        perror("system");
        free(command);
        return 1;
    }

    free(command);
    return ret;
}
