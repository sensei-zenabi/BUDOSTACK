#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringList;

static void print_help(const char *progname) {
    printf("Usage:\n");
    printf("  %s <source.c> <output> [extra gcc args...]\n", progname);
    printf("\nExample:\n");
    printf("  %s ./example.c ./example\n", progname);
    printf("\n");
    printf("Builds a standalone executable from the given C source while\n");
    printf("automatically linking all C files in the BUDOSTACK ./budo/ folder.\n");
    printf("The BUDOSTACK root is detected from the compile binary location.\n");
}

static int string_list_append(StringList *list, const char *value) {
    if (!list || !value) {
        return -1;
    }
    if (list->count == list->capacity) {
        size_t next_capacity = list->capacity ? list->capacity * 2 : 8;
        char **next_items = realloc(list->items, next_capacity * sizeof(char *));
        if (!next_items) {
            perror("realloc");
            return -1;
        }
        list->items = next_items;
        list->capacity = next_capacity;
    }
    list->items[list->count] = strdup(value);
    if (!list->items[list->count]) {
        perror("strdup");
        return -1;
    }
    list->count += 1;
    return 0;
}

static void string_list_free(StringList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int has_extension(const char *name, const char *ext) {
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    if (name_len < ext_len) {
        return 0;
    }
    return strcmp(name + name_len - ext_len, ext) == 0;
}

static int collect_budo_sources(const char *dir_path,
                                StringList *sources,
                                const char *exclude_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("opendir");
        return -1;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(path)) {
            fprintf(stderr, "Error: Path too long for '%s/%s'.\n", dir_path, entry->d_name);
            closedir(dir);
            return -1;
        }

        struct stat st;
        if (lstat(path, &st) != 0) {
            perror("lstat");
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (collect_budo_sources(path, sources, exclude_path) != 0) {
                closedir(dir);
                return -1;
            }
        } else if (S_ISREG(st.st_mode) && has_extension(entry->d_name, ".c")) {
            char resolved[PATH_MAX];
            if (!realpath(path, resolved)) {
                perror("realpath");
                closedir(dir);
                return -1;
            }
            if (exclude_path && strcmp(resolved, exclude_path) == 0) {
                continue;
            }
            if (string_list_append(sources, resolved) != 0) {
                closedir(dir);
                return -1;
            }
        }
    }

    if (closedir(dir) != 0) {
        perror("closedir");
        return -1;
    }

    return 0;
}

static int get_repo_root(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        perror("readlink");
        return -1;
    }
    exe_path[len] = '\0';

    char *slash = strrchr(exe_path, '/');
    if (!slash) {
        fprintf(stderr, "Error: unexpected executable path '%s'\n", exe_path);
        return -1;
    }
    *slash = '\0';

    slash = strrchr(exe_path, '/');
    if (!slash) {
        fprintf(stderr, "Error: could not determine repository root from '%s'\n", exe_path);
        return -1;
    }
    *slash = '\0';

    if (snprintf(buffer, size, "%s", exe_path) >= (int)size) {
        return -1;
    }
    return 0;
}

static int run_compiler(const char *source_path,
                        const char *output_path,
                        const char *budo_dir,
                        const StringList *budo_sources,
                        int extra_argc,
                        char *extra_argv[]) {
    size_t args_needed = 12 + (budo_sources ? budo_sources->count : 0) + (size_t)extra_argc;
    char **args = calloc(args_needed, sizeof(char *));
    if (!args) {
        perror("calloc");
        return -1;
    }

    size_t idx = 0;
    args[idx++] = "gcc";
    args[idx++] = "-std=c11";
    args[idx++] = "-Wall";
    args[idx++] = "-Wextra";
    args[idx++] = "-Werror";
    args[idx++] = "-Wpedantic";
    args[idx++] = "-I";
    args[idx++] = (char *)budo_dir;
    args[idx++] = "-o";
    args[idx++] = (char *)output_path;
    args[idx++] = (char *)source_path;

    if (budo_sources) {
        for (size_t i = 0; i < budo_sources->count; ++i) {
            args[idx++] = budo_sources->items[i];
        }
    }

    for (int i = 0; i < extra_argc; ++i) {
        args[idx++] = extra_argv[i];
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        free(args);
        return -1;
    }

    if (pid == 0) {
        execvp("gcc", args);
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        free(args);
        return -1;
    }

    free(args);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "gcc failed with status %d\n", status);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: Source and output paths are required.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    const char *source_path = argv[1];
    const char *output_path = argv[2];

    if (access(source_path, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read source '%s': %s\n", source_path, strerror(errno));
        return EXIT_FAILURE;
    }

    char resolved_source[PATH_MAX];
    if (!realpath(source_path, resolved_source)) {
        fprintf(stderr, "Error: Could not resolve source '%s': %s\n", source_path, strerror(errno));
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (get_repo_root(repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "Error: Failed to determine repository root.\n");
        return EXIT_FAILURE;
    }

    char budo_dir[PATH_MAX];
    if (snprintf(budo_dir, sizeof(budo_dir), "%s/budo", repo_root) >= (int)sizeof(budo_dir)) {
        fprintf(stderr, "Error: BUDOSTACK path too long.\n");
        return EXIT_FAILURE;
    }

    struct stat st;
    if (stat(budo_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: Missing BUDOSTACK budo directory at '%s'.\n", budo_dir);
        return EXIT_FAILURE;
    }

    StringList budo_sources = {0};
    if (collect_budo_sources(budo_dir, &budo_sources, resolved_source) != 0) {
        string_list_free(&budo_sources);
        return EXIT_FAILURE;
    }

    int extra_argc = argc - 3;
    char **extra_argv = argc > 3 ? &argv[3] : NULL;

    if (run_compiler(source_path, output_path, budo_dir, &budo_sources, extra_argc, extra_argv) != 0) {
        string_list_free(&budo_sources);
        return EXIT_FAILURE;
    }

    string_list_free(&budo_sources);

    printf("Built executable '%s' using BUDOSTACK libraries from %s.\n", output_path, budo_dir);
    return EXIT_SUCCESS;
}
