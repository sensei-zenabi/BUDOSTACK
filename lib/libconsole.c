/*
 * libconsole.c
 *
 * This file contains the implementation of the prettyprint function,
 * which prints a message to stdout one character at a time, with a
 * configurable delay (in milliseconds) between each character.
 *
 * Design Principles:
 * - Use only standard, cross-platform C libraries (stdio.h, time.h).
 * - Use -std=c11 and plain C.
 * - The function delays are implemented using clock() for portability.
 *
 * Note: The delay mechanism here uses a busy wait loop with clock()
 *       which may not be very efficient, but it works with standard C.
 */

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

// If PATH_MAX is not defined, define it.
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Declare external global variable 'base_path' defined in main.c.
// This variable should contain the directory where main.c (the executable) resides.
extern char base_path[PATH_MAX];

// prettyprint prints the provided message one character at a time.
// delay_ms specifies the delay between printing each character in milliseconds.
void prettyprint(const char *message, unsigned int delay_ms) {
    // Calculate delay in clock ticks. CLOCKS_PER_SEC gives the number of clock ticks per second.
    clock_t delay_ticks = (delay_ms * CLOCKS_PER_SEC) / 1000;
    
    // Iterate through each character in the message.
    for (const char *p = message; *p != '\0'; p++) {
        // Print the character.
        putchar(*p);
        fflush(stdout);  // Ensure the character is output immediately.

        // Start a busy wait loop to create the delay.
        clock_t start_time = clock();
        while ((clock() - start_time) < delay_ticks)
            ;  // Busy-wait loop.
    }
    // Print a newline after the message.
    putchar('\n');
}

void printlogo() {
    // AALTO LOGO
    printf(" █████   █████  ██      ████████  ██████ \n");
    printf("██   ██ ██   ██ ██         ██    ██    ██\n"); 
    printf("███████ ███████ ██         ██    ██    ██\n"); 
    printf("██   ██ ██   ██ ██         ██    ██    ██\n"); 
    printf("██   ██ ██   ██ ███████    ██     ██████ \n");                          
}

// login prompts the user for a username and password,
// defaults to "default" if no username is entered, and then
// attempts to change the current directory to a path built relative
// to the current working directory: "<current_dir>/users/<username>".
//
// This version does not rely on any external global variables.
void login() {
    char username[100];
    // char password[100];

    // Prompt for username.
    printf("\n\nEnter username: ");
    if (fgets(username, sizeof(username), stdin) != NULL) {
        // Remove the trailing newline, if present.
        size_t i = 0;
        while (username[i] != '\0') {
            if (username[i] == '\n') {
                username[i] = '\0';
                break;
            }
            i++;
        }
    }
    // If no username provided, default to "default".
    if (username[0] == '\0') {
        snprintf(username, sizeof(username), "default");
    }

    /*
    // Prompt for password (mock-up; input is not hidden).
    printf("Enter password: ");
    if (fgets(password, sizeof(password), stdin) != NULL) {
        size_t i = 0;
        while (password[i] != '\0') {
            if (password[i] == '\n') {
                password[i] = '\0';
                break;
            }
            i++;
        }
    }
    */ 
    
    // For demonstration, assume password verification is successful.
    printf("Login successful. Welcome, %s!\n", username);

    // Get the current working directory.
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return;
    }

    // Build the target directory relative to the current directory.
    // This path will be: "<current_dir>/users/<username>"
    char new_path[PATH_MAX];
    snprintf(new_path, sizeof(new_path), "%s/users/%s", cwd, username);

    // Attempt to change the current working directory.
    if (chdir(new_path) != 0) {
        perror("chdir");
        printf("Unable to change directory to %s\n", new_path);
    } else {
        //printf("Current directory changed to %s\n", new_path);
    }
}
