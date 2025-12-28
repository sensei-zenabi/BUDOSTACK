#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct StringList {
    char **items;
    size_t count;
    size_t capacity;
};

static struct StringList output_files;
static char root_path[PATH_MAX];

static void add_output_file(const char *path)
{
    size_t i;
    char *copy;

    for (i = 0; i < output_files.count; i++) {
        if (strcmp(output_files.items[i], path) == 0) {
            return;
        }
    }

    if (output_files.count == output_files.capacity) {
        size_t new_capacity = output_files.capacity == 0 ? 32 : output_files.capacity * 2;
        char **new_items = realloc(output_files.items, new_capacity * sizeof(*new_items));

        if (new_items == NULL) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }

        output_files.items = new_items;
        output_files.capacity = new_capacity;
    }

    copy = strdup(path);
    if (copy == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    output_files.items[output_files.count++] = copy;
}

static void add_output_from_make(const char *output)
{
    const char *start = output;
    char full_path[PATH_MAX];
    int written;

    while (start[0] == '.' && start[1] == '/') {
        start += 2;
    }

    if (start[0] == '/') {
        add_output_file(start);
        return;
    }

    written = snprintf(full_path, sizeof(full_path), "%s/%s", root_path, start);
    if (written < 0 || (size_t)written >= sizeof(full_path)) {
        fprintf(stderr, "Output path too long: %s/%s\n", root_path, start);
        return;
    }

    add_output_file(full_path);
}

static void parse_make_outputs(void)
{
    FILE *pipe;
    char *line = NULL;
    size_t length = 0;

    pipe = popen("make -n", "r");
    if (pipe == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    while (getline(&line, &length, pipe) != -1) {
        char *saveptr = NULL;
        char *token = strtok_r(line, " \t\n", &saveptr);

        while (token != NULL) {
            if (strcmp(token, "-o") == 0) {
                char *output = strtok_r(NULL, " \t\n", &saveptr);

                if (output != NULL) {
                    add_output_from_make(output);
                }
            }

            token = strtok_r(NULL, " \t\n", &saveptr);
        }
    }

    free(line);

    if (pclose(pipe) == -1) {
        perror("pclose");
        exit(EXIT_FAILURE);
    }
}

static int is_output_file(const char *path)
{
    size_t i;

    for (i = 0; i < output_files.count; i++) {
        if (strcmp(output_files.items[i], path) == 0) {
            return 1;
        }
    }

    return 0;
}

static int visit_path(const char *path, const struct stat *info, int typeflag, struct FTW *ftwbuf)
{
    (void)info;
    (void)ftwbuf;

    if (strstr(path, "/.git/") != NULL) {
        return 0;
    }

    if (strlen(path) >= 5 && strcmp(path + strlen(path) - 5, "/.git") == 0) {
        return 0;
    }

    if (typeflag != FTW_F) {
        return 0;
    }

    if (!is_output_file(path)) {
        printf("%s\n", path);
    }

    return 0;
}

static void cleanup_outputs(void)
{
    size_t i;

    for (i = 0; i < output_files.count; i++) {
        free(output_files.items[i]);
    }

    free(output_files.items);
    output_files.items = NULL;
    output_files.count = 0;
    output_files.capacity = 0;
}

int main(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        fprintf(stderr, "usage: garbage\n");
        return EXIT_FAILURE;
    }

    if (getcwd(root_path, sizeof(root_path)) == NULL) {
        perror("getcwd");
        return EXIT_FAILURE;
    }

    parse_make_outputs();

    if (nftw(root_path, visit_path, 32, FTW_PHYS) != 0) {
        perror("nftw");
        cleanup_outputs();
        return EXIT_FAILURE;
    }

    cleanup_outputs();
    return EXIT_SUCCESS;
}
