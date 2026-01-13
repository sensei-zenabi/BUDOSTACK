#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("_LIST <directory>\n");
    printf("List files and folders in a directory as a TASK array literal.\n");
    printf("Example: _LIST ./users/\n");
}

static int compare_names(const void *a, const void *b) {
    const char *left = *(const char *const *)a;
    const char *right = *(const char *const *)b;

    return strcmp(left, right);
}

static int print_escaped_string(const char *value) {
    if (value == NULL)
        return -1;

    if (putchar('"') == EOF)
        return -1;

    for (const char *p = value; *p != '\0'; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') {
            if (putchar('\\') == EOF || putchar(c) == EOF)
                return -1;
        } else if (c == '\n') {
            if (fputs("\\n", stdout) == EOF)
                return -1;
        } else if (c == '\r') {
            if (fputs("\\r", stdout) == EOF)
                return -1;
        } else if (c == '\t') {
            if (fputs("\\t", stdout) == EOF)
                return -1;
        } else {
            if (putchar(c) == EOF)
                return -1;
        }
    }

    if (putchar('"') == EOF)
        return -1;

    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "_LIST: usage: _LIST <directory>\n");
        return EXIT_FAILURE;
    }

    const char *dir_path = argv[1];
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("_LIST: opendir");
        return EXIT_FAILURE;
    }

    size_t capacity = 16u;
    size_t count = 0u;
    char **names = malloc(capacity * sizeof(*names));
    if (names == NULL) {
        fprintf(stderr, "_LIST: memory allocation failed\n");
        closedir(dir);
        return EXIT_FAILURE;
    }

    errno = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        if (count == capacity) {
            size_t next_capacity = capacity * 2u;
            char **next = realloc(names, next_capacity * sizeof(*names));
            if (next == NULL) {
                fprintf(stderr, "_LIST: memory allocation failed\n");
                for (size_t i = 0; i < count; ++i)
                    free(names[i]);
                free(names);
                closedir(dir);
                return EXIT_FAILURE;
            }
            names = next;
            capacity = next_capacity;
        }

        char *copy = strdup(name);
        if (copy == NULL) {
            fprintf(stderr, "_LIST: memory allocation failed\n");
            for (size_t i = 0; i < count; ++i)
                free(names[i]);
            free(names);
            closedir(dir);
            return EXIT_FAILURE;
        }
        names[count++] = copy;
    }

    if (errno != 0) {
        perror("_LIST: readdir");
        for (size_t i = 0; i < count; ++i)
            free(names[i]);
        free(names);
        closedir(dir);
        return EXIT_FAILURE;
    }

    if (closedir(dir) != 0) {
        perror("_LIST: closedir");
        for (size_t i = 0; i < count; ++i)
            free(names[i]);
        free(names);
        return EXIT_FAILURE;
    }

    if (count > 1u)
        qsort(names, count, sizeof(*names), compare_names);

    if (putchar('{') == EOF) {
        perror("_LIST: putchar");
        for (size_t i = 0; i < count; ++i)
            free(names[i]);
        free(names);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < count; ++i) {
        if (i > 0u) {
            if (fputs(", ", stdout) == EOF) {
                perror("_LIST: fputs");
                for (size_t j = 0; j < count; ++j)
                    free(names[j]);
                free(names);
                return EXIT_FAILURE;
            }
        }

        if (print_escaped_string(names[i]) != 0) {
            perror("_LIST: output");
            for (size_t j = 0; j < count; ++j)
                free(names[j]);
            free(names);
            return EXIT_FAILURE;
        }
    }

    if (puts("}") == EOF) {
        perror("_LIST: puts");
        for (size_t i = 0; i < count; ++i)
            free(names[i]);
        free(names);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < count; ++i)
        free(names[i]);
    free(names);

    return EXIT_SUCCESS;
}
