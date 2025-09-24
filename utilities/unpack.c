/* 
 * unpack.c - Unpacks a zip archive to the current directory.
 * 
 * Design:
 * - Uses the standard library only.
 * - Validates command-line arguments and prints usage instructions if incorrect or "-help" is given.
 * - Constructs a command string to invoke "unzip <zip_file>".
 * - Uses system() to call the unzip command.
 * - Compiled as plain C (C11, no additional libraries).
 *
 * Compile with: gcc -std=c11 -o unpack unpack.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check for help or invalid number of arguments
    if (argc != 2 || strcmp(argv[1], "-help") == 0) {
        printf("Usage: %s <zip_file>\n", argv[0]);
        printf("Unpacks the specified zip archive to the current directory.\n");
        return 1;
    }
    
    // Prepare the command string to unpack using unzip
    char command[1024];
    int n = snprintf(command, sizeof(command), "unzip \"%s\"", argv[1]);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        fprintf(stderr, "Error: command buffer overflow\n");
        return 1;
    }
    
    // Execute the command
    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "Error: unzip command failed with code %d\n", ret);
        return ret;
    }
    
    return 0;
}
