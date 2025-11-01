#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_usage(void) {
    printf("Usage: _LOG -state <on|off> -file <path>\n");
    printf("Short form: _LOG -on -mylog.txt\n");
}

static const char *get_base_dir(void) {
    const char *base = getenv("BUDOSTACK_BASE");
    if (base && base[0] != '\0') {
        return base;
    }
    return NULL;
}

static int build_absolute_path(const char *base,
                               const char *input,
                               char *output,
                               size_t size) {
    if (!input || !output || size == 0) {
        return -1;
    }

    if (input[0] == '/') {
        if (snprintf(output, size, "%s", input) >= (int)size) {
            return -1;
        }
        return 0;
    }

    if (!base || base[0] == '\0') {
        if (snprintf(output, size, "%s", input) >= (int)size) {
            return -1;
        }
        return 0;
    }

    size_t base_len = strlen(base);
    const char *fmt = (base_len > 0 && base[base_len - 1] == '/') ? "%s%s" : "%s/%s";
    if (snprintf(output, size, fmt, base, input) >= (int)size) {
        return -1;
    }
    return 0;
}

static int ensure_directory_exists(const char *path) {
    char buffer[PATH_MAX];
    if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int)sizeof(buffer)) {
        return -1;
    }

    char *last_slash = strrchr(buffer, '/');
    if (!last_slash) {
        return 0;
    }

    if (last_slash == buffer) {
        return 0;
    }

    *last_slash = '\0';

    for (char *p = buffer + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (strcmp(buffer, ".") != 0 && strcmp(buffer, "..") != 0) {
                if (mkdir(buffer, 0775) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            *p = '/';
        }
    }

    if (buffer[0] != '\0' && strcmp(buffer, ".") != 0 && strcmp(buffer, "..") != 0) {
        if (mkdir(buffer, 0775) != 0 && errno != EEXIST) {
            return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *state = NULL;
    const char *file_arg = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-help") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        } else if (strcmp(arg, "-state") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_LOG: missing value for -state\n");
                print_usage();
                return EXIT_FAILURE;
            }
            state = argv[++i];
        } else if (strcmp(arg, "-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_LOG: missing value for -file\n");
                print_usage();
                return EXIT_FAILURE;
            }
            file_arg = argv[++i];
        } else if (strcmp(arg, "-on") == 0 || strcmp(arg, "--on") == 0) {
            state = "on";
        } else if (strcmp(arg, "-off") == 0 || strcmp(arg, "--off") == 0) {
            state = "off";
        } else if (arg[0] == '-' && state == NULL) {
            state = arg + 1;
        } else if (arg[0] == '-' && file_arg == NULL) {
            file_arg = arg + 1;
        } else if (state == NULL) {
            state = arg;
        } else if (file_arg == NULL) {
            file_arg = arg;
        } else {
            fprintf(stderr, "_LOG: unexpected argument '%s'\n", arg);
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (!state) {
        fprintf(stderr, "_LOG: missing state (expected on or off)\n");
        print_usage();
        return EXIT_FAILURE;
    }

    int enable = 0;
    if (strcmp(state, "on") == 0 || strcmp(state, "ON") == 0 || strcmp(state, "On") == 0) {
        enable = 1;
    } else if (strcmp(state, "off") == 0 || strcmp(state, "OFF") == 0 || strcmp(state, "Off") == 0) {
        enable = 0;
    } else {
        fprintf(stderr, "_LOG: invalid state '%s' (expected on or off)\n", state);
        return EXIT_FAILURE;
    }

    const char *base = get_base_dir();
    if (!base) {
        fprintf(stderr, "_LOG: BUDOSTACK_BASE is not set\n");
        return EXIT_FAILURE;
    }

    char control_path[PATH_MAX];
    if (snprintf(control_path, sizeof(control_path), "%s/.budostack_log_state", base) >= (int)sizeof(control_path)) {
        fprintf(stderr, "_LOG: control path too long\n");
        return EXIT_FAILURE;
    }

    if (enable) {
        if (!file_arg || file_arg[0] == '\0') {
            fprintf(stderr, "_LOG: missing log file path\n");
            print_usage();
            return EXIT_FAILURE;
        }

        char resolved[PATH_MAX];
        if (build_absolute_path(base, file_arg, resolved, sizeof(resolved)) != 0) {
            fprintf(stderr, "_LOG: failed to resolve log file path\n");
            return EXIT_FAILURE;
        }

        if (ensure_directory_exists(resolved) != 0) {
            fprintf(stderr, "_LOG: failed to create directories for '%s': %s\n", resolved, strerror(errno));
            return EXIT_FAILURE;
        }

        FILE *fp = fopen(resolved, "w");
        if (!fp) {
            fprintf(stderr, "_LOG: unable to open '%s': %s\n", resolved, strerror(errno));
            return EXIT_FAILURE;
        }
        fclose(fp);

        FILE *ctrl = fopen(control_path, "w");
        if (!ctrl) {
            fprintf(stderr, "_LOG: unable to write control file: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        fprintf(ctrl, "%s\n", resolved);
        fclose(ctrl);

        printf("Logging enabled -> %s\n", resolved);
    } else {
        if (unlink(control_path) != 0 && errno != ENOENT) {
            fprintf(stderr, "_LOG: failed to disable logging: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        printf("Logging disabled\n");
    }

    return EXIT_SUCCESS;
}
