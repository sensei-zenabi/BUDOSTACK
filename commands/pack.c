/* 
 * pack.c - Packs a file or folder into a zip archive.
 * 
 * Design:
 * - Uses the standard library only.
 * - Checks command-line arguments: if incorrect or "-help" is provided, prints usage info.
 * - Builds a command string to invoke "zip -r <destination> <source>".
 * - Calls system() to execute the command.
 * - Uses plain C (C11, no extra headers) and no additional libraries.
 *
 * Compile with: gcc -std=c11 -o pack pack.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check for help or invalid number of arguments
    if (argc != 3 || strcmp(argv[1], "-help") == 0 || strcmp(argv[2], "-help") == 0) {
        printf("Usage: %s <source_file_or_directory> <destination_zip>\n", argv[0]);
        printf("Packs the specified file or directory into a zip archive.\n");
        return 1;
    }
    
    // Prepare the command string to pack using zip
    char command[1024];
    int n = snprintf(command, sizeof(command), "zip -r \"%s\" \"%s\"", argv[2], argv[1]);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        fprintf(stderr, "Error: command buffer overflow\n");
        return 1;
    }
    
    // Execute the command
    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "Error: zip command failed with code %d\n", ret);
        return ret;
    }
    
    return 0;
}
