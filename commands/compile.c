/* 
 * File: compile.c
 * Description: A simple command-line tool that wraps gcc to compile one or more C source files.
 *              The first argument is used to determine the output executable name by removing the ".c" suffix.
 *              All provided files are passed to gcc for compilation using the C11 standard.
 *
 * Design principles:
 *  - Use only standard libraries (stdio.h, stdlib.h, string.h).
 *  - Create a simple command wrapper to generate a gcc command.
 *  - Strip the ".c" extension from the first argument for naming the output executable.
 *  - Support a "-help" flag to display usage information.
 *  - Use system() to invoke gcc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Function to remove the ".c" extension from a filename if present */
void strip_extension(const char *filename, char *output, size_t maxlen) {
    size_t len = strlen(filename);
    if (len >= 2 && strcmp(filename + len - 2, ".c") == 0) {
        /* Copy all except the last two characters */
        if (len - 2 < maxlen) {
            strncpy(output, filename, len - 2);
            output[len - 2] = '\0';
        } else {
            /* Fallback: if output buffer too small, copy entire filename */
            strncpy(output, filename, maxlen - 1);
            output[maxlen - 1] = '\0';
        }
    } else {
        /* If not ending with ".c", just copy the filename */
        strncpy(output, filename, maxlen - 1);
        output[maxlen - 1] = '\0';
    }
}

/* Function to display usage information */
void print_help() {
    printf("Example:\n");
    printf("> compile main.c mylib1.c mylib2.c\n");
    printf("This will compile main.c (and link mylib1.c and mylib2.c) into an executable named 'main'.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: No input files provided.\n");
        print_help();
        return EXIT_FAILURE;
    }

    /* Check if the first argument is a help flag */
    if (strcmp(argv[1], "-help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    /* Determine output executable name based on the first file */
    char output_name[256];
    strip_extension(argv[1], output_name, sizeof(output_name));

    /* Construct the gcc command */
    char command[2048];
    int pos = 0;
    pos += snprintf(command + pos, sizeof(command) - pos, "gcc -std=c11 ");

    /* Append each provided source file */
    for (int i = 1; i < argc; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, "%s ", argv[i]);
    }

    /* Append the output flag and executable name */
    pos += snprintf(command + pos, sizeof(command) - pos, "-o %s", output_name);

    /* Print the command for debugging purposes (can be removed) */
    printf("Running command: %s\n", command);

    /* Execute the command */
    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "Compilation failed with error code %d.\n", ret);
        return EXIT_FAILURE;
    }

    printf("Compilation succeeded. Executable: %s\n", output_name);
    return EXIT_SUCCESS;
}
