#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_DIR_SUFFIX "/.budostack/sdl"
#define SOCKET_NAME_FMT "%s/%llu.sock"

static void print_error(const char *msg) {
    fprintf(stderr, "sdlText: %s\n", msg);
}

static int ensure_runtime_dir(char *path, size_t path_size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        print_error("HOME environment variable not set");
        return -1;
    }

    if (snprintf(path, path_size, "%s%s", home, SOCKET_DIR_SUFFIX) >= (int)path_size) {
        print_error("socket directory path too long");
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            print_error("window not initialized (missing socket directory)");
        } else {
            fprintf(stderr, "sdlText: stat failed for %s: %s\n", path, strerror(errno));
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "sdlText: %s exists and is not a directory\n", path);
        return -1;
    }
    return 0;
}

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "sdlText: invalid integer for %s: %s\n", name, value);
        return -1;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "sdlText: integer out of range for %s: %s\n", name, value);
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static void append_token(char **text, size_t *len, const char *token, int insert_space) {
    size_t token_len = strlen(token);
    size_t extra = token_len + (insert_space ? 1 : 0);
    char *new_text = realloc(*text, *len + extra + 1);
    if (new_text == NULL) {
        free(*text);
        *text = NULL;
        *len = 0;
        return;
    }
    *text = new_text;
    if (insert_space) {
        (*text)[(*len)++] = ' ';
    }
    memcpy(*text + *len, token, token_len);
    *len += token_len;
    (*text)[*len] = '\0';
}

static int parse_text_argument(int *index, int argc, char *argv[], char **out_text) {
    free(*out_text);
    *out_text = NULL;
    size_t len = 0;
    int suppress_space = 0;

    for (int i = *index; i < argc; ++i) {
        int is_option = 0;
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "-id") == 0 ||
                strcmp(argv[i], "-text") == 0) {
                is_option = 1;
            }
        } else if (strcasecmp(argv[i], "to") == 0 || strcasecmp(argv[i], "TO") == 0) {
            is_option = 1;
        }

        if (is_option) {
            if (len == 0) {
                print_error("missing value for -text");
                free(*out_text);
                *out_text = NULL;
                return -1;
            }
            *index = i - 1;
            break;
        }

        if (strcmp(argv[i], "+") == 0) {
            if (suppress_space) {
                print_error("consecutive '+' tokens in -text value");
                free(*out_text);
                *out_text = NULL;
                return -1;
            }
            suppress_space = 1;
            continue;
        }

        append_token(out_text, &len, argv[i], len > 0 && suppress_space == 0);
        if (*out_text == NULL) {
            print_error("memory allocation failure while parsing -text");
            return -1;
        }
        suppress_space = 0;
    }

    if (*out_text == NULL || (*out_text)[0] == '\0') {
        print_error("missing value for -text");
        free(*out_text);
        *out_text = NULL;
        return -1;
    }

    if (suppress_space) {
        print_error("dangling '+' token at end of -text");
        free(*out_text);
        *out_text = NULL;
        return -1;
    }

    return 0;
}

static int send_command(const char *socket_path, const char *payload) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "sdlText: socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "sdlText: connect failed for %s: %s\n", socket_path, strerror(errno));
        close(fd);
        return -1;
    }

    size_t payload_len = strlen(payload);
    ssize_t written = write(fd, payload, payload_len);
    if (written != (ssize_t)payload_len) {
        fprintf(stderr, "sdlText: failed to send command: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    char buf[16];
    (void)read(fd, buf, sizeof(buf));
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    int x = 0;
    int y = 0;
    char *text = NULL;
    unsigned long long id = 0;
    bool have_x = false;
    bool have_y = false;
    bool have_text = false;
    bool have_id = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                print_error("missing value for -x");
                goto fail;
            }
            if (parse_int(argv[i], "-x", &x) != 0) {
                goto fail;
            }
            have_x = true;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                print_error("missing value for -y");
                goto fail;
            }
            if (parse_int(argv[i], "-y", &y) != 0) {
                goto fail;
            }
            have_y = true;
        } else if (strcmp(argv[i], "-text") == 0) {
            if (++i >= argc) {
                print_error("missing value for -text");
                goto fail;
            }
            if (parse_text_argument(&i, argc, argv, &text) != 0) {
                goto fail;
            }
            have_text = true;
        } else if (strcmp(argv[i], "-id") == 0) {
            if (++i >= argc) {
                print_error("missing value for -id");
                goto fail;
            }
            char *end = NULL;
            errno = 0;
            unsigned long long parsed = strtoull(argv[i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0') {
                fprintf(stderr, "sdlText: invalid value for -id: %s\n", argv[i]);
                goto fail;
            }
            id = parsed;
            have_id = true;
        } else {
            fprintf(stderr, "sdlText: unknown argument %s\n", argv[i]);
            goto fail;
        }
    }

    if (!have_x || !have_y || !have_text || !have_id) {
        print_error("Usage: sdlText -x <int> -y <int> -text <string> -id <window id>");
        goto fail;
    }

    char runtime_dir[PATH_MAX];
    if (ensure_runtime_dir(runtime_dir, sizeof(runtime_dir)) != 0) {
        goto fail;
    }

    char socket_path[PATH_MAX];
    if (snprintf(socket_path, sizeof(socket_path), SOCKET_NAME_FMT, runtime_dir, id) >= (int)sizeof(socket_path)) {
        print_error("socket path too long");
        goto fail;
    }

    size_t needed = strlen(text) + 64;
    char *payload = malloc(needed);
    if (payload == NULL) {
        print_error("out of memory building command payload");
        goto fail;
    }

    if (snprintf(payload, needed, "TEXT|%d|%d|%s\n", x, y, text) >= (int)needed) {
        print_error("text too long");
        free(payload);
        goto fail;
    }

    if (send_command(socket_path, payload) != 0) {
        free(payload);
        goto fail;
    }

    free(payload);
    free(text);
    return EXIT_SUCCESS;

fail:
    free(text);
    return EXIT_FAILURE;
}
