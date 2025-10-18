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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_DIR_SUFFIX "/.budostack/sdl"
#define SOCKET_NAME_FMT "%s/%llu.sock"

static void print_error(const char *msg) {
    fprintf(stderr, "sdlDrawSprite: %s\n", msg);
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
            fprintf(stderr, "sdlDrawSprite: stat failed for %s: %s\n", path, strerror(errno));
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "sdlDrawSprite: %s exists and is not a directory\n", path);
        return -1;
    }
    return 0;
}

static int parse_int(const char *value, const char *name, int *out) {
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        fprintf(stderr, "sdlDrawSprite: invalid integer for %s: %s\n", name, value);
        return -1;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "sdlDrawSprite: integer out of range for %s: %s\n", name, value);
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static int send_command(const char *socket_path, const char *payload) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "sdlDrawSprite: socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "sdlDrawSprite: connect failed for %s: %s\n", socket_path, strerror(errno));
        close(fd);
        return -1;
    }

    size_t payload_len = strlen(payload);
    ssize_t written = write(fd, payload, payload_len);
    if (written != (ssize_t)payload_len) {
        fprintf(stderr, "sdlDrawSprite: failed to send command: %s\n", strerror(errno));
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
    const char *file = NULL;
    unsigned long long id = 0;
    bool have_x = false;
    bool have_y = false;
    bool have_file = false;
    bool have_id = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) {
                print_error("missing value for -x");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-x", &x) != 0) {
                return EXIT_FAILURE;
            }
            have_x = true;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) {
                print_error("missing value for -y");
                return EXIT_FAILURE;
            }
            if (parse_int(argv[i], "-y", &y) != 0) {
                return EXIT_FAILURE;
            }
            have_y = true;
        } else if (strcmp(argv[i], "-file") == 0) {
            if (++i >= argc) {
                print_error("missing value for -file");
                return EXIT_FAILURE;
            }
            file = argv[i];
            have_file = true;
        } else if (strcmp(argv[i], "-id") == 0) {
            if (++i >= argc) {
                print_error("missing value for -id");
                return EXIT_FAILURE;
            }
            char *end = NULL;
            errno = 0;
            unsigned long long parsed = strtoull(argv[i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0') {
                fprintf(stderr, "sdlDrawSprite: invalid value for -id: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
            id = parsed;
            have_id = true;
        } else {
            fprintf(stderr, "sdlDrawSprite: unknown argument %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (!have_x || !have_y || !have_file || !have_id) {
        print_error("Usage: sdlDrawSprite -x <int> -y <int> -file <path> -id <window id>");
        return EXIT_FAILURE;
    }

    char runtime_dir[PATH_MAX];
    if (ensure_runtime_dir(runtime_dir, sizeof(runtime_dir)) != 0) {
        return EXIT_FAILURE;
    }

    char socket_path[PATH_MAX];
    if (snprintf(socket_path, sizeof(socket_path), SOCKET_NAME_FMT, runtime_dir, id) >= (int)sizeof(socket_path)) {
        print_error("socket path too long");
        return EXIT_FAILURE;
    }

    char payload[PATH_MAX + 64];
    if (snprintf(payload, sizeof(payload), "DRAW_SPRITE|%d|%d|%s\n", x, y, file) >= (int)sizeof(payload)) {
        print_error("file path too long");
        return EXIT_FAILURE;
    }

    if (send_command(socket_path, payload) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
