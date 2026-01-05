#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_help(const char *progname) {
    printf("Usage:\n");
    printf("  %s <source.c> <output>\n", progname);
    printf("\nExamples:\n");
    printf("  %s ./games/example.c ./games/example\n", progname);
    printf("\n");
    printf("Builds the given C source into an executable linked against the\n");
    printf("BUDOSTACK libraries in ./lib using gcc.\n");
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

static int validate_output_dir(const char *output_path) {
    if (!output_path || !*output_path) {
        fprintf(stderr, "Error: Output path is empty.\n");
        return -1;
    }

    const char *slash = strrchr(output_path, '/');
    if (!slash) {
        return 0;
    }

    size_t dir_len = (size_t)(slash - output_path);
    if (dir_len == 0 || dir_len >= PATH_MAX) {
        fprintf(stderr, "Error: Output directory path is invalid.\n");
        return -1;
    }

    char dir_path[PATH_MAX];
    memcpy(dir_path, output_path, dir_len);
    dir_path[dir_len] = '\0';

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        fprintf(stderr, "Error: Output directory '%s' is unavailable: %s\n", dir_path, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: Output path '%s' is not a directory.\n", dir_path);
        return -1;
    }
    return 0;
}

static int gather_lib_sources(const char *repo_root, glob_t *out_glob) {
    if (!repo_root || !out_glob) {
        return -1;
    }

    char pattern[PATH_MAX];
    if (snprintf(pattern, sizeof(pattern), "%s/lib/*.c", repo_root) >= (int)sizeof(pattern)) {
        fprintf(stderr, "Error: Library path is too long.\n");
        return -1;
    }

    int rc = glob(pattern, 0, NULL, out_glob);
    if (rc == GLOB_NOMATCH) {
        fprintf(stderr, "Error: No library sources found in '%s/lib'.\n", repo_root);
        return -1;
    }
    if (rc != 0) {
        fprintf(stderr, "Error: Failed to enumerate library sources.\n");
        return -1;
    }
    return 0;
}

static int run_compiler(const char *input_path, const char *output_path, const glob_t *libs) {
    size_t base_args = 9;
    size_t extra_args = 2;
    size_t total = base_args + (libs ? libs->gl_pathc : 0) + extra_args + 1;

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
    args[idx++] = "-o";
    args[idx++] = (char *)output_path;
    args[idx++] = (char *)input_path;

    if (libs) {
        for (size_t i = 0; i < libs->gl_pathc; ++i) {
            args[idx++] = libs->gl_pathv[i];
        }
    }

    args[idx++] = "-lm";
    args[idx++] = "-pthread";
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

    if (WIFSIGNALED(status)) {
        fprintf(stderr, "gcc terminated with signal %d\n", WTERMSIG(status));
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "gcc failed with status %d\n", WEXITSTATUS(status));
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: Missing source or output path.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    if (access(argv[1], R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read source '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    if (validate_output_dir(argv[2]) != 0) {
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (get_repo_root(repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "Error: Failed to determine repository root.\n");
        return EXIT_FAILURE;
    }

    glob_t libs;
    memset(&libs, 0, sizeof(libs));
    if (gather_lib_sources(repo_root, &libs) != 0) {
        return EXIT_FAILURE;
    }

    if (run_compiler(argv[1], argv[2], &libs) != 0) {
        globfree(&libs);
        return EXIT_FAILURE;
    }

    globfree(&libs);

    printf("Built executable '%s' from %s\n", argv[2], argv[1]);
    return EXIT_SUCCESS;
}
