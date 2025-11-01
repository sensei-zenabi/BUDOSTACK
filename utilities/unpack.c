/*
 * unpack.c - Unpacks a zip archive to a directory alongside the archive.
 *
 * Design:
 * - Uses the standard library only.
 * - Validates command-line arguments and prints usage instructions if incorrect or "-help" is given.
 * - Determines a target directory by stripping the archive extension.
 * - Ensures the target directory exists before extraction.
 * - Constructs a command string to invoke "unzip -d <target_dir> <zip_file>".
 * - Uses system() to call the unzip command.
 * - Compiled as plain C (C11, no additional libraries).
 *
 * Compile with: gcc -std=c11 -o unpack unpack.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
    // Check for help or invalid number of arguments
    if (argc != 2 || strcmp(argv[1], "-help") == 0) {
        printf("Usage: %s <zip_file>\n", argv[0]);
        printf("Unpacks the specified archive into a matching directory next to it.\n");
        return 1;
    }

    const char *archive_path = argv[1];
    const char *basename = strrchr(archive_path, '/');
    size_t prefix_len = 0;
    if (basename != NULL) {
        prefix_len = (size_t)(basename - archive_path) + 1;
        basename++;
    } else {
        basename = archive_path;
    }

    if (*basename == '\0') {
        fprintf(stderr, "Error: invalid archive name\n");
        return 1;
    }

    const char *extension = strrchr(basename, '.');
    size_t stem_len = extension != NULL && extension != basename ? (size_t)(extension - basename) : strlen(basename);
    if (stem_len == 0) {
        fprintf(stderr, "Error: could not determine output directory\n");
        return 1;
    }

    char output_dir[1024];
    if (prefix_len + stem_len >= sizeof(output_dir)) {
        fprintf(stderr, "Error: output directory path too long\n");
        return 1;
    }

    memcpy(output_dir, archive_path, prefix_len);
    memcpy(output_dir + prefix_len, basename, stem_len);
    output_dir[prefix_len + stem_len] = '\0';

    if (mkdir(output_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        return 1;
    }

    // Prepare the command string to unpack using unzip
    char command[1024];
    int n = snprintf(command, sizeof(command), "unzip -d \"%s\" \"%s\"", output_dir, archive_path);
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
