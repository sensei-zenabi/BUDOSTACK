/*
 * runtask.c - A simplified script engine with PRINT and WAIT commands.
 *
 * Design Principles:
 *   - Minimalism: Only the PRINT and WAIT commands are supported.
 *   - Diagnostics: Detailed error messages are printed to stderr only when
 *                  the debug flag (-d) is provided.
 *   - Portability: Uses standard C11 (-std=c11) and only standard libraries.
 *
 * Compilation:
 *   gcc -std=c11 -o runtask runtask.c
 *
 * Usage:
 *   ./runtask taskfile [-d]
 *   (Where taskfile contains commands such as:
 *      PRINT "HELLO WORLD"
 *      WAIT 1000   --> Waits 1000ms)
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
// Main Function: Task Runner with PRINT and WAIT Commands
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

    char buffer[256];
    int lineNumber = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        lineNumber++;
        char *line = trim(buffer);
        if (line[0] == '\0') {
            // Empty line; skip it.
            continue;
        }
        
        // Process PRINT command.
        if (strncmp(line, "PRINT", 5) == 0) {
            if (debug)
                fprintf(stderr, "Processing PRINT command at line %d: %s\n", lineNumber, line);
            // Locate the first double quote.
            char *start = strchr(line, '\"');
            if (!start) {
                if (debug)
                    fprintf(stderr, "Error: Missing opening quote for PRINT command at line %d.\n", lineNumber);
                continue;
            }
            start++; // Move past the opening quote.
            // Locate the closing double quote.
            char *end = strchr(start, '\"');
            if (!end) {
                if (debug)
                    fprintf(stderr, "Error: Missing closing quote for PRINT command at line %d.\n", lineNumber);
                continue;
            }
            size_t len = end - start;
            char message[256];
            if (len >= sizeof(message)) {
                if (debug)
                    fprintf(stderr, "Warning: Message truncated at line %d.\n", lineNumber);
                len = sizeof(message) - 1;
            }
            strncpy(message, start, len);
            message[len] = '\0';
            printf("%s\n", message);
        }
        // Process WAIT command.
        else if (strncmp(line, "WAIT", 4) == 0) {
            if (debug)
                fprintf(stderr, "Processing WAIT command at line %d: %s\n", lineNumber, line);
            int ms;
            if (sscanf(line, "WAIT %d", &ms) == 1) {
                delay_ms(ms);
            } else {
                if (debug)
                    fprintf(stderr, "Error: Invalid WAIT command format at line %d: %s\n", lineNumber, line);
            }
        }
        // Unrecognized command.
        else {
            if (debug)
                fprintf(stderr, "Diagnostic: Unrecognized command at line %d: %s\n", lineNumber, line);
        }
    }

    fclose(fp);
    return 0;
}
