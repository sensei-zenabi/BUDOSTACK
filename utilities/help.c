#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * Design Principles:
 * - Written in plain C using -std=c11 and only standard libraries.
 * - No header files are created; everything is contained in a single file.
 * - The code includes comments to clarify design decisions.
 * - If the program is started with the "-a" argument (i.e. "help -a"),
 *   a reserved section is printed for future hidden features or advanced help.
 */
static int print_help_file(const char *path) {
    char buffer[4096];
    FILE *help_file = fopen(path, "rb");

    if (help_file == NULL) {
        fprintf(stderr, "help: failed to open %s: %s\n", path, strerror(errno));
        return 1;
    }

    while (!feof(help_file)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), help_file);

        if (bytes_read > 0 && fwrite(buffer, 1, bytes_read, stdout) != bytes_read) {
            fprintf(stderr, "help: failed to write help output\n");
            fclose(help_file);
            return 1;
        }

        if (ferror(help_file)) {
            fprintf(stderr, "help: failed to read %s\n", path);
            fclose(help_file);
            return 1;
        }
    }

    if (fclose(help_file) != 0) {
        fprintf(stderr, "help: failed to close %s\n", path);
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int status = print_help_file("documents/help.txt");

    if (status != 0) {
        return status;
    }

    // Check for the "-a" argument to display the reserved section
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("This is reserved for something.\n");
        printf("\n");
    }

    return 0;
}
