#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "Usage: _STRCMP <string1> <string2> -cs\n");
    fprintf(stream, "-cs = <optional> case sensitive\n");
}

static int to_lower_copy(const char *input, char **output) {
    if (input == NULL || output == NULL) {
        return -1;
    }

    size_t len = strlen(input);
    char *buffer = malloc(len + 1U);
    if (buffer == NULL) {
        perror("_STRCMP: malloc");
        return -1;
    }

    for (size_t i = 0; i < len; ++i) {
        buffer[i] = (char)tolower((unsigned char)input[i]);
    }
    buffer[len] = '\0';

    *output = buffer;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3 && argc != 4) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    int case_sensitive = 0;
    if (argc == 4) {
        if (strcmp(argv[3], "-cs") != 0) {
            fprintf(stderr, "_STRCMP: unknown option '%s'\n", argv[3]);
            print_usage(stderr);
            return EXIT_FAILURE;
        }
        case_sensitive = 1;
    }

    const char *input = argv[1];
    const char *pattern = argv[2];
    int match_result = 0;

    if (case_sensitive) {
        match_result = fnmatch(pattern, input, 0);
    } else {
        char *lower_input = NULL;
        char *lower_pattern = NULL;

        if (to_lower_copy(input, &lower_input) != 0) {
            free(lower_pattern);
            free(lower_input);
            return EXIT_FAILURE;
        }

        if (to_lower_copy(pattern, &lower_pattern) != 0) {
            free(lower_pattern);
            free(lower_input);
            return EXIT_FAILURE;
        }

        match_result = fnmatch(lower_pattern, lower_input, 0);
        free(lower_pattern);
        free(lower_input);
    }

    if (match_result != 0 && match_result != FNM_NOMATCH) {
        fprintf(stderr, "_STRCMP: fnmatch failed\n");
        return EXIT_FAILURE;
    }

    if (printf("%d\n", match_result == 0 ? 1 : 0) < 0) {
        perror("_STRCMP: printf");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
