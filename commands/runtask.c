/*
 * runtask.c - A simplified script engine with diagnostic messages.
 *
 * Design Principles:
 *   - Minimalism: Only the PRINT command is supported.
 *   - Diagnostics: Detailed error messages are printed to stderr to help
 *                  interpret issues during execution.
 *   - Portability: Uses standard C11 (-std=c11) and only standard libraries.
 *
 * Compilation:
 *   gcc -std=c11 -o runtask runtask.c
 *
 * Usage:
 *   ./runtask mytask.task
 *   (Where mytask.task contains lines like: PRINT "HELLO WORLD")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---------------------------
// Helper: Trim Function
// ---------------------------
// Removes leading and trailing whitespace from a string.
char *trim(char *str) {
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

// ---------------------------
// Main Function: Task Runner with Diagnostics
// ---------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s taskfile\n", argv[0]);
        return 1;
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
            // Empty line, skip it.
            continue;
        }
        // Check if the line starts with "PRINT"
        if (strncmp(line, "PRINT", 5) == 0) {
            // Diagnostic: log processing of PRINT command.
            fprintf(stderr, "Processing PRINT command at line %d: %s\n", lineNumber, line);
            // Locate the first double quote.
            char *start = strchr(line, '\"');
            if (!start) {
                fprintf(stderr, "Error: Missing opening quote for PRINT command at line %d.\n", lineNumber);
                continue;
            }
            start++; // Move past the opening quote.
            // Locate the closing double quote.
            char *end = strchr(start, '\"');
            if (!end) {
                fprintf(stderr, "Error: Missing closing quote for PRINT command at line %d.\n", lineNumber);
                continue;
            }
            size_t len = end - start;
            char message[256];
            if (len >= sizeof(message)) {
                fprintf(stderr, "Warning: Message truncated at line %d.\n", lineNumber);
                len = sizeof(message) - 1;
            }
            strncpy(message, start, len);
            message[len] = '\0';
            printf("%s\n", message);
        } else {
            // If the command is not recognized, output a diagnostic message.
            fprintf(stderr, "Diagnostic: Unrecognized command at line %d: %s\n", lineNumber, line);
        }
    }

    fclose(fp);
    return 0;
}
