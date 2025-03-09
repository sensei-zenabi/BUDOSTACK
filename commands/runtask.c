/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, CMD, CLEAR, ROUTE, START, and SHELL commands.
*
* This version uses tmux with a dedicated socket to run external apps in parallel:
* - Each .task file generates a fixed tmux session named "server" and a dedicated socket (e.g. /tmp/tmux_server.sock).
* - RUN commands add apps (from the "apps/" directory) as new windows in that session.
*   The first RUN command creates the session (using "unset TMUX;" to avoid nested warnings)
*   and appends "; exec bash" to keep the window open.
* - SHELL commands add shell scripts (from the "shell/" directory) as new windows in the session.
*   They work similarly to RUN but target shell scripts.
* - A separate START command attaches to the tmux session.
* - The use of a dedicated socket allows multiple parallel tasks to coexist.
* - If tmux is not installed, a warning is issued.
*
* Compilation:
*   gcc -std=c11 -o runtask runtask.c
*
* Usage:
*   ./runtask taskfile [-d]
*
* Design Note:
*   The session name is intentionally fixed to "server" per user request.
*   In addition, the RUN and SHELL commands now use a target like "server:" (with a colon)
*   so that tmux automatically assigns a free window index instead of trying to use 0.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <threads.h> // For thrd_sleep
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

// Global flag to indicate if any RUN or SHELL command has been issued.
int tmux_started = 0;

// The session name for tmux (fixed to "server" as per modification).
char session_name[128] = {0};

// The dedicated socket path for this session.
char socket_path[256] = {0};

void sigint_handler(int signum) {
    (void)signum;
    stop = 1;
}

// ---------------------------
// Function: print_help
// ---------------------------
void print_help(void) {
    printf("\nRuntask Help\n");
    printf("============\n\n");
    printf("Runtask is a simplified script engine that processes a task script\n");
    printf("composed of numbered lines, each containing a single command. The supported\n");
    printf("commands are PRINT, WAIT, GOTO, RUN, CMD, CLEAR, ROUTE, START, and SHELL.\n\n");
    printf("Usage:\n");
    printf("  ./runtask taskfile [-d]\n\n");
    printf("Supported Commands:\n");
    printf("  PRINT \"message\"\n");
    printf("  WAIT milliseconds\n");
    printf("  GOTO line_number\n");
    printf("  RUN executable    (adds an app from the 'apps/' directory as a new tmux window)\n");
    printf("  SHELL script      (runs a shell script from the 'shell/' directory in a new tmux window)\n");
    printf("  CMD executable    (runs an executable from the 'commands/' directory)\n");
    printf("  CLEAR             (clears the screen)\n");
    printf("  ROUTE clear       (clears the file route.rt)\n");
    printf("  ROUTE ...         (appends a route command to route.rt)\n");
    printf("  START             (attaches to the tmux session with all RUN/SHELL apps running)\n\n");
    printf("Compilation:\n");
    printf("  gcc -std=c11 -o runtask runtask.c\n\n");
}

// ---------------------------
// Helper: Trim Function
// ---------------------------
char *trim(char *str) {
    while (isspace((unsigned char)*str))
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

// ---------------------------
// Helper: Delay Function
// ---------------------------
void delay_ms(int ms) {
    int elapsed = 0;
    while (elapsed < ms && !stop) {
        int sleep_time = (ms - elapsed > 50) ? 50 : (ms - elapsed);
        struct timespec ts;
        ts.tv_sec = sleep_time / 1000;
        ts.tv_nsec = (sleep_time % 1000) * 1000000L;
        thrd_sleep(&ts, NULL);
        elapsed += sleep_time;
    }
}

// ---------------------------
// Data Structure for a Script Line
// ---------------------------
typedef struct {
    int number;
    char text[256];
} ScriptLine;

// Comparison function for qsort based on line numbers.
int cmpScriptLine(const void *a, const void *b) {
    const ScriptLine *lineA = (const ScriptLine *)a;
    const ScriptLine *lineB = (const ScriptLine *)b;
    return lineA->number - lineB->number;
}

// ---------------------------
// Main Function: Task Runner
// ---------------------------
int main(int argc, char *argv[]) {
    // Install signal handler for CTRL+C.
    signal(SIGINT, sigint_handler);

    if (argc >= 2 && strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s taskfile [-d]\n", argv[0]);
        return 1;
    }
    int debug = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug = 1;
            break;
        }
    }

    // Check if tmux is installed.
    if (system("command -v tmux > /dev/null 2>&1") != 0) {
        fprintf(stderr, "Warning: tmux is not installed. Please install tmux to enable parallel RUN/SHELL command support.\n");
    }

    // Set a fixed tmux session name "server".
    snprintf(session_name, sizeof(session_name), "server");
    // Define a dedicated socket path for this session.
    snprintf(socket_path, sizeof(socket_path), "/tmp/tmux_%s.sock", session_name);
    if (debug)
        fprintf(stderr, "Using tmux session name: %s\nSocket: %s\n", session_name, socket_path);

    // Prepend "tasks/" to the taskfile argument.
    char task_path[512];
    snprintf(task_path, sizeof(task_path), "tasks/%s", argv[1]);
    FILE *fp = fopen(task_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", task_path);
        return 1;
    }

    // Read and parse the script file.
    ScriptLine scriptLines[1024];
    int count = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        char *line = trim(buffer);
        if (line[0] == '\0')
            continue;
        int lineNumber = 0, offset = 0;
        if (sscanf(line, "%d%n", &lineNumber, &offset) != 1) {
            if (debug)
                fprintf(stderr, "Error: Missing or invalid line number in: %s\n", line);
            continue;
        }
        scriptLines[count].number = lineNumber;
        char *commandPart = trim(line + offset);
        strncpy(scriptLines[count].text, commandPart, sizeof(scriptLines[count].text) - 1);
        scriptLines[count].text[sizeof(scriptLines[count].text) - 1] = '\0';
        count++;
    }
    fclose(fp);

    // Sort the script lines by line number.
    qsort(scriptLines, count, sizeof(ScriptLine), cmpScriptLine);

    // Process the script.
    int pc = 0;
    while (pc < count && !stop) {
        if (debug)
            fprintf(stderr, "Executing line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);

        if (strncmp(scriptLines[pc].text, "PRINT", 5) == 0) {
            char *start = strchr(scriptLines[pc].text, '\"');
            if (!start) {
                if (debug)
                    fprintf(stderr, "Error: Missing opening quote for PRINT command at line %d.\n", scriptLines[pc].number);
                pc++;
                continue;
            }
            start++;
            char *end = strchr(start, '\"');
            if (!end) {
                if (debug)
                    fprintf(stderr, "Error: Missing closing quote for PRINT command at line %d.\n", scriptLines[pc].number);
                pc++;
                continue;
            }
            size_t len = end - start;
            char message[256];
            if (len >= sizeof(message)) {
                if (debug)
                    fprintf(stderr, "Warning: Message truncated at line %d.\n", scriptLines[pc].number);
                len = sizeof(message) - 1;
            }
            strncpy(message, start, len);
            message[len] = '\0';
            printf("%s\n", message);
        }
        else if (strncmp(scriptLines[pc].text, "WAIT", 4) == 0) {
            int ms;
            if (sscanf(scriptLines[pc].text, "WAIT %d", &ms) == 1) {
                delay_ms(ms);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid WAIT command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        else if (strncmp(scriptLines[pc].text, "GOTO", 4) == 0) {
            int target;
            if (sscanf(scriptLines[pc].text, "GOTO %d", &target) == 1) {
                int found = -1;
                for (int i = 0; i < count; i++) {
                    if (scriptLines[i].number == target) {
                        found = i;
                        break;
                    }
                }
                if (found == -1) {
                    if (debug)
                        fprintf(stderr, "Error: GOTO target %d not found from line %d.\n",
                                target, scriptLines[pc].number);
                } else {
                    pc = found;
                    continue;
                }
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid GOTO command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        else if (strncmp(scriptLines[pc].text, "RUN", 3) == 0) {
            char executable[256];
            if (sscanf(scriptLines[pc].text, "RUN %s", executable) == 1) {
                char path[512];
                snprintf(path, sizeof(path), "apps/%s", executable);
                char command[1024];

                // Check if the session exists by querying tmux with our dedicated socket.
                char check_cmd[256];
                snprintf(check_cmd, sizeof(check_cmd),
                         "unset TMUX; tmux -S %s has-session -t %s 2>/dev/null", socket_path, session_name);
                int session_exists = (system(check_cmd) == 0);

                if (!session_exists) {
                    // Create a new detached tmux session with the fixed session name.
                    snprintf(command, sizeof(command),
                             "unset TMUX; tmux -S %s new-session -d -s %s -n %s \"%s; exec bash\"",
                             socket_path, session_name, executable, path);
                    tmux_started = 1;
                } else {
                    // Create a new window in the existing session.
                    // Note the colon after session_name, which lets tmux assign the next free index.
                    snprintf(command, sizeof(command),
                             "unset TMUX; tmux -S %s new-window -t %s: -n %s \"%s; exec bash\"",
                             socket_path, session_name, executable, path);
                }
                if (debug)
                    fprintf(stderr, "Executing RUN: %s\n", command);
                system(command);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid RUN command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        // ---------------------------
        // New SHELL command block
        // ---------------------------
        else if (strncmp(scriptLines[pc].text, "SHELL", 5) == 0) {
            char script[256];
            if (sscanf(scriptLines[pc].text, "SHELL %s", script) == 1) {
                char path[512];
                // Build the path to the shell script in the "shell/" directory.
                snprintf(path, sizeof(path), "shell/%s", script);
                char command[1024];

                // Check if the tmux session exists by querying tmux with our dedicated socket.
                char check_cmd[256];
                snprintf(check_cmd, sizeof(check_cmd),
                         "unset TMUX; tmux -S %s has-session -t %s 2>/dev/null", socket_path, session_name);
                int session_exists = (system(check_cmd) == 0);

                if (!session_exists) {
                    // Create a new detached tmux session with the fixed session name for the SHELL command.
                    snprintf(command, sizeof(command),
                             "unset TMUX; tmux -S %s new-session -d -s %s -n %s \"%s; exec bash\"",
                             socket_path, session_name, script, path);
                    tmux_started = 1;
                } else {
                    // Create a new window in the existing session.
                    snprintf(command, sizeof(command),
                             "unset TMUX; tmux -S %s new-window -t %s: -n %s \"%s; exec bash\"",
                             socket_path, session_name, script, path);
                }
                if (debug)
                    fprintf(stderr, "Executing SHELL: %s\n", command);
                system(command);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid SHELL command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        else if (strncmp(scriptLines[pc].text, "CMD", 3) == 0) {
            char executable[256];
            if (sscanf(scriptLines[pc].text, "CMD %s", executable) == 1) {
                char path[512];
                snprintf(path, sizeof(path), "commands/%s", executable);
                if (debug)
                    fprintf(stderr, "Running command: %s\n", path);
                pid_t pid = fork();
                if (pid < 0) {
                    if (debug)
                        fprintf(stderr, "Error: fork() failed for %s\n", path);
                } else if (pid == 0) {
                    char *argv[] = { path, NULL };
                    execv(path, argv);
                    if (debug)
                        fprintf(stderr, "Error: execv() failed for %s\n", path);
                    exit(EXIT_FAILURE);
                } else {
                    int status;
                    while (waitpid(pid, &status, 0) < 0) {
                        if (errno != EINTR) {
                            if (debug)
                                fprintf(stderr, "Error: waitpid() failed for %s\n", path);
                            break;
                        }
                    }
                }
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid CMD command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        else if (strncmp(scriptLines[pc].text, "CLEAR", 5) == 0) {
            printf("\033[H\033[J");
            fflush(stdout);
        }
        else if (strncmp(scriptLines[pc].text, "ROUTE", 5) == 0) {
            char *args = trim(scriptLines[pc].text + 5);
            if (strcmp(args, "clear") == 0) {
                FILE *rf = fopen("route.rt", "w");
                if (!rf) {
                    if (debug)
                        fprintf(stderr, "Error: Could not clear route file route.rt at line %d.\n", scriptLines[pc].number);
                } else {
                    fclose(rf);
                }
            } else {
                FILE *rf = fopen("route.rt", "a");
                if (!rf) {
                    if (debug)
                        fprintf(stderr, "Error: Could not open route file route.rt for appending at line %d.\n", scriptLines[pc].number);
                } else {
                    fprintf(rf, "route %s\n", args);
                    fclose(rf);
                }
            }
        }
        else if (strncmp(scriptLines[pc].text, "START", 5) == 0) {
            // The START command attaches to the tmux session.
            if (tmux_started) {
                char attach_cmd[256];
                snprintf(attach_cmd, sizeof(attach_cmd),
                         "unset TMUX; tmux -S %s attach -t %s", socket_path, session_name);
                if (debug)
                    fprintf(stderr, "Attaching to tmux session '%s'...\n", session_name);
                system(attach_cmd);
            } else {
                fprintf(stderr, "No tmux session created. Please run a RUN or SHELL command first.\n");
            }
        }
        else {
            if (debug)
                fprintf(stderr, "Diagnostic: Unrecognized command at line %d: %s\n",
                        scriptLines[pc].number, scriptLines[pc].text);
        }
        pc++;
        if (stop) {
            if (debug)
                fprintf(stderr, "Execution interrupted by user (CTRL+C).\n");
            break;
        }
    }
    return 0;
}
