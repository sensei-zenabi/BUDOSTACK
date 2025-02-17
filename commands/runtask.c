/*
 * runtask.c - A simplified script engine with PRINT, WAIT, GOTO, and RUN commands.
 *
 * Design Principles:
 *   - Minimalism: Only the PRINT, WAIT, GOTO, and RUN commands are supported.
 *   - Script Organization: The engine reads the entire script (with numbered lines)
 *                          into memory, sorts them by line number, and uses a program
 *                          counter to simulate jumps (GOTO).
 *   - Diagnostics: Detailed error messages are printed to stderr only when the
 *                  debug flag (-d) is provided.
 *   - Portability: Uses standard C11 (-std=c11) and only standard libraries.
 *
 * Compilation:
 *   gcc -std=c11 -o runtask runtask.c
 *
 * Usage:
 *   ./runtask taskfile [-d]
 *
 * Help:
 *   To display this help, type:
 *       ./runtask -help
 *
 * Example TASK script:
 *   10 PRINT "THIS IS MY TASK:"
 *   20 PRINT "HELLO WORLD"
 *   30 WAIT 1000
 *   40 PRINT "I WAITED 1000ms"
 *   50 WAIT 2000
 *   60 PRINT "I WAITED 2000ms"
 *   70 RUN example
 *   80 GOTO 10
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <threads.h>   // For thrd_sleep
#include <unistd.h>    // For fork, execv, and sleep functions
#include <sys/wait.h>
#include <errno.h>

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

// Signal handler for CTRL+C (SIGINT)
void sigint_handler(int signum) {
    (void)signum;  // Unused parameter
    stop = 1;
}

// ---------------------------
// Function: print_help
// ---------------------------
// Displays an extensive help message when "runtask -help" is invoked.
// Design comments: This function centralizes help text into a single location,
// making it easier to update or translate in the future.
void print_help(void) {
    printf("\nRuntask Help\n");
    printf("============\n\n");
    printf("Runtask is a simplified script engine that processes a task script\n");
    printf("composed of numbered lines, each containing a single command. The supported\n");
    printf("commands are PRINT, WAIT, GOTO, and RUN. The engine is designed with minimalism\n");
    printf("and portability in mind, using standard C11 and only standard libraries.\n\n");
    
    printf("Usage:\n");
    printf("  ./runtask taskfile [-d]\n\n");
    printf("  taskfile  : A file containing the task script with numbered lines.\n");
    printf("  -d        : (Optional) Enables debug mode, providing detailed error messages.\n\n");
    
    printf("Supported Commands:\n");
    printf("  PRINT \"message\"\n");
    printf("      Prints the specified message to the console. The message must be enclosed\n");
    printf("      in double quotes.\n\n");
    
    printf("  WAIT milliseconds\n");
    printf("      Pauses execution for the specified number of milliseconds. For example, WAIT 1000\n");
    printf("      pauses the script for 1000 ms (1 second).\n\n");
    
    printf("  GOTO line_number\n");
    printf("      Jumps to the script line with the given line number. This is used to create loops\n");
    printf("      or jump to specific sections of the script.\n\n");
    
    printf("  RUN executable\n");
    printf("      Executes an external program located in the 'apps/' directory. The executable\n");
    printf("      name is appended to 'apps/' to form the full path. For instance, RUN example\n");
    printf("      will attempt to run 'apps/example'.\n\n");
    
    printf("Example TASK Script:\n");
    printf("--------------------\n");
    printf("  10 PRINT \"THIS IS MY TASK:\"\n");
    printf("  20 PRINT \"HELLO WORLD\"\n");
    printf("  30 WAIT 1000\n");
    printf("  40 PRINT \"I WAITED 1000ms\"\n");
    printf("  50 WAIT 2000\n");
    printf("  60 PRINT \"I WAITED 2000ms\"\n");
    printf("  70 RUN example\n");
    printf("  80 GOTO 10\n\n");
    
    printf("Additional Details:\n");
    printf("  - The script is read completely into memory, and the lines are sorted by their\n");
    printf("    line numbers before execution.\n");
    printf("  - A program counter (pc) is used to step through the sorted commands, simulating\n");
    printf("    jumps with the GOTO command.\n");
    printf("  - Debug mode (-d) will output diagnostic messages to stderr, including errors such as\n");
    printf("    missing quotes in PRINT commands or invalid command formats.\n");
    printf("  - The engine gracefully handles interruptions (CTRL+C) during execution.\n\n");
    
    printf("Compilation:\n");
    printf("  gcc -std=c11 -o runtask runtask.c\n\n");
    
    printf("For more information or to report issues, please refer to the source comments in runtask.c.\n\n");
}

// ---------------------------
// Helper: Trim Function
// ---------------------------
// Removes leading and trailing whitespace from a string.
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
// Sleeps in small increments to periodically check for CTRL+C (SIGINT).
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
    int number;           // The line number (e.g., 10, 20, ...)
    char text[256];       // The command (e.g., PRINT "HELLO WORLD")
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

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", argv[1]);
        return 1;
    }

    // ---------------------------
    // Read and Parse the Script File
    // ---------------------------
    ScriptLine scriptLines[1024];
    int count = 0;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), fp)) {
        char *line = trim(buffer);
        if (line[0] == '\0') {
            // Skip empty lines.
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

    // ---------------------------
    // Execute the Script
    // ---------------------------
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
            start++; // Move past the opening quote.
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
        // RUN command
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
