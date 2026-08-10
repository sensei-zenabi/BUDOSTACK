#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_OVERLAY [enable|disable]\n");
    fprintf(stderr, "  Controls whether graphics/overlay.png is drawn by the terminal.\n");
    fprintf(stderr, "  With no argument, prints 1 when enabled or 0 when disabled.\n");
}

static int send_request(int fd, const char *action) {
    char request[64];
    int length = snprintf(request, sizeof(request), "\x1b]777;overlay=%s\a", action);
    if (length < 0 || (size_t)length >= sizeof(request)) {
        fprintf(stderr, "_TERM_OVERLAY: failed to create terminal request\n");
        return -1;
    }

    size_t written = 0u;
    while (written < (size_t)length) {
        ssize_t result = write(fd, request + written, (size_t)length - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_OVERLAY: write");
            return -1;
        }
        written += (size_t)result;
    }
    return 0;
}

static int read_response(int fd) {
    char buffer[64];
    size_t offset = 0u;

    while (offset + 1u < sizeof(buffer)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_OVERLAY: select");
            return -1;
        }
        if (ready == 0) {
            fprintf(stderr, "_TERM_OVERLAY: timed out waiting for terminal response\n");
            return -1;
        }

        ssize_t count = read(fd, buffer + offset, sizeof(buffer) - offset - 1u);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_OVERLAY: read");
            return -1;
        }
        if (count == 0) {
            fprintf(stderr, "_TERM_OVERLAY: unexpected EOF waiting for terminal response\n");
            return -1;
        }
        offset += (size_t)count;
        buffer[offset] = '\0';

        char *newline = memchr(buffer, '\n', offset);
        if (newline) {
            *newline = '\0';
            const char prefix[] = "_TERM_OVERLAY ";
            if (strncmp(buffer, prefix, sizeof(prefix) - 1u) != 0 ||
                (strcmp(buffer + sizeof(prefix) - 1u, "0") != 0 &&
                 strcmp(buffer + sizeof(prefix) - 1u, "1") != 0)) {
                fprintf(stderr, "_TERM_OVERLAY: unexpected response '%s'\n", buffer);
                return -1;
            }
            printf("%s\n", buffer + sizeof(prefix) - 1u);
            return 0;
        }
    }

    fprintf(stderr, "_TERM_OVERLAY: terminal response was too long\n");
    return -1;
}

int main(int argc, char **argv) {
    const char *action = "query";
    if (argc > 2 || !argv) {
        print_usage();
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        action = argv[1];
        if (!action || (strcmp(action, "enable") != 0 && strcmp(action, "disable") != 0)) {
            print_usage();
            return EXIT_FAILURE;
        }
    }

    int tty_fd = open("/dev/tty", O_RDWR);
    int write_fd = tty_fd >= 0 ? tty_fd : STDOUT_FILENO;
    int read_fd = tty_fd >= 0 ? tty_fd : STDIN_FILENO;
    if (send_request(write_fd, action) != 0) {
        if (tty_fd >= 0) {
            close(tty_fd);
        }
        return EXIT_FAILURE;
    }

    int result = 0;
    if (strcmp(action, "query") == 0) {
        result = read_response(read_fd);
    }
    if (tty_fd >= 0 && close(tty_fd) != 0) {
        perror("_TERM_OVERLAY: close");
        return EXIT_FAILURE;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
