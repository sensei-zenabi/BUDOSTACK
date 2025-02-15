/*
 * main.c
 *
 * This file implements a simple Linux-like terminal with integrated paging.
 * Paging is applied to the output of external commands only if the output
 * exceeds one screen. The output is captured via a pipe and then, if needed,
 * presented page-by-page using arrow keys for scrolling, 'f' to search within
 * the output (with an interactive list of matches), and 'q' to quit.
 *
 * Design Principles:
 * - **Conditional Paging:** Output is only paged when it exceeds the terminal height.
 * - **Separation of Concerns:** The code wraps command execution to capture output,
 *   then either prints it directly or calls a pager.
 * - **Interactive Paging & Search:** The pager function uses termios and ANSI escape
 *   sequences to allow scrolling. In search mode, all matches are listed, and the
 *   active match is highlighted.
 * - **Minimal Dependencies:** Uses only standard C libraries and POSIX APIs.
 *
 * Modification:
 * - Added a global variable `paging_enabled` to control paging.
 * - Provided a function `disable_paging()` which can be called by applications (from
 *   the commands folder) to disable paging.
 * - Updated the output display logic in execute_command_with_paging() so that if paging
 *   is disabled, the output is printed directly regardless of its length.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <termios.h>    // For terminal control (raw mode)
#include <sys/ioctl.h>  // For querying terminal window size
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "commandparser.h"
#include "input.h" // Include the new header

/* Global variable to control paging.
 * 1: paging enabled (default)
 * 0: paging disabled */
int paging_enabled = 1;

/* This function can be called by any command (in the commands folder)
 * to disable paging in the terminal output. */
void disable_paging(void) {
    paging_enabled = 0;
}

// Forward declaration for search mode.
int search_mode(const char **lines, size_t line_count, const char *query);

// Displays the current working directory as the prompt.
void display_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("shell$ ");
}

// search_mode:
// Given an array of lines and a search query, find all matching lines and
// present an interactive menu for selecting one. The active match is highlighted.
// Up/down arrow keys scroll the list; Enter selects the active match; 'q' cancels.
// Returns the index in 'lines' of the selected match, or -1 if canceled.
int search_mode(const char **lines, size_t line_count, const char *query) {
    int *matches = malloc(line_count * sizeof(int));
    if (!matches) {
        perror("malloc");
        return -1;
    }
    int match_count = 0;
    for (size_t i = 0; i < line_count; i++) {
        if (strstr(lines[i], query) != NULL) {
            matches[match_count++] = i;
        }
    }
    if (match_count == 0) {
        free(matches);
        printf("No matches found. Press any key to continue...");
        getchar();
        return -1;
    }
    int active = 0; // active match index in the matches array
    int menu_start = 0; // starting index for display
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    int menu_height = w.ws_row - 1; // lines available for menu
    while (1) {
        // Clear screen.
        printf("\033[H\033[J");
        // Determine the window for the menu.
        int end = menu_start + menu_height;
        if (end > match_count)
            end = match_count;
        // Display matches.
        for (int i = menu_start; i < end; i++) {
            if (i == active) {
                // Highlight active match.
                printf("\033[7m"); // reverse video
            }
            // Display match with its original line number (1-indexed) and content.
            printf("Line %d: %s", matches[i] + 1, lines[matches[i]]);
            if (i == active) {
                printf("\033[0m"); // reset formatting
            }
            printf("\n");
        }
        // Display instructions.
        printf("\nUse Up/Down arrows to select, Enter to jump, 'q' to cancel.\n");
        fflush(stdout);
        // Set terminal to raw mode.
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int ch = getchar();
        // Restore terminal settings.
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (ch == 'q') {
            active = -1;
            break;
        } else if (ch == '\n' || ch == '\r') {
            // User selected the active match.
            break;
        } else if (ch == '\033') { // possible arrow key
            if (getchar() == '[') {
                int code = getchar();
                if (code == 'A') { // Up arrow.
                    if (active > 0) {
                        active--;
                        if (active < menu_start)
                            menu_start = active;
                    }
                } else if (code == 'B') { // Down arrow.
                    if (active < match_count - 1) {
                        active++;
                        if (active >= menu_start + menu_height)
                            menu_start = active - menu_height + 1;
                    }
                }
            }
        }
    }
    int result = -1;
    if (active != -1) {
        result = matches[active];
    }
    free(matches);
    return result;
}

// Pager function:
// Displays the given text (an array of lines) page by page.
// The user can scroll using the up/down arrow keys, press 'f' to enter search mode,
// and press 'q' to quit. When quitting, a newline is printed so that the prompt starts on a new line.
void pager(const char **lines, size_t line_count) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    int page_height = w.ws_row - 1;
    if (page_height < 1)
        page_height = 10;
    int start = 0;
    while (1) {
        // Clear the screen.
        printf("\033[H\033[J");
        // Display one page of text.
        for (int i = start; i < start + page_height && i < (int)line_count; i++) {
            printf("%s\n", lines[i]);
        }
        // Show page info and instructions.
        printf("\nPage %d/%d - Use Up/Down arrows to scroll, 'f' to search, 'q' to quit.", 
               start / page_height + 1, (int)((line_count + page_height - 1) / page_height));
        fflush(stdout);
        // Set terminal to raw mode.
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int c = getchar();
        // Restore terminal settings.
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (c == 'q') {
            break; // Quit pager.
        } else if (c == '\033') { // Arrow keys.
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
        } else if (c == 'f') {
            // Exit raw mode and prompt for search query.
            char search[256];
            printf("\nSearch: ");
            fflush(stdout);
            if (fgets(search, sizeof(search), stdin) != NULL) {
                search[strcspn(search, "\n")] = '\0';
                if (strlen(search) > 0) {
                    int selected = search_mode(lines, line_count, search);
                    if (selected != -1)
                        start = selected;
                }
            }
        }
        // Other keys are ignored.
    }
    // Print a newline after exiting pager.
    printf("\n");
}

// execute_command_with_paging:
// Wraps execute_command() to capture its output and, if needed,
// display it through the pager. If the output fits on one screen or if paging is disabled,
// it is printed directly.
void execute_command_with_paging(CommandStruct *cmd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout == -1) {
        perror("dup");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
        perror("dup2");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    close(pipefd[1]);
    // Execute the command.
    execute_command(cmd);
    fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
        perror("dup2 restore");
    }
    close(saved_stdout);
    // Read the captured output.
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
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        ws.ws_row = 24;
    }
    int page_height = ws.ws_row - 1;
    if (page_height < 1)
        page_height = 10;
    // If paging is disabled or the output fits in one page, print directly.
    if (!paging_enabled || (int)current_line <= page_height) {
        for (size_t i = 0; i < current_line; i++) {
            printf("%s\n", lines[i]);
        }
    } else {
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
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
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
        parse_input(input, &cmd);
        execute_command_with_paging(&cmd);
        free(input);
        free_command_struct(&cmd);
    }
    printf("Exiting terminal...\n");
    return 0;
}
