#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
    compile.c

    A simple wrapper around gcc that:
      - Uses -std=c11 always
      - Accepts arbitrary gcc flags (e.g. -lm, -O2, -Wall, etc.)
      - Accepts one or more C source files (*.c)
      - Derives the output executable name from the first source file

    Usage:
      compile [gcc-flags] file1.c [file2.c ...]
    Example:
      compile -lm -O2 main.c util.c
    This invokes:
      gcc -std=c11 main.c util.c -lm -O2 -o main
*/

/* Strip the ".c" extension from filename if present */
static void strip_extension(const char *filename, char *output, size_t maxlen) {
    size_t len = strlen(filename);
    if (len > 2 && strcmp(filename + len - 2, ".c") == 0) {
        size_t outlen = len - 2;
        if (outlen < maxlen) {
            memcpy(output, filename, outlen);
            output[outlen] = '\0';
        } else {
            strncpy(output, filename, maxlen - 1);
            output[maxlen - 1] = '\0';
        }
    } else {
        strncpy(output, filename, maxlen - 1);
        output[maxlen - 1] = '\0';
    }
}

/* Print usage information */
static void print_help(const char *progname) {
    printf("Usage:\n");
    printf("  %s [gcc-flags] file1.c [file2.c ...]\n", progname);
    printf("\nExample:\n");
    printf("  %s -lm -O2 main.c util.c\n", progname);
    printf("  # compiles main.c and util.c into executable 'main'\n");
}

/* Check if a string ends with ".c" */
static int is_c_file(const char *arg) {
    size_t len = strlen(arg);
    return (len > 2 && strcmp(arg + len - 2, ".c") == 0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: No arguments provided.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    /* Help */
    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    /* Collect flags and sources */
    char *flags[argc];
    int  flag_count = 0;
    char *sources[argc];
    int  src_count  = 0;

    for (int i = 1; i < argc; i++) {
        if (is_c_file(argv[i])) {
            sources[src_count++] = argv[i];
        } else if (argv[i][0] == '-') {
            flags[flag_count++] = argv[i];
        } else {
            fprintf(stderr, "Warning: treating \"%s\" as gcc flag\n", argv[i]);
            flags[flag_count++] = argv[i];
        }
    }

    if (src_count == 0) {
        fprintf(stderr, "Error: No C source files provided.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    /* Determine output name from first source */
    char output_name[256];
    strip_extension(sources[0], output_name, sizeof(output_name));

    /* Build gcc command: sources first, then flags, then -o */
    char cmd[4096];
    int  pos = snprintf(cmd, sizeof(cmd), "gcc -std=c11 ");

    for (int i = 0; i < src_count; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, "%s ", sources[i]);
    }
    for (int i = 0; i < flag_count; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, "%s ", flags[i]);
    }
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, "-o %s", output_name);

    /* Execute */
    printf("Running: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Compilation failed (exit %d).\n", ret);
        return EXIT_FAILURE;
    }

    printf("Success: ./ %s\n", output_name);
    return EXIT_SUCCESS;
}
