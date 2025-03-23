/*
 * run.c
 *
 * This program implements a simple command named "run" that accepts a filename 
 * (and optionally additional arguments) and attempts to execute it similarly 
 * to how the Linux terminal would.
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
 *     ./run anyfile.anytype [additional arguments...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check if at least one argument (the file/command) is provided
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [arguments...]\n", argv[0]);
        return 1;
    }

    // Calculate the total length needed for the command string.
    // Each argument length plus one space character (or null terminator at the end).
    size_t length = 0;
    for (int i = 1; i < argc; i++) {
        length += strlen(argv[i]) + 1;
    }

    // Allocate the command string buffer dynamically.
    char *command = malloc(length);
    if (!command) {
        perror("malloc");
        return 1;
    }

    // Construct the command string by concatenating the arguments.
    command[0] = '\0';  // initialize as empty string
    for (int i = 1; i < argc; i++) {
        strcat(command, argv[i]);
        if (i < argc - 1) {
            strcat(command, " ");
        }
    }

    // Execute the constructed command string using system().
    int ret = system(command);
    if (ret == -1) {
        perror("system");
        free(command);
        return 1;
    }

    free(command);
    return ret;
}
