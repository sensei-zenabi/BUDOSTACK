/*
 * main.c
 *
 * This file implements a simple Linux-like terminal with integrated paging.
 * Paging is applied to the output of external commands only if the output
 * exceeds one screen. The output is captured via a pipe and then, if needed,
 * presented page-by-page using arrow keys for scrolling and 'q' to quit.
 *
 * Design Principles:
 * - **Conditional Paging:** Output is only paged when it exceeds the terminal height.
 * - **Separation of Concerns:** The code wraps command execution to capture output,
 *   then either prints it directly or calls a pager.
 * - **Minimal Dependencies:** Uses only standard C libraries and POSIX APIs.
 * - **Interactive Paging:** The pager function uses termios and ANSI escape sequences
 *   to allow scrolling.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <termios.h>   // For terminal control (raw mode)
#include <sys/ioctl.h> // For querying terminal window size
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "commandparser.h"
#include "input.h"     // Include the new header

// Displays the current working directory as the prompt.
void display_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("shell$ ");
}

// Pager function:
// Displays the given text (an array of lines) page by page.
// The user can scroll using the up/down arrow keys and quit by pressing 'q'.
// When quitting, a newline is printed so that the next prompt starts on a new line.
void pager(const char **lines, size_t line_count) {
    struct winsize w;
    // Attempt to get terminal window size; fallback if needed.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24; // Default value if size cannot be obtained.
    }
    int page_height = w.ws_row - 1; // Reserve one line for status/instructions.
    if (page_height < 1) {
        page_height = 10; // Fallback page height.
    }

    int start = 0;
    while (1) {
        // Clear the screen using ANSI escape codes.
        printf("\033[H\033[J");
        // Display a page of text.
        for (int i = start; i < start + page_height && i < (int)line_count; i++) {
            printf("%s\n", lines[i]);
        }
        // Show status with page info and instructions.
        printf("\nPage %d/%d - Use Up/Down arrows to scroll, 'q' to quit.", 
               start / page_height + 1, (int)((line_count + page_height - 1) / page_height));
        fflush(stdout);

        // Set terminal to raw mode to capture arrow key inputs immediately.
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        int c = getchar();

        // Restore original terminal settings.
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

        if (c == 'q') {
            break;  // Quit pager.
        } else if (c == '\033') { // Escape sequence (possible arrow key).
            if (getchar() == '[') {
                int code = getchar();
                if (code == 'A') { // Up arrow.
                    if (start - page_height >= 0)
                        start -= page_height;
                    else
                        start = 0;
                } else if (code == 'B') { // Down arrow.
                    if (start + page_height < (int)line_count)
                        start += page_height;
                }
            }
        }
        // Other keys are ignored.
    }
    // Print a newline after exiting the pager to ensure the prompt starts on a new line.
    printf("\n");
}

// execute_command_with_paging:
// Wraps around execute_command() to capture its output and, if needed,
// display it through the pager. If the output fits on one screen, it is printed directly.
void execute_command_with_paging(CommandStruct *cmd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    
    // Save the original stdout file descriptor.
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout == -1) {
        perror("dup");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    
    // Redirect stdout to the pipe's write end.
    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
        perror("dup2");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    close(pipefd[1]); // Close duplicate write end.

    // Execute the command.
    execute_command(cmd);
    
    // Flush any pending output.
    fflush(stdout);
    // Restore stdout.
    if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
        perror("dup2 restore");
    }
    close(saved_stdout);
    
    // Read the captured output from the pipe.
    char buffer[4096];
    size_t total_size = 0;
    size_t buffer_size = 4096;
    char *output = malloc(buffer_size);
    if (!output) {
        perror("malloc");
        close(pipefd[0]);
        return;
    }
    ssize_t bytes;
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (total_size + bytes >= buffer_size) {
            buffer_size *= 2;
            char *temp = realloc(output, buffer_size);
            if (!temp) {
                perror("realloc");
                free(output);
                close(pipefd[0]);
                return;
            }
            output = temp;
        }
        memcpy(output + total_size, buffer, bytes);
        total_size += bytes;
    }
    close(pipefd[0]);
    
    // If there's no output, nothing to display.
    if (total_size == 0) {
        free(output);
        return;
    }
    output[total_size] = '\0';

    // Split the captured output into lines.
    size_t line_count = 0;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n')
            line_count++;
    }
    // Allocate an array for pointers to each line.
    char **lines = malloc((line_count + 1) * sizeof(char *));
    if (!lines) {
        perror("malloc");
        free(output);
        return;
    }
    size_t current_line = 0;
    char *saveptr;
    char *token = strtok_r(output, "\n", &saveptr);
    while (token != NULL) {
        lines[current_line++] = token;
        token = strtok_r(NULL, "\n", &saveptr);
    }
    
    // Determine terminal page height.
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    int page_height = w.ws_row - 1;
    if (page_height < 1) {
        page_height = 10;
    }
    
    // If output fits on one screen, print it directly.
    if ((int)current_line <= page_height) {
        for (size_t i = 0; i < current_line; i++) {
            printf("%s\n", lines[i]);
        }
    } else {
        // Otherwise, invoke the pager to display the captured output.
        pager((const char **)lines, current_line);
    }
    
    free(lines);
    free(output);
}

int main(void) {
    char *input;
    CommandStruct cmd;
    printf("Welcome to the Linux-like Terminal!\nType 'exit' to quit.\n");
    while (1) {
        display_prompt();
        input = read_input();
        if (input == NULL) {
            printf("\n");
            break;
        }
        /* Remove the trailing newline if any */
        input[strcspn(input, "\n")] = '\0';

        // Check for built-in "exit" command.
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        // Handle built-in "cd" command separately.
        if (strncmp(input, "cd", 2) == 0) {
            parse_input(input, &cmd);
            if (cmd.param_count > 0) {
                if (chdir(cmd.parameters[0]) != 0)
                    perror("cd");
            } else {
                fprintf(stderr, "cd: missing operand\n");
            }
            free(input);
            free_command_struct(&cmd);
            continue;
        }

        // For all other commands, capture and conditionally page their output.
        parse_input(input, &cmd);
        execute_command_with_paging(&cmd);
        free(input);
        free_command_struct(&cmd);
    }
    printf("Exiting terminal...\n");
    return 0;
}
