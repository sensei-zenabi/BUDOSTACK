#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_BUFFER_SIZE 1024

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CONFIG_FILENAME "config.ini"

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  _CONFIG -read <key>\n"
            "  _CONFIG -write <key> <value>\n");
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
            char resolved[PATH_MAX];
            if (!realpath(argv0, resolved)) {
                ssize_t len = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
                if (len >= 0) {
                    resolved[len] = '\0';
                } else {
                    resolved[0] = '\0';
                }
            }

            if (resolved[0] != '\0') {
                strncpy(cached, resolved, sizeof(cached) - 1);
                cached[sizeof(cached) - 1] = '\0';
                char *slash = strrchr(cached, '/');
                if (slash) {
                    *slash = '\0';
                    slash = strrchr(cached, '/');
                    if (slash) {
                        *slash = '\0';
                    }
                }
            }
        }
    }

    return cached[0] ? cached : NULL;
}

static const char *config_path(const char *argv0, char *buffer, size_t size) {
    const char *base = get_base_dir(argv0);

    if (base && base[0] != '\0') {
        size_t len = strlen(base);
        const char *fmt = (len > 0 && base[len - 1] == '/') ? "%s%s" : "%s/%s";
        if (snprintf(buffer, size, fmt, base, CONFIG_FILENAME) >= (int)size)
            return NULL;
        return buffer;
    }

    if (snprintf(buffer, size, "%s", CONFIG_FILENAME) >= (int)size)
        return NULL;

    return buffer;
}

static const char *skip_leading_space(const char *s) {
    while (*s != '\0' && isspace((unsigned char)*s))
        ++s;
    return s;
}

static const char *trim_trailing_space(const char *start, const char *end) {
    while (end > start && isspace((unsigned char)end[-1]))
        --end;
    return end;
}

static int match_key_line(const char *line, const char *key, const char **value_start, const char **value_end) {
    const char *cursor = skip_leading_space(line);
    const char *equals = NULL;
    const char *key_end = NULL;

    if (*cursor == '\0' || *cursor == '\n' || *cursor == '\r' || *cursor == '#' || *cursor == ';' || *cursor == '[')
        return 0;

    equals = strchr(cursor, '=');
    if (equals == NULL)
        return 0;

    key_end = trim_trailing_space(cursor, equals);

    size_t key_len = strlen(key);
    if ((size_t)(key_end - cursor) != key_len)
        return 0;

    if (strncmp(cursor, key, key_len) != 0)
        return 0;

    cursor = equals + 1;
    cursor = skip_leading_space(cursor);
    if (value_start)
        *value_start = cursor;
    if (value_end) {
        const char *line_end = line + strlen(line);
        line_end = trim_trailing_space(cursor, line_end);
        *value_end = line_end;
    }

    return 1;
}

static int read_value(const char *argv0, const char *key) {
    char path[PATH_MAX];
    const char *config = config_path(argv0, path, sizeof(path));

    if (!config) {
        fprintf(stderr, "_CONFIG: failed to build config path.\n");
        return EXIT_FAILURE;
    }

    FILE *file = fopen(config, "r");
    if (file == NULL) {
        perror("_CONFIG: fopen");
        return EXIT_FAILURE;
    }

    char line[LINE_BUFFER_SIZE];
    int found = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        const char *value_start = NULL;
        const char *value_end = NULL;

        if (match_key_line(line, key, &value_start, &value_end)) {
            size_t value_len = (size_t)(value_end - value_start);
            printf("%.*s\n", (int)value_len, value_start);
            found = 1;
            break;
        }
    }

    if (fclose(file) != 0) {
        perror("_CONFIG: fclose");
        return EXIT_FAILURE;
    }

    if (!found) {
        fprintf(stderr, "_CONFIG: key '%s' not found in %s.\n", key, config);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static char *join_value(int argc, char *argv[], int start_index) {
    size_t total = 1;
    for (int i = start_index; i < argc; ++i)
        total += strlen(argv[i]) + 1;

    char *value = malloc(total);
    if (value == NULL)
        return NULL;

    value[0] = '\0';
    for (int i = start_index; i < argc; ++i) {
        if (i > start_index)
            strncat(value, " ", total - strlen(value) - 1);
        strncat(value, argv[i], total - strlen(value) - 1);
    }

    return value;
}

static int write_value(const char *argv0, const char *key, const char *value) {
    char path[PATH_MAX];
    const char *config = config_path(argv0, path, sizeof(path));

    if (!config) {
        fprintf(stderr, "_CONFIG: failed to build config path.\n");
        return EXIT_FAILURE;
    }

    FILE *input = fopen(config, "r");
    if (input == NULL) {
        perror("_CONFIG: fopen");
        return EXIT_FAILURE;
    }

    char tmp_path[PATH_MAX];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", config) >= (int)sizeof(tmp_path)) {
        fprintf(stderr, "_CONFIG: failed to build temp path.\n");
        fclose(input);
        return EXIT_FAILURE;
    }

    FILE *output = fopen(tmp_path, "w");
    if (output == NULL) {
        perror("_CONFIG: fopen");
        fclose(input);
        return EXIT_FAILURE;
    }

    char line[LINE_BUFFER_SIZE];
    int found = 0;
    int last_line_has_newline = 1;

    while (fgets(line, sizeof(line), input) != NULL) {
        last_line_has_newline = (strchr(line, '\n') != NULL);
        if (match_key_line(line, key, NULL, NULL)) {
            if (fprintf(output, "%s=%s\n", key, value) < 0) {
                perror("_CONFIG: fprintf");
                fclose(input);
                fclose(output);
                return EXIT_FAILURE;
            }
            found = 1;
            continue;
        }
        if (fputs(line, output) == EOF) {
            perror("_CONFIG: fputs");
            fclose(input);
            fclose(output);
            return EXIT_FAILURE;
        }
    }

    if (ferror(input)) {
        perror("_CONFIG: fgets");
        fclose(input);
        fclose(output);
        return EXIT_FAILURE;
    }

    if (!found) {
        if (!last_line_has_newline) {
            if (fputc('\n', output) == EOF) {
                perror("_CONFIG: fputc");
                fclose(input);
                fclose(output);
                return EXIT_FAILURE;
            }
        }
        if (fprintf(output, "%s=%s\n", key, value) < 0) {
            perror("_CONFIG: fprintf");
            fclose(input);
            fclose(output);
            return EXIT_FAILURE;
        }
    }

    if (fclose(input) != 0) {
        perror("_CONFIG: fclose");
        fclose(output);
        return EXIT_FAILURE;
    }

    if (fclose(output) != 0) {
        perror("_CONFIG: fclose");
        return EXIT_FAILURE;
    }

    if (rename(tmp_path, config) != 0) {
        perror("_CONFIG: rename");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-read") == 0) {
        if (argc != 3) {
            usage();
            return EXIT_FAILURE;
        }
        return read_value(argv[0], argv[2]);
    }

    if (strcmp(argv[1], "-write") == 0) {
        if (argc < 4) {
            usage();
            return EXIT_FAILURE;
        }
        char *value = join_value(argc, argv, 3);
        if (value == NULL) {
            perror("_CONFIG: malloc");
            return EXIT_FAILURE;
        }
        int result = write_value(argv[0], argv[2], value);
        free(value);
        return result;
    }

    usage();
    return EXIT_FAILURE;
}
