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
#include <stdlib.h>
#include <sys/wait.h>   /* for waitpid */

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

// Function to print the BUDOSTACK logo
void printlogo(void) {
    // Each line ends with '\n' to ensure proper line breaks in the terminal.
    // Backslashes are escaped as '\\' so they are printed correctly.
    printf(" ______  _     _ ______   _____  _______ _______ _______ _______ _     _\n");
    printf(" |_____] |     | |     \\ |     | |______    |    |_____| |       |____/ \n");
    printf(" |_____] |_____| |_____/ |_____| ______|    |    |     | |_____  |    \\_\n");
    prettyprint("           ===== BUDOSTACK - The Martial Art of Software =====         ", 20);
    //prettyprint("", 1000);
    prettyprint("                      ...for those about to zen...", 50);
    prettyprint("\r", 1000);

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

/* say: speak the given text via espeak if available.
   If espeak is not installed or fails, do nothing.
   Uses a double-fork so the parent never waits and no SIGCHLD
   handling is needed (avoids interfering with other waitpid calls). */
void say(const char *text)
{
    /* flush stdout so any pending prints appear before we fork */
    fflush(stdout);

    /* check common locations for an executable espeak */
    if (access("/usr/bin/espeak", X_OK) != 0 &&
        access("/usr/local/bin/espeak", X_OK) != 0) {
        return;
    }

    /* first fork: parent returns immediately */
    pid_t pid = fork();
    if (pid < 0) {
        /* fork failed; give up */
        return;
    }
    if (pid > 0) {
        /* parent: don't wait, just return */
        return;
    }

    /* first child: fork again to detach */
    pid_t pid2 = fork();
    if (pid2 < 0) {
        /* second fork failed; exit child */
        _exit(EXIT_FAILURE);
    }
    if (pid2 > 0) {
        /* first child exits immediately */
        _exit(EXIT_SUCCESS);
    }

    /* grandchild: replace with espeak */
    execlp("espeak", "espeak",
           "-p", "25",
           "-g", "1",
           text,
           (char *)NULL);

    /* if exec fails, just exit */
    _exit(EXIT_FAILURE);
}

