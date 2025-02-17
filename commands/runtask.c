/*
 * runtask.c - A simplified script engine with PRINT, WAIT, GOTO, and RUN commands.
 *
 * Design Principles:
 *   - Minimalism: Only the PRINT, WAIT, GOTO, and now RUN commands are supported.
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
// Busy-waits for the specified number of milliseconds.
void delay_ms(int ms) {
    double seconds = ms / 1000.0;
    clock_t start = clock();
    while ((double)(clock() - start) / CLOCKS_PER_SEC < seconds)
        ; // Busy waiting
}

// ---------------------------
// Data Structure for a Script Line
// ---------------------------
typedef struct {
    int number;           // The line number (e.g., 10, 20, ...)
    char text[256];       // The command part of the line (e.g., PRINT "HELLO WORLD")
} ScriptLine;

// Comparison function for qsort based on line numbers.
int cmpScriptLine(const void *a, const void *b) {
    const ScriptLine *lineA = (const ScriptLine *)a;
    const ScriptLine *lineB = (const ScriptLine *)b;
    return lineA->number - lineB->number;
}

// ---------------------------
// Main Function: Task Runner with PRINT, WAIT, GOTO, and RUN Commands
// ---------------------------
int main(int argc, char *argv[]) {
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
        // Each line should start with a line number.
        int lineNumber = 0;
        int offset = 0;
        if (sscanf(line, "%d%n", &lineNumber, &offset) != 1) {
            if (debug)
                fprintf(stderr, "Error: Missing or invalid line number in: %s\n", line);
            continue;
        }
        scriptLines[count].number = lineNumber;
        // The rest of the line is the command part.
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
    while (pc < count) {
        if (debug)
            fprintf(stderr, "Executing line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);

        // Check for PRINT command.
        if (strncmp(scriptLines[pc].text, "PRINT", 5) == 0) {
            // Locate the first double quote.
            char *start = strchr(scriptLines[pc].text, '\"');
            if (!start) {
                if (debug)
                    fprintf(stderr, "Error: Missing opening quote for PRINT command at line %d.\n", scriptLines[pc].number);
                pc++;
                continue;
            }
            start++; // Move past the opening quote.
            // Locate the closing double quote.
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
        // Check for WAIT command.
        else if (strncmp(scriptLines[pc].text, "WAIT", 4) == 0) {
            int ms;
            if (sscanf(scriptLines[pc].text, "WAIT %d", &ms) == 1) {
                delay_ms(ms);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid WAIT command format at line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        // Check for GOTO command.
        else if (strncmp(scriptLines[pc].text, "GOTO", 4) == 0) {
            int target;
            if (sscanf(scriptLines[pc].text, "GOTO %d", &target) == 1) {
                // Search for the target line number.
                int found = -1;
                for (int i = 0; i < count; i++) {
                    if (scriptLines[i].number == target) {
                        found = i;
                        break;
                    }
                }
                if (found == -1) {
                    if (debug)
                        fprintf(stderr, "Error: GOTO target %d not found from line %d.\n", target, scriptLines[pc].number);
                    // Continue to next command if target not found.
                } else {
                    // Jump to the target line.
                    pc = found;
                    continue; // Skip the normal pc increment.
                }
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid GOTO command format at line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        // Check for RUN command.
        else if (strncmp(scriptLines[pc].text, "RUN", 3) == 0) {
            char executable[256];
            if (sscanf(scriptLines[pc].text, "RUN %s", executable) == 1) {
                char path[512];
                // Prepend the "apps/" folder to the executable name.
                snprintf(path, sizeof(path), "apps/%s", executable);
                if (debug)
                    fprintf(stderr, "Running executable: %s\n", path);
                int ret = system(path);
                if (ret == -1 && debug)
                    fprintf(stderr, "Error: Could not run executable %s\n", path);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid RUN command format at line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);
            }
        }
        // Unrecognized command.
        else {
            if (debug)
                fprintf(stderr, "Diagnostic: Unrecognized command at line %d: %s\n", scriptLines[pc].number, scriptLines[pc].text);
        }
        pc++;
    }

    return 0;
}
