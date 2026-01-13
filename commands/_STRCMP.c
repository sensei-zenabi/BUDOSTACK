#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "Usage: _STRCMP <string1> <string2> [-cs] [-fw]\n");
    fprintf(stream, "-cs = <optional> case sensitive\n");
    fprintf(stream, "-fw = <optional> full word match\n");
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

static int parse_options(int argc, char *argv[], int *case_sensitive, int *full_word) {
    *case_sensitive = 0;
    *full_word = 0;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-cs") == 0) {
            *case_sensitive = 1;
        } else if (strcmp(argv[i], "-fw") == 0) {
            *full_word = 1;
        } else {
            fprintf(stderr, "_STRCMP: unknown option '%s'\n", argv[i]);
            print_usage(stderr);
            return -1;
        }
    }

    return 0;
}

static int match_full_word(const char *input, const char *pattern, int case_sensitive) {
    if (case_sensitive) {
        return strcmp(input, pattern) == 0 ? 0 : FNM_NOMATCH;
    }

    char *lower_input = NULL;
    char *lower_pattern = NULL;

    if (to_lower_copy(input, &lower_input) != 0) {
        free(lower_input);
        return -1;
    }

    if (to_lower_copy(pattern, &lower_pattern) != 0) {
        free(lower_pattern);
        free(lower_input);
        return -1;
    }

    int result = strcmp(lower_input, lower_pattern) == 0 ? 0 : FNM_NOMATCH;
    free(lower_pattern);
    free(lower_input);
    return result;
}

static int match_pattern(const char *input, const char *pattern, int case_sensitive) {
    if (case_sensitive) {
        return fnmatch(pattern, input, 0);
    }

    char *lower_input = NULL;
    char *lower_pattern = NULL;

    if (to_lower_copy(input, &lower_input) != 0) {
        free(lower_input);
        return -1;
    }

    if (to_lower_copy(pattern, &lower_pattern) != 0) {
        free(lower_pattern);
        free(lower_input);
        return -1;
    }

    int result = fnmatch(lower_pattern, lower_input, 0);
    free(lower_pattern);
    free(lower_input);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 5) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    int case_sensitive = 0;
    int full_word = 0;

    if (parse_options(argc, argv, &case_sensitive, &full_word) != 0) {
        return EXIT_FAILURE;
    }

    const char *input = argv[1];
    const char *pattern = argv[2];
    int match_result = 0;

    if (full_word) {
        match_result = match_full_word(input, pattern, case_sensitive);
    } else {
        match_result = match_pattern(input, pattern, case_sensitive);
    }

    if (match_result != 0 && match_result != FNM_NOMATCH) {
        fprintf(stderr, "_STRCMP: match failed\n");
        return EXIT_FAILURE;
    }

    if (printf("%d\n", match_result == 0 ? 1 : 0) < 0) {
        perror("_STRCMP: printf");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
