#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} path_list_t;

static void print_help(const char *progname) {
    printf("Usage:\n");
    printf("  %s <source.c> <output>\n", progname);
    printf("\nExample:\n");
    printf("  %s ./example.c ./example\n", progname);
    printf("\n");
    printf("Builds a C executable from the given source file and links\n");
    printf("all .c files found under the BUDOSTACK ./budo/ directory.\n");
    printf("This allows applications to include libraries from ./budo/\n");
    printf("without maintaining separate makefiles.\n");
}

static int has_suffix(const char *name, const char *suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > name_len) {
        return 0;
    }
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void free_path_list(path_list_t *list) {
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

static int push_path(path_list_t *list, const char *path) {
    if (!list || !path) {
        return -1;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        char **new_items = realloc(list->items, new_capacity * sizeof(char *));
        if (!new_items) {
            perror("realloc");
            return -1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    char *copy = strdup(path);
    if (!copy) {
        perror("strdup");
        return -1;
    }
    list->items[list->count++] = copy;
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

static path_list_t *g_collect_list = NULL;

static int collect_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;
    if (typeflag == FTW_F && has_suffix(fpath, ".c")) {
        if (!g_collect_list) {
            return 1;
        }
        if (push_path(g_collect_list, fpath) != 0) {
            return 1;
        }
    }
    return 0;
}

static int collect_budo_sources(const char *repo_root, path_list_t *list) {
    if (!repo_root || !list) {
        return -1;
    }
    char budo_path[PATH_MAX];
    if (snprintf(budo_path, sizeof(budo_path), "%s/budo", repo_root) >= (int)sizeof(budo_path)) {
        fprintf(stderr, "Error: BUDOSTACK path too long.\n");
        return -1;
    }
    if (access(budo_path, R_OK | X_OK) != 0) {
        fprintf(stderr, "Error: Cannot access budo directory at '%s'.\n", budo_path);
        return -1;
    }

    g_collect_list = list;
    int rc = nftw(budo_path, collect_callback, 16, FTW_PHYS);
    g_collect_list = NULL;
    if (rc != 0) {
        fprintf(stderr, "Error: Failed to scan budo sources under '%s'.\n", budo_path);
        return -1;
    }
    return 0;
}

static int run_compiler(const char *input_path,
                        const char *output_path,
                        const char *repo_root,
                        const path_list_t *sources) {
    if (!input_path || !output_path || !repo_root || !sources) {
        return -1;
    }
    if (sources->count == 0) {
        fprintf(stderr, "Error: No budo sources found to compile.\n");
        return -1;
    }

    char include_path[PATH_MAX];
    if (snprintf(include_path, sizeof(include_path), "-I%s/budo", repo_root) >= (int)sizeof(include_path)) {
        fprintf(stderr, "Error: Include path too long.\n");
        return -1;
    }

    size_t base_args = 12;
    size_t total = base_args + sources->count + 1;
    char **args = calloc(total, sizeof(char *));
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
    args[idx++] = "-pthread";
    args[idx++] = include_path;
    args[idx++] = "-o";
    args[idx++] = (char *)output_path;
    args[idx++] = (char *)input_path;
    for (size_t i = 0; i < sources->count; ++i) {
        args[idx++] = sources->items[i];
    }
    args[idx++] = "-lm";
    args[idx] = NULL;

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
        fprintf(stderr, "Error: Missing source file or output path.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    char input_path[PATH_MAX];
    if (!realpath(argv[1], input_path)) {
        fprintf(stderr, "Error: Could not resolve source '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    if (access(input_path, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read source '%s'.\n", input_path);
        return EXIT_FAILURE;
    }

    if (!argv[2] || argv[2][0] == '\0') {
        fprintf(stderr, "Error: Output path is required.\n");
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (get_repo_root(repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "Error: Failed to determine repository root.\n");
        return EXIT_FAILURE;
    }

    path_list_t sources = {0};
    if (collect_budo_sources(repo_root, &sources) != 0) {
        free_path_list(&sources);
        return EXIT_FAILURE;
    }

    if (run_compiler(input_path, argv[2], repo_root, &sources) != 0) {
        free_path_list(&sources);
        return EXIT_FAILURE;
    }

    free_path_list(&sources);

    printf("Built executable '%s' from %s using budo libraries.\n", argv[2], input_path);
    return EXIT_SUCCESS;
}
