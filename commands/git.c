/*
 * git_wrapper.c
 *
 * This program acts as a wrapper for Git commands.
 * Depending on the user arguments, it translates:
 *   - "git" to "git status"
 *   - "git <path_to_file>" to "git log --follow -- <path_to_file>"
 *   - "git changes" to "git log --name-only"
 *   - "git -help" to display this help message
 *
 * Design principles used:
 *   - Plain C implementation using C11 and standard POSIX-compliant functions.
 *   - The program uses execvp to replace the process image with the appropriate git command.
 *   - Simple conditional checks determine which git command to execute.
 *   - Error handling is provided to notify the user if the command execution fails.
 *
 * Compile with:
 *   gcc -std=c11 -Wall -Wextra -o git git_wrapper.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_help(const char *prog_name) {
    // Remove any directory path from prog_name
    const char *base_name = strrchr(prog_name, '/');
    if (base_name)
        base_name++; // skip the '/'
    else
        base_name = prog_name;

    printf("Usage:\n");
    printf("  %s                : Equivalent to 'git status'\n", base_name);
    printf("  %s <path_to_file> : Equivalent to 'git log --follow -- <path_to_file>'\n", base_name);
    printf("  %s changes       : Equivalent to 'git log --name-only'\n", base_name);
    printf("  %s -help         : Display this help message\n", base_name);
}

int main(int argc, char *argv[]) {
    // No additional argument: execute "git status"
    if (argc == 1) {
        char *args[] = {"git", "status", NULL};
        execvp("git", args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }
    // Single argument provided
    else if (argc == 2) {
        // If the argument is "-help"
        if (strcmp(argv[1], "-help") == 0) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        // If the argument is "changes"
        else if (strcmp(argv[1], "changes") == 0) {
            char *args[] = {"git", "log", "--stat", "--graph", NULL};
            execvp("git", args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        // If the argument is "commits"
		else if (strcmp(argv[1], "commits") == 0) {
            char *args[] = {"sh", "-c", "echo '\nCOMMITS PER FILE:\n' && git log --pretty=format: --name-only | sort | uniq -c | sort -rn", NULL};
            execvp("sh", args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        // Otherwise treat it as a file path and execute "git log --follow -- <file>"
        else {
            char *args[] = {"git", "log", "--stat", "--graph", "--follow", "--", argv[1], NULL};
            execvp("git", args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
    }
    // If more than one argument is provided, print usage message
    else {
        fprintf(stderr, "Usage: %s [<path_to_file>|changes|-help]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}
