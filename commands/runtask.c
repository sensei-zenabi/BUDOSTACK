/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, CLEAR, CMD, and now ROUTE commands.
*
* Design Principles:
* - Minimalism: Only the PRINT, WAIT, GOTO, RUN, CLEAR, CMD, and now ROUTE commands are supported.
* - Script Organization: The engine reads the entire script (with numbered lines)
*   into memory, sorts them by line number, and uses a program counter to simulate jumps (GOTO).
* - Diagnostics: Detailed error messages are printed to stderr only when the debug flag (-d) is provided.
* - Portability: Uses standard C11 (-std=c11) and only standard libraries.
*
* Compilation:
* gcc -std=c11 -o runtask runtask.c
*
* Usage:
* ./runtask taskfile [-d]
*
* Help:
* To display this help, type:
* ./runtask -help
*
* Example TASK script:
* 10 PRINT "THIS IS MY TASK:"
* 20 PRINT "HELLO WORLD"
* 30 WAIT 1000
* 40 PRINT "I WAITED 1000ms"
* 50 WAIT 2000
* 60 PRINT "I WAITED 2000ms"
* 70 RUN example
* 80 CMD help
* 90 GOTO 10
* 100 CLEAR        <-- CLEAR command to clear the screen
* 110 ROUTE clear  <-- ROUTE command to clear the file route.rt
* 120 ROUTE 1 1 2 1  <-- Appends "route 1 1 2 1" to route.rt
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <threads.h> // For thrd_sleep
#include <unistd.h>  // For fork, execv, and sleep functions
#include <sys/wait.h>
#include <errno.h>

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

// Signal handler for CTRL+C (SIGINT)
void sigint_handler(int signum) {
    (void)signum; // Unused parameter
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
    printf("commands are PRINT, WAIT, GOTO, RUN, CLEAR, CMD, and now ROUTE. The engine is designed with minimalism\n");
    printf("and portability in mind, using standard C11 and only standard libraries.\n\n");
    printf("Usage:\n");
    printf(" ./runtask taskfile [-d]\n\n");
    printf(" taskfile : A file containing the task script with numbered lines.\n");
    printf(" -d : (Optional) Enables debug mode, providing detailed error messages.\n\n");
    printf("Supported Commands:\n");
    printf(" PRINT \"message\"\n");
    printf(" WAIT milliseconds\n");
    printf(" GOTO line_number\n");
    printf(" RUN executable    (runs an executable from the 'apps/' directory)\n");
    printf(" CMD executable    (runs an executable from the 'commands/' directory)\n");
    printf(" CLEAR             (clears the screen)\n");
    printf(" ROUTE clear       (clears the file route.rt)\n");
    printf(" ROUTE ...         (appends the command prefixed by 'route ' into route.rt)\n\n");
    printf("Example TASK Script:\n");
    printf("--------------------\n");
    printf(" 10 PRINT \"THIS IS MY TASK:\"\n");
    printf(" 20 CMD help\n");
    printf(" 30 WAIT 1000\n");
    printf(" 40 PRINT \"I WAITED 1000ms\"\n");
    printf(" 50 WAIT 2000\n");
    printf(" 60 PRINT \"I WAITED 2000ms\"\n");
    printf(" 70 RUN example\n");
    printf(" 80 GOTO 10\n");
    printf(" 90 CLEAR\n");
    printf("100 ROUTE clear\n");
    printf("110 ROUTE 1 1 2 1\n\n");
    printf("Compilation:\n");
    printf(" gcc -std=c11 -o runtask runtask.c\n\n");
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
    int number;      // The line number (e.g., 10, 20, ...)
    char text[256];  // The command (e.g., PRINT "HELLO WORLD")
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
    // Install the signal handler for SIGINT (CTRL+C)
    signal(SIGINT, sigint_handler);

    // Check if the help option is requested.
    if (argc >= 2 && strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s taskfile [-d]\n", argv[0]);
        return 1;
    }
    int debug = 0;
    // Check for the debug flag in additional arguments.
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug = 1;
            break;
        }
    }

    // Prepend "tasks/" to the taskfile argument.
    char task_path[512];
    snprintf(task_path, sizeof(task_path), "tasks/%s", argv[1]);
    FILE *fp = fopen(task_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", task_path);
        return 1;
    }

    // Read and Parse the Script File
    ScriptLine scriptLines[1024];
    int count = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        char *line = trim(buffer);
        if (line[0] == '\0') {
            continue;
        }
        int lineNumber = 0;
        int offset = 0;
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

    // Sort the script lines by their line numbers.
    qsort(scriptLines, count, sizeof(ScriptLine), cmpScriptLine);

    // Execute the Script
    int pc = 0; // Program counter: index into scriptLines[]
    while (pc < count && !stop) {
        if (debug)
            fprintf(stderr, "Executing line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);

        // PRINT command
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
        // WAIT command
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
        // GOTO command
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
        // RUN command: Executes an external program from the "apps/" directory.
        else if (strncmp(scriptLines[pc].text, "RUN", 3) == 0) {
            char executable[256];
            if (sscanf(scriptLines[pc].text, "RUN %s", executable) == 1) {
                char path[512];
                snprintf(path, sizeof(path), "apps/%s", executable);
                if (debug)
                    fprintf(stderr, "Running executable: %s\n", path);
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
                    fprintf(stderr, "Error: Invalid RUN command format at line %d: %s\n",
                            scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        // CMD command: Executes an external program from the "commands/" directory.
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
        // ROUTE command: Handles routing commands.
        else if (strncmp(scriptLines[pc].text, "ROUTE", 5) == 0) {
            // Get the arguments after "ROUTE"
            char *args = scriptLines[pc].text + 5;
            args = trim(args);
            // If "clear", then clear the route file.
            if (strcmp(args, "clear") == 0) {
                FILE *rf = fopen("route.rt", "w");
                if (!rf) {
                    if (debug)
                        fprintf(stderr, "Error: Could not clear route file route.rt at line %d.\n", scriptLines[pc].number);
                } else {
                    fclose(rf);
                }
            }
            // Otherwise, append the route command to route.rt.
            else {
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
        // CLEAR command: Clears the terminal screen using ANSI escape sequences.
        else if (strncmp(scriptLines[pc].text, "CLEAR", 5) == 0) {
            printf("\033[H\033[J");
            fflush(stdout);
        }
        // Unrecognized command
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
