#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200112L   /* Changed per instructions to POSIX.1-200112L */

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
#include <signal.h>     // For signal handling
#include <sys/select.h> // For select()
#include <dirent.h>     // For directory handling
#include <sys/stat.h>   // For stat()

#include "commandparser.h"
#include "input.h"      // Include the input handling header

extern void printlogo();
extern void login();
extern void say();

/* Global variable to control paging.
 * 1: paging enabled (default)
 * 0: paging disabled (used for realtime commands)
 */
int paging_enabled = 1;

/* Global variable to store the base directory (extracted from the executable path)
 * so that relative paths like apps/ can be resolved.
 */
char base_directory[PATH_MAX] = {0};

/* Global dynamic list to store realtime commands loaded from apps/ folder. */
char **realtime_commands = NULL;
int realtime_command_count = 0;

/* 
 * Global copies of the original command-line arguments.
 * These will be used by the "restart" command when re-executing the new binary.
 */
static int g_argc;
static char **g_argv;

/* load_realtime_commands()
 *
 * This function scans the "apps/" directory (relative to the base_directory) and
 * adds the name of each executable file found to the realtime_commands list.
 */
void load_realtime_commands(void) {
    char apps_path[PATH_MAX];
    snprintf(apps_path, sizeof(apps_path), "%s/apps", base_directory);
    DIR *dir = opendir(apps_path);
    if (!dir) {
        perror("opendir");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip the special entries "." and ".." */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        /* Construct the full path to the file */
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", apps_path, entry->d_name);
        
        /* Check if it's a regular file and executable */
        struct stat sb;
        if (stat(full_path, &sb) == 0 && S_ISREG(sb.st_mode) && access(full_path, X_OK) == 0) {
            realtime_commands = realloc(realtime_commands, (realtime_command_count + 1) * sizeof(char *));
            if (!realtime_commands) {
                perror("realloc");
                closedir(dir);
                return;
            }
            realtime_commands[realtime_command_count] = strdup(entry->d_name);
            if (!realtime_commands[realtime_command_count]) {
                perror("strdup");
                closedir(dir);
                return;
            }
            realtime_command_count++;
        }
    }
    closedir(dir);
}

/* free_realtime_commands()
 *
 * This function frees the memory allocated for the realtime_commands list.
 */
void free_realtime_commands(void) {
    for (int i = 0; i < realtime_command_count; i++) {
        free(realtime_commands[i]);
    }
    free(realtime_commands);
    realtime_commands = NULL;
    realtime_command_count = 0;
}

/* delay function using busy-wait based on clock() */
void delay(double seconds) {
    clock_t start_time = clock();
    while ((double)(clock() - start_time) / CLOCKS_PER_SEC < seconds) {
        /* Busy waiting */
    }
}

/* delayPrint() prints the provided string one character at a time,
 * waiting for delayTime seconds between each character.
 */
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

/* Displays the current working directory as the prompt. */
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
    int page_height = w.ws_row - 2;
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

int is_realtime_command(const char *command) {
    for (int i = 0; i < realtime_command_count; i++) {
        if (strcmp(command, realtime_commands[i]) == 0)
            return 1;
    }
    return 0;
}

/*
 * Updated execute_command_with_paging():
 * - For realtime commands (or when the "-nopaging" flag is provided), execute directly.
 * - Otherwise, fork a child process to execute the command and capture its output.
 */
void execute_command_with_paging(CommandStruct *cmd) {
    int nopaging = 0;
    /* Check if the command parameters contain "-nopaging" flag. */
    for (int i = 0; i < cmd->param_count; i++) {
        if (strcmp(cmd->parameters[i], "-nopaging") == 0) {
            nopaging = 1;
            /* Remove the "-nopaging" flag by shifting the remaining parameters. */
            for (int j = i; j < cmd->param_count - 1; j++) {
                cmd->parameters[j] = cmd->parameters[j + 1];
            }
            cmd->param_count--;
            break;
        }
    }
    
    /* Realtime mode is now entered if:
     * - The "-nopaging" flag is provided, or
     * - The command is in the realtime command list loaded from apps/ folder.
     */
    if (nopaging || is_realtime_command(cmd->command)) {
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
        // In child process, reset SIGINT to default so that CTRL+C kills the app.
        signal(SIGINT, SIG_DFL);
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
    
    /* Parent process: Close write end and capture output */
    close(pipefd[1]);
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
     */
    size_t line_count = 0;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n')
            line_count++;
    }
    if (total_size > 0 && output[total_size-1] != '\n')
        line_count++;
    
    char **lines = malloc(line_count * sizeof(char *));
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

    /* 
     * Ignore SIGINT in the shell so that CTRL+C does not quit BUDOSTACK.
     * Child processes will reset SIGINT to default.
     */
    signal(SIGINT, SIG_IGN);
    
    /* Store original command-line arguments for later use by the "restart" command */
    g_argc = argc;
    g_argv = argv;
    
    /* Determine the base directory of the executable. */
    char exe_path[PATH_MAX] = {0};
    if (argc > 0) {
        if (realpath(argv[0], exe_path) != NULL) {
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0'; // Terminate the string at the last '/'
            }
            set_base_path(exe_path);
            strncpy(base_directory, exe_path, sizeof(base_directory)-1);
        }
    }
    
    /* Load the realtime command list from the apps/ folder */
    load_realtime_commands();

    /* Clear the screen */
    system("clear");

    /* Modified: Determine if we need to auto-run a command. */
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
        system("clear");
        printlogo();
        login();
        printf("========================================================================\n");
    }

    printf("\nSYSTEM READY");
    say("system ready");
    printf("\nType 'help' for command list.");
    printf("\nType 'exit' to quit.\n\n");

    /* Execute auto_command if set */
    if (auto_command != NULL) {
        parse_input(auto_command, &cmd);
        execute_command_with_paging(&cmd);
        free_command_struct(&cmd);
        free(auto_command);
    }

    /* Main loop */
    while (1) {
        display_prompt();
        input = read_input();
        if (input == NULL) {
            printf("\n");
            break;
        }
        if (input[0] == '\0') {
            free(input);
            continue;
        }
        input[strcspn(input, "\n")] = '\0';
        
        /* NEW: "restart" command handling.
         * When the user types "restart" or "restart -f", the shell first changes its working directory to the base directory,
         * then runs "make" to recompile itself. If "restart -f" is entered, "make clean" is executed before rebuilding.
         * After running make (or make clean failure), a pause is introduced so that build warnings/errors can be read.
         */
        if (strncmp(input, "restart", 7) == 0) {
            int force = 0;
            // Tokenize the input to check for additional parameter "-f"
            char *token = strtok(input, " ");
            token = strtok(NULL, " ");
            if (token && strcmp(token, "-f") == 0) {
                force = 1;
            }
            free(input);
            // Change directory to base_directory so that make is run from the correct location.
            if (chdir(base_directory) != 0) {
                perror("chdir to base_directory failed");
                continue;
            }
            // If force flag is set, run "make clean" before rebuilding.
            if (force) {
                int clean_ret = system("make clean");
                if (clean_ret != 0) {
                    fprintf(stderr, "make clean failed, not restarting.\n");
                    printf("Press ENTER to continue...");
                    fflush(stdout);
                    while(getchar() != '\n');
                    continue;
                }
            }
            int ret = system("make");
            printf("Press ENTER to continue...");
            fflush(stdout);
            while(getchar() != '\n');
            if (ret != 0) {
                fprintf(stderr, "Make failed, not restarting.\n");
                continue;
            } else {
                execv(g_argv[0], g_argv);
                perror("execv failed");
                exit(EXIT_FAILURE);
            }
            continue;
        }
        
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        /* Built-in "cd" command handling */
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
        /* Built-in "run" command handling */
        if (strncmp(input, "run", 3) == 0 && (input[3] == ' ' || input[3] == '\0')) {
            if (input[3] == '\0') {
                fprintf(stderr, "run: missing operand\n");
                free(input);
                continue;
            }
            /* Skip the "run " prefix and trim leading spaces */
            char *shell_command = input + 4;
            while (*shell_command == ' ')
                shell_command++;
            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
            } else if (pid == 0) {
                // Reset SIGINT to default so that CTRL+C can kill the app.
                signal(SIGINT, SIG_DFL);
                execl("/bin/sh", "sh", "-c", shell_command, (char *)NULL);
                perror("execl");
                exit(EXIT_FAILURE);
            } else {
                int status;
                waitpid(pid, &status, 0);
            }
            free(input);
            continue;
        }
        /* Default processing for other commands */
        parse_input(input, &cmd);
        say(&cmd);
        execute_command_with_paging(&cmd);
        free(input);
        free_command_struct(&cmd);
    }
    
    /* Free the realtime command list resources before exiting */
    free_realtime_commands();
    
    printf("Exiting terminal...\n");
    return 0;
}
