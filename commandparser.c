#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700  /* For realpath() */

#include "commandparser.h"
#include <limits.h>    /* For PATH_MAX */
#include <stdlib.h>    /* For malloc, free, realpath, exit */
#include <unistd.h>    /* For access, fork */
#include <string.h>    /* For strlen, strncpy, strtok_r, memcpy, strcmp */
#include <stdio.h>     /* For fprintf, perror */
#include <sys/wait.h>  /* For waitpid */
#include <errno.h>     /* For errno */
#include <glob.h>      /* For glob() */
#include <signal.h>    /* For signal() */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Simple strdup replacement for C11 */
static char *my_strdup(const char *s) {
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup)
        memcpy(dup, s, len + 1);
    return dup;
}

#ifndef strdup
#define strdup(s) my_strdup(s)
#endif

/* Base path for locating commands */
static char base_path[PATH_MAX] = "";

/* Set the directory where executables are looked up */
void set_base_path(const char *path) {
    if (path) {
        strncpy(base_path, path, PATH_MAX - 1);
        base_path[PATH_MAX - 1] = '\0';
    }
}

/* Commands that should bypass wildcard expansion */
static const char *bypass_expansion_commands[] = {
    "list",
    NULL
};

/* Should this command’s parameters NOT be glob-expanded? */
static int should_bypass_expansion(const char *command) {
    for (int i = 0; bypass_expansion_commands[i] != NULL; i++) {
        if (strcmp(command, bypass_expansion_commands[i]) == 0)
            return 1;
    }
    return 0;
}

/* Does this string contain any shell-style wildcards? */
static int contains_wildcard(const char *str) {
    return (strchr(str, '*') || strchr(str, '?') || strchr(str, '[')) ? 1 : 0;
}

/*
 * Tokenize and parse the input line into a CommandStruct.
 * Flags (tokens starting with '-') and their immediately following values
 * are stored in cmd->options[]; all remaining tokens are parameters,
 * with optional globbing.
 */
void parse_input(const char *input, CommandStruct *cmd) {
    char buffer[INPUT_SIZE];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(buffer, " ", &saveptr);
    if (!token) return;

    /* The first token is the command name */
    strncpy(cmd->command, token, sizeof(cmd->command) - 1);
    cmd->command[sizeof(cmd->command) - 1] = '\0';

    cmd->param_count = 0;
    cmd->opt_count   = 0;

    /* Process the rest of the tokens */
    while ((token = strtok_r(NULL, " ", &saveptr)) != NULL) {
        if (token[0] == '-' && token[1] != '\0') {
            /* Store the flag itself */
            if (cmd->opt_count < MAX_OPTIONS) {
                char *dup = strdup(token);
                if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                cmd->options[cmd->opt_count++] = dup;
            }
            /* Peek for a value */
            char *value = strtok_r(NULL, " ", &saveptr);
            if (value && value[0] != '-') {
                /* Value for the flag */
                if (cmd->opt_count < MAX_OPTIONS) {
                    char *dup = strdup(value);
                    if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                    cmd->options[cmd->opt_count++] = dup;
                }
            } else if (value && value[0] == '-') {
                /*
                 * It was actually another flag—store it
                 * (will check for its value in the next loop iteration)
                 */
                if (cmd->opt_count < MAX_OPTIONS) {
                    char *dup = strdup(value);
                    if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                    cmd->options[cmd->opt_count++] = dup;
                }
            }
            /* If value was NULL, nothing more to do */
        } else {
            /* Positional parameter: maybe glob-expand it */
            if (should_bypass_expansion(cmd->command)) {
                if (cmd->param_count < MAX_PARAMETERS) {
                    char *dup = strdup(token);
                    if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                    cmd->parameters[cmd->param_count++] = dup;
                }
            } else if (contains_wildcard(token)) {
                glob_t glob_result;
                int ret = glob(token, GLOB_NOCHECK, NULL, &glob_result);
                if (ret == 0 || ret == GLOB_NOMATCH) {
                    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                        if (cmd->param_count < MAX_PARAMETERS) {
                            char *dup = strdup(glob_result.gl_pathv[i]);
                            if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                            cmd->parameters[cmd->param_count++] = dup;
                        }
                    }
                } else {
                    /* On glob error, use the literal token */
                    if (cmd->param_count < MAX_PARAMETERS) {
                        char *dup = strdup(token);
                        if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                        cmd->parameters[cmd->param_count++] = dup;
                    }
                }
                globfree(&glob_result);
            } else {
                /* Plain parameter */
                if (cmd->param_count < MAX_PARAMETERS) {
                    char *dup = strdup(token);
                    if (!dup) { perror("strdup failed"); exit(EXIT_FAILURE); }
                    cmd->parameters[cmd->param_count++] = dup;
                }
            }
        }
    }
}

/*
 * Locate the executable under base_path, then execv() it,
 * passing all flags (and their values) first, then positional parameters.
 */
int execute_command(const CommandStruct *cmd) {
    char command_path[PATH_MAX];
    int found = 0;

    static const char *relative_commands_dirs[] = {
        "commands",
        "apps",
        "utilities"
    };
    int num_dirs = sizeof(relative_commands_dirs) / sizeof(relative_commands_dirs[0]);

    /* Search for the executable */
    for (int i = 0; i < num_dirs; i++) {
        int n;
        if (base_path[0] != '\0') {
            n = snprintf(command_path, sizeof(command_path),
                         "%s/%s/%s",
                         base_path, relative_commands_dirs[i], cmd->command);
        } else {
            n = snprintf(command_path, sizeof(command_path),
                         "./%s/%s",
                         relative_commands_dirs[i], cmd->command);
        }
        if (n < 0 || n >= (int)sizeof(command_path))
            continue;
        if (access(command_path, X_OK) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        //fprintf(stderr, "%s was ran as shell command...\n", cmd->command);
        return -1;
    }

    /* Resolve to absolute path */
    char abs_path[PATH_MAX];
    if (!realpath(command_path, abs_path)) {
        perror("realpath failed");
        return -1;
    }

    /* Build argv: flags+values first, then parameters */
    int total_args = 1 + cmd->opt_count + cmd->param_count;
    char *args[total_args + 1];
    args[0] = abs_path;
    int idx = 1;
    for (int i = 0; i < cmd->opt_count; i++)
        args[idx++] = cmd->options[i];
    for (int i = 0; i < cmd->param_count; i++)
        args[idx++] = cmd->parameters[i];
    args[idx] = NULL;

    /* Fork & exec */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execv(abs_path, args);
        perror("execv failed");
        exit(EXIT_FAILURE);
    } else {
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid failed");
            return -1;
        }
    }
    return 0;
}

/* Free all strdup’d memory in a CommandStruct */
void free_command_struct(CommandStruct *cmd) {
    for (int i = 0; i < cmd->param_count; i++)
        free(cmd->parameters[i]);
    for (int i = 0; i < cmd->opt_count; i++)
        free(cmd->options[i]);
}
