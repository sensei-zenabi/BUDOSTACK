/*
 * main.c
 *
 * This file implements a simple Linux-like terminal.
 *
 * Original functionality:
 * - Integrated paging for output when needed.
 * - Captures external command output via a pipe and pages it if it exceeds one screen.
 *
 * Modifications in this version:
 * - For non-realtime commands, a child process is forked to run the command with concurrent reading via select().
 * - A timeout (5 seconds) is enforced; if reached, the child is killed.
 * - Both stdout and stderr are captured.
 * - The captured output is split into lines manually so that all content (including empty lines or ANSI escape sequences)
 *   is preserved exactly as provided.
 * - The login() function is now skipped if a task is auto-run (e.g. "./aalto node_perception"), so that it does not disturb
 *   the task's output.
 * - nanosleep is used instead of usleep to avoid implicit declaration warnings under -std=c11.
 *
 * Design Principles:
 * - Modularity & Separation of Concerns: Command parsing, execution, and paging are separated.
 * - Real-Time Feedback: Realtime commands are executed directly.
 * - Safety: The parent reads concurrently so that the pipe never fills up, and the child is killed if it exceeds a timeout.
 * - Robustness: The pager now preserves all output—including empty lines and any escape sequences—in order to faithfully
 *   display the command’s output.
 * - Minimal Dependencies: Uses only standard C libraries and POSIX APIs.
 */

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <termios.h>    // For terminal control (raw mode)
#include <time.h>       // For time functions and nanosleep
#include <sys/ioctl.h>  // For querying terminal window size
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>     // For kill()
#include <sys/select.h> // For select()

#include "commandparser.h"
#include "input.h"      // Include the input handling header

extern void aaltologo();
extern void login();

/* Global variable to control paging.
 * 1: paging enabled (default)
 * 0: paging disabled (used for realtime commands)
 */
int paging_enabled = 1;

// List of commands that use realtime mode (no paging or buffering).
const char *realtime_commands[] = {
    "help",
    "runtask",
    "assist",
    "edit",
    "cmath",
    "inet",
    "list",
    "table",
    "csv_trend",
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
        /* Busy waiting */
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
 * Updated execute_command_with_paging():
 * - For realtime commands, execute directly.
 * - For other commands, fork a child process to execute the command with redirected output.
 * - The parent uses select() to read concurrently from the pipe while waiting for the child.
 * - A timeout (5 seconds) is enforced; if reached, the child is killed.
 * - Both stdout and stderr are captured.
 * - The captured output is split into lines manually, preserving empty lines and all characters.
 */
void execute_command_with_paging(CommandStruct *cmd) {
    if (is_realtime_command(cmd->command)) {
        // Realtime mode: Execute command directly without extra modifications.
        execute_command(cmd);
        return;
    }
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    
    if (pid == 0) {
        /* Child process: Redirect stdout and stderr to the pipe. */
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        if (dup2(pipefd[1], STDERR_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execute_command(cmd);
        exit(EXIT_SUCCESS);
    }
    
    /* Parent process: Close write end. */
    close(pipefd[1]);
    
    /* Set the pipe's read end to non-blocking mode. */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    
    time_t start_time = time(NULL);
    int timeout_seconds = 5; // Timeout in seconds.
    size_t total_size = 0;
    size_t buffer_size = 4096;
    char *output = malloc(buffer_size);
    if (!output) {
        perror("malloc");
        close(pipefd[0]);
        return;
    }
    char buffer[4096];
    
    /* Concurrently read from the pipe while monitoring the child process */
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);
        struct timeval tv = {1, 0}; // 1-second timeout for select()
        int sel_ret = select(pipefd[0] + 1, &readfds, NULL, NULL, &tv);
        if (sel_ret > 0 && FD_ISSET(pipefd[0], &readfds)) {
            ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer));
            if (bytes > 0) {
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
            } else if (bytes == 0) {
                // End-of-file reached.
                break;
            }
        }
        /* Check if child has finished */
        pid_t result = waitpid(pid, NULL, WNOHANG);
        if (result == pid) {
            // Child finished. Continue reading any remaining data.
        }
        if (time(NULL) - start_time > timeout_seconds) {
            /* Timeout reached; kill child process if still running */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            break;
        }
    }
    close(pipefd[0]);
    
    if (total_size == 0) {
        free(output);
        return;
    }
    output[total_size] = '\0';
    
    /*
     * Manually split output into lines while preserving empty lines.
     * We count the number of lines by scanning for '\n'. Each newline terminates a line.
     */
    size_t line_count = 0;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n')
            line_count++;
    }
    /* If the output does not end with a newline, count the last line as well */
    if (total_size > 0 && output[total_size-1] != '\n')
        line_count++;
    
    char **lines = malloc((line_count) * sizeof(char *));
    if (!lines) {
        perror("malloc");
        free(output);
        return;
    }
    size_t current_line = 0;
    char *start = output;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n') {
            output[i] = '\0';  // terminate this line
            lines[current_line++] = start;
            start = output + i + 1;
        }
    }
    /* If the last character is not a newline, capture the remaining text as the last line */
    if (start < output + total_size) {
        lines[current_line++] = start;
    }
    
    /* Determine page height based on terminal size. */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        ws.ws_row = 24;
    }
    int page_height = ws.ws_row - 1;
    if (page_height < 1)
        page_height = 10;
    
    /* If the output fits in one page, print directly; otherwise, page the output. */
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

    /* Determine the base directory of the executable.
       This design decision ensures that commands are looked up relative to the executable's location,
       making the command execution independent of the current working directory.
    */
    char exe_path[PATH_MAX] = {0};
    if (argc > 0) {
        if (realpath(argv[0], exe_path) != NULL) {
            /* Extract the directory part from the full path */
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0'; // Terminate the string at the last '/'
            }
            /* Set the base directory for command lookup */
            set_base_path(exe_path);
        }
    }

    /* Clear the screen */
    system("clear");

    /* Modified: Determine if we need to auto-run a command.
       If a single argument is provided and it is not "-f", build the auto-run command.
    */
    char *auto_command = NULL;
    if (argc == 2 && strcmp(argv[1], "-f") != 0) {
        size_t len = strlen("runtask ") + strlen(argv[1]) + strlen(".task") + 1;
        auto_command = malloc(len);
        if (auto_command == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        snprintf(auto_command, len, "runtask %s.task", argv[1]);
    }

    /* Modified: Skip startup messages if in forced mode (-f) or auto_command mode. */
    if ((argc > 1 && strcmp(argv[1], "-f") == 0) || auto_command != NULL) {
        /* Do not print startup messages and skip login() */
    } else {
        /* Startup messages. */
        system("clear");
        aaltologo();
        printf("\n");
        delayPrint("AALTO - All Around Linux Terminal Operator\n", 0.02);
        delayPrint(" ", 1);
        delayPrint("...for those who enjoy simple things...", 0.05);
        delayPrint(" ", 1);
        login();
    }

    printf("\n\nSYSTEM READY");
    if (0) {
        /* Print the list of realtime commands. */
        printf("\n\nRealtime Mode Commands (output will be displayed immediately):\n");
        for (int i = 0; realtime_commands[i] != NULL; i++) {
            printf(" %s\n", realtime_commands[i]);
        }
    }
    printf("\nType 'help' for command list.");
    printf("\nType 'exit' to quit.\n\n");

    /* Modified: If an auto_command was built, execute it once before entering the main loop. */
    if (auto_command != NULL) {
        parse_input(auto_command, &cmd);
        execute_command_with_paging(&cmd);
        free_command_struct(&cmd);
        free(auto_command);
    }

    /* Main loop. */
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
