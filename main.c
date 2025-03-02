/*
 * main.c
 *
 * This file implements a simple Linux-like terminal.
 *
 * Original functionality:
 * - Integrated paging for output when needed.
 * - Captures external command output via a pipe and pages it if it exceeds one screen.
 *
 * Modifications:
 * - At startup, a list of commands that use realtime mode (without paging/buffering)
 *   is displayed to the user.
 * - When a realtime command is executed, a debug message is printed and the command's
 *   output is displayed immediately.
 * - **Modified:** If the application is started with a single argument (not "-f"),
 *   it automatically simulates starting in "-f" mode and then executes the command
 *   "runtask <argument>".
 *
 * Design Principles:
 * - **Modularity & Separation of Concerns:** Command parsing, execution, and paging are
 *   separated.
 * - **Real-Time Feedback:** Realtime commands are executed directly, allowing their output
 *   (and subroutine execution) to be seen as it happens.
 * - **Minimal Dependencies:** Uses only standard C libraries and POSIX APIs.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <termios.h>    // For terminal control (raw mode)
#include <time.h>       // For time delay function
#include <sys/ioctl.h>  // For querying terminal window size
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "commandparser.h"
#include "input.h"      // Include the input handling header

/* Global variable to control paging.
 * 1: paging enabled (default)
 * 0: paging disabled (used for realtime commands)
 */
int paging_enabled = 1;

// List of commands that use realtime mode (no paging or buffering).
const char *realtime_commands[] = {
    "help", 
    "runtask",
    "hello", 
    NULL
};

/* Helper function to check if a command is to be executed in realtime mode. */
int is_realtime_command(const char *command) {
    for (int i = 0; realtime_commands[i] != NULL; i++) {
        if (strcmp(command, realtime_commands[i]) == 0)
            return 1;
    }
    return 0;
}

// delay function using busy-wait based on clock()
void delay(double seconds) {
    clock_t start_time = clock();
    while ((double)(clock() - start_time) / CLOCKS_PER_SEC < seconds) {
        // Busy waiting.
    }
}

// delayPrint() prints the provided string one character at a time,
// waiting for delayTime seconds between each character.
void delayPrint(const char *str, double delayTime) {
    for (int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
        fflush(stdout);
        delay(delayTime);
    }
}

/* This function can be called by any command to disable paging. */
void disable_paging(void) {
    paging_enabled = 0;
}

/* Forward declaration for search mode. */
int search_mode(const char **lines, size_t line_count, const char *query);

// Displays the current working directory as the prompt.
void display_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("shell$ ");
}

/* search_mode remains unchanged from the original implementation. */
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
    int active = 0;
    int menu_start = 0;
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    int menu_height = w.ws_row - 1;
    while (1) {
        printf("\033[H\033[J"); // Clear screen.
        int end = menu_start + menu_height;
        if (end > match_count)
            end = match_count;
        for (int i = menu_start; i < end; i++) {
            if (i == active) {
                printf("\033[7m"); // Highlight active match.
            }
            printf("Line %d: %s", matches[i] + 1, lines[matches[i]]);
            if (i == active) {
                printf("\033[0m"); // Reset formatting.
            }
            printf("\n");
        }
        printf("\nUse Up/Down arrows to select, Enter to jump, 'q' to cancel.\n");
        fflush(stdout);
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (ch == 'q') {
            active = -1;
            break;
        } else if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == '\033') { // Arrow key
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

/* Pager function remains unchanged from the original implementation. */
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
        printf("\033[H\033[J"); // Clear the screen.
        for (int i = start; i < start + page_height && i < (int)line_count; i++) {
            printf("%s\n", lines[i]);
        }
        printf("\nPage %d/%d - Use Up/Down arrows to scroll, 'f' to search, 'q' to quit.",
               start / page_height + 1, (int)((line_count + page_height - 1) / page_height));
        fflush(stdout);
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int c = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (c == 'q') {
            break;
        } else if (c == '\033') {
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
    }
    printf("\n");
}

/*
 * Modified execute_command_with_paging():
 * - If the command is in the realtime list (as determined by is_realtime_command()),
 *   print a debug message and execute command directly.
 * - Otherwise, capture its output and page it as needed.
 */
void execute_command_with_paging(CommandStruct *cmd) {
    if (is_realtime_command(cmd->command)) {
        // Realtime mode: Print debug message and execute command directly.
        printf("Paging disabled for: %s\n", cmd->command);
        fflush(stdout);
        execute_command(cmd);
        return;
    }
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
    // If the output fits in one page, print directly.
    if ((int)current_line <= page_height) {
        for (size_t i = 0; i < current_line; i++) {
            printf("%s\n", lines[i]);
        }
    } else {
        pager((const char **)lines, current_line);
    }
    free(lines);
    free(output);
}

int main(int argc, char *argv[]) {
    char *input;
    CommandStruct cmd;
    // Clear the screen
    system("clear");

    // Modified: Determine if we need to auto-run a command.
    // If a single argument is provided and it is not "-f", build the auto-run command.
    char *auto_command = NULL;
    if (argc == 2 && strcmp(argv[1], "-f") != 0) {
        size_t len = strlen("runtask ") + strlen(argv[1]) + 1;
        auto_command = malloc(len);
        if (auto_command == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        snprintf(auto_command, len, "runtask %s", argv[1]);
    }

    // Modified: Skip startup messages if in forced mode (-f) or auto_command mode.
    if ((argc > 1 && strcmp(argv[1], "-f") == 0) || auto_command != NULL) {
        // Do not print startup messages.
    } else {
        // Startup messages.
        printf("Starting kernel");
        delayPrint("...", 0.3);
        printf("\nKernel started!");
        printf("\n\nSTARTING SYSTEM:");
        printf("\n\n Calibrating Zero‑Point Data Modules");
        delayPrint("..........", 0.3);
        printf("\n Synchronizing Temporal Data Vectors");
        delayPrint("..", 0.3);
        printf("\n Finalizing inter-module diagnostics");
        delayPrint(".....", 0.3);
        printf("\n Creating hyper-threading");
        delayPrint("...", 0.3);
        printf("\n Performing system integrity checks");
        delayPrint("...........", 0.3);
        printf("\n Cleaning");
        delayPrint("....", 0.3);
    }

    printf("\n\nSYSTEM READY");
    if (0) {
        // Print the list of realtime commands.
        printf("\n\nRealtime Mode Commands (output will be displayed immediately):\n");
        for (int i = 0; realtime_commands[i] != NULL; i++) {
            printf(" %s\n", realtime_commands[i]);
        }
    }
	printf("\nType 'help' for command list.");
    printf("\nType 'exit' to quit.\n\n");

    // Modified: If an auto_command was built, execute it once before entering the main loop.
    if (auto_command != NULL) {
        parse_input(auto_command, &cmd);
        execute_command_with_paging(&cmd);
        free_command_struct(&cmd);
        free(auto_command);
    }

    // Main loop.
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
