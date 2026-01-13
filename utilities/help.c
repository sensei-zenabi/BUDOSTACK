#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

static int path_dirname(char *path) {
    char *slash = strrchr(path, '/');

    if (slash == NULL) {
        return -1;
    }

    if (slash == path) {
        path[1] = '\0';
        return 0;
    }

    *slash = '\0';
    return 0;
}

static int resolve_help_path(char *dest, size_t dest_size, const char *argv0) {
    char base_path[PATH_MAX];
    const char *env_base = getenv("BUDOSTACK_BASE");

    if (env_base && env_base[0] != '\0') {
        if (realpath(env_base, base_path) == NULL) {
            strncpy(base_path, env_base, sizeof(base_path) - 1);
            base_path[sizeof(base_path) - 1] = '\0';
        }
    } else {
        char exe_path[PATH_MAX];
        int resolved = 0;

        if (argv0 && realpath(argv0, exe_path) != NULL) {
            resolved = 1;
        } else {
            ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
            if (len != -1) {
                exe_path[len] = '\0';
                resolved = 1;
            }
        }

        if (!resolved) {
            return -1;
        }

        if (path_dirname(exe_path) != 0) {
            return -1;
        }

        if (path_dirname(exe_path) != 0) {
            return -1;
        }

        strncpy(base_path, exe_path, sizeof(base_path) - 1);
        base_path[sizeof(base_path) - 1] = '\0';
    }

    if (snprintf(dest, dest_size, "%s/documents/help.txt", base_path) >= (int)dest_size) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char help_path[PATH_MAX];
    if (resolve_help_path(help_path, sizeof(help_path), argv[0]) != 0) {
        fprintf(stderr, "help: failed to resolve BUDOSTACK root path\n");
        return 1;
    }

    int status = print_help_file(help_path);

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
