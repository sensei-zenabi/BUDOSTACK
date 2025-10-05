/*
SYNTAX: _EXE -[x] -[y]

DESCRIPTION:

  With _EXE command user is able to start executable with offset given by
  [x] in columns and [y] as rows from top left corner. All output from
  executables started with _EXE follow the background color without replacing
  it. I.e. all characters printed will follow the character background cell
  color instead of replacing it.

*/

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "../lib/termbg.h"

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);

    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "_EXE: invalid integer for %s: '%s'\n", name, value);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "_EXE: integer out of range for %s: '%s'\n", name, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static void print_usage(void) {
    fprintf(stderr, "Usage: _EXE -x <col> -y <row> [--] <command> [args...]\n");
}

static const char *get_base_dir(const char *argv0) {
    static char cached[PATH_MAX];
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;

        const char *env = getenv("BUDOSTACK_BASE");
        if (env && env[0] != '\0') {
            if (!realpath(env, cached)) {
                strncpy(cached, env, sizeof(cached) - 1);
                cached[sizeof(cached) - 1] = '\0';
            }
        } else if (argv0 && argv0[0] != '\0') {
            char exe_path[PATH_MAX];
            if (!realpath(argv0, exe_path)) {
                ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
                if (len >= 0) {
                    exe_path[len] = '\0';
                } else {
                    exe_path[0] = '\0';
                }
            }

            if (exe_path[0] != '\0') {
                char *slash = strrchr(exe_path, '/');
                if (slash) {
                    *slash = '\0';
                    slash = strrchr(exe_path, '/');
                    if (slash) {
                        *slash = '\0';
                        strncpy(cached, exe_path, sizeof(cached) - 1);
                        cached[sizeof(cached) - 1] = '\0';
                    }
                }
            }
        }
    }

    return cached[0] ? cached : NULL;
}

static int build_from_base(const char *base, const char *suffix, char *buffer, size_t size) {
    if (!suffix || !*suffix || !buffer || size == 0)
        return -1;

    if (suffix[0] == '/') {
        if (snprintf(buffer, size, "%s", suffix) >= (int)size)
            return -1;
        return 0;
    }

    if (base && base[0] != '\0') {
        size_t len = strlen(base);
        const char *fmt = (len > 0 && base[len - 1] == '/') ? "%s%s" : "%s/%s";
        if (snprintf(buffer, size, fmt, base, suffix) >= (int)size)
            return -1;
    } else {
        if (snprintf(buffer, size, "%s", suffix) >= (int)size)
            return -1;
    }

    return 0;
}

static int resolve_child_path(const char *command, const char *base, char *resolved, size_t size) {
    static const char *search_dirs[] = {"apps", "commands", "utilities"};

    if (!command || !*command || !resolved || size == 0)
        return -1;

    if (strchr(command, '/')) {
        char candidate[PATH_MAX];
        if (build_from_base(base, command, candidate, sizeof(candidate)) != 0)
            return -1;
        if (access(candidate, X_OK) != 0)
            return -1;
        if (!realpath(candidate, resolved)) {
            strncpy(resolved, candidate, size - 1);
            resolved[size - 1] = '\0';
        }
        return 0;
    }

    for (size_t i = 0; i < sizeof(search_dirs) / sizeof(search_dirs[0]); ++i) {
        char suffix[PATH_MAX];
        if (snprintf(suffix, sizeof(suffix), "%s/%s", search_dirs[i], command) >= (int)sizeof(suffix))
            continue;

        char candidate[PATH_MAX];
        if (build_from_base(base, suffix, candidate, sizeof(candidate)) != 0)
            continue;
        if (access(candidate, X_OK) != 0)
            continue;

        if (!realpath(candidate, resolved)) {
            strncpy(resolved, candidate, size - 1);
            resolved[size - 1] = '\0';
        }
        return 0;
    }

    return -1;
}

static void write_char_at(int x, int y, unsigned char ch, int *last_bg) {
    int color;
    if (termbg_get(x, y, &color)) {
        if (*last_bg != color) {
            printf("\033[48;5;%dm", color);
            *last_bg = color;
        }
    } else if (*last_bg != -1) {
        printf("\033[49m");
        *last_bg = -1;
    }

    printf("\033[%d;%dH", y + 1, x + 1);
    fputc(ch, stdout);
}

static void reset_background(int *last_bg) {
    if (*last_bg != -1) {
        printf("\033[49m");
        *last_bg = -1;
    }
}

static int process_output(int fd, int origin_x, int origin_y) {
    char buffer[4096];
    int current_x = 0;
    int current_y = 0;
    int last_bg = -1;

    for (;;) {
        ssize_t nread = read(fd, buffer, sizeof(buffer));
        if (nread > 0) {
            for (ssize_t i = 0; i < nread; ++i) {
                unsigned char ch = (unsigned char)buffer[i];
                switch (ch) {
                    case '\r':
                        current_x = 0;
                        reset_background(&last_bg);
                        break;
                    case '\n':
                        current_x = 0;
                        current_y++;
                        reset_background(&last_bg);
                        break;
                    case '\t': {
                        int spaces = 8 - (current_x % 8);
                        if (spaces == 0)
                            spaces = 8;
                        for (int s = 0; s < spaces; ++s) {
                            write_char_at(origin_x + current_x, origin_y + current_y, ' ', &last_bg);
                            current_x++;
                        }
                        break;
                    }
                    case '\b':
                        if (current_x > 0)
                            current_x--;
                        reset_background(&last_bg);
                        break;
                    default:
                        if (ch < 0x20 && ch != 0x1b) {
                            /* Skip other control characters */
                            break;
                        }
                        write_char_at(origin_x + current_x, origin_y + current_y, ch, &last_bg);
                        current_x++;
                        break;
                }
            }
            fflush(stdout);
        } else if (nread == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            perror("_EXE: read");
            reset_background(&last_bg);
            fflush(stdout);
            return -1;
        }
    }

    reset_background(&last_bg);
    fflush(stdout);
    return 0;
}

int main(int argc, char *argv[]) {
    int x = -1;
    int y = -1;
    int have_x = 0;
    int have_y = 0;
    int command_index = -1;
    int pipefd[2] = {-1, -1};
    pid_t pid = -1;
    int status = 0;
    int exit_code = EXIT_FAILURE;
    int output_error = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            command_index = i + 1;
            break;
        } else if (!have_x && strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_EXE: missing value for -x\n");
                print_usage();
                goto cleanup;
            }
            if (parse_int(argv[i], "-x", &x) != 0)
                goto cleanup;
            have_x = 1;
        } else if (!have_y && strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "_EXE: missing value for -y\n");
                print_usage();
                goto cleanup;
            }
            if (parse_int(argv[i], "-y", &y) != 0)
                goto cleanup;
            have_y = 1;
        } else {
            command_index = i;
            break;
        }
    }

    if (command_index == -1)
        command_index = argc;

    if (!have_x || !have_y || command_index >= argc) {
        fprintf(stderr, "_EXE: missing required arguments\n");
        print_usage();
        goto cleanup;
    }

    if (x < 0 || y < 0) {
        fprintf(stderr, "_EXE: coordinates must be non-negative\n");
        goto cleanup;
    }

    char **child_argv = &argv[command_index];
    const char *base_dir = get_base_dir(argv[0]);
    char resolved_path[PATH_MAX];
    int have_resolved_path = 0;

    if (resolve_child_path(child_argv[0], base_dir, resolved_path, sizeof(resolved_path)) == 0)
        have_resolved_path = 1;

    if (pipe(pipefd) != 0) {
        perror("_EXE: pipe");
        goto cleanup;
    }

    pid = fork();
    if (pid < 0) {
        perror("_EXE: fork");
        goto cleanup;
    }

    if (pid == 0) {
        /* Child process */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
            perror("_EXE: dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        if (have_resolved_path)
            execv(resolved_path, child_argv);
        else
            execvp(child_argv[0], child_argv);
        perror("_EXE: execvp");
        _exit(EXIT_FAILURE);
    }

    close(pipefd[1]);
    pipefd[1] = -1;

    output_error = (process_output(pipefd[0], x, y) != 0);
    close(pipefd[0]);
    pipefd[0] = -1;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        perror("_EXE: waitpid");
        goto cleanup;
    }

    if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exit_code = 128 + WTERMSIG(status);
    else
        exit_code = EXIT_FAILURE;

    if (output_error)
        exit_code = EXIT_FAILURE;

    pid = -1;

cleanup:
    if (pipefd[0] != -1)
        close(pipefd[0]);
    if (pipefd[1] != -1)
        close(pipefd[1]);
    if (pid > 0) {
        /* Ensure the child is reaped if we exited early */
        while (waitpid(pid, NULL, 0) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
    }

    termbg_shutdown();

    return exit_code;
}
