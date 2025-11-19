#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "budostack_paths.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int budostack_compute_root_directory(const char *argv0, char *out_path, size_t out_size) {
    if (!argv0 || !out_path || out_size == 0u) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            return -1;
        }
        size_t len = strlen(cwd);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, cwd, len + 1u);
        return 0;
    }

    char *last_sep = strrchr(resolved, '/');
    if (!last_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *last_sep = '\0';

    char *apps_sep = strrchr(resolved, '/');
    if (!apps_sep) {
        size_t len = strlen(resolved);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, resolved, len + 1u);
        return 0;
    }
    *apps_sep = '\0';

    size_t len = strlen(resolved);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1u);
    return 0;
}

int budostack_build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    int written = snprintf(dest, dest_size, "%s/%s", base, suffix);
    if (written < 0 || (size_t)written >= dest_size) {
        return -1;
    }
    return 0;
}

int budostack_resolve_resource_path(const char *root_dir,
                                    const char *argument,
                                    char *out_path,
                                    size_t out_size) {
    if (!argument || !out_path || out_size == 0u) {
        return -1;
    }

    if (argument[0] == '/') {
        size_t len = strlen(argument);
        if (len >= out_size) {
            return -1;
        }
        memcpy(out_path, argument, len + 1u);
        return 0;
    }

    if (root_dir) {
        if (budostack_build_path(out_path, out_size, root_dir, argument) == 0 && access(out_path, R_OK) == 0) {
            return 0;
        }
    }

    size_t len = strlen(argument);
    if (len >= out_size) {
        return -1;
    }
    memcpy(out_path, argument, len + 1u);
    return 0;
}
