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
    printf("_TERM_MOUSE\n");
    printf("Query mouse position and button presses from the terminal emulator.\n");
    printf("Outputs a TASK array literal: {X, Y, LEFT, RIGHT}\n");
    printf("X/Y are pixel positions from the top-left corner. LEFT/RIGHT are the\n");
    printf("number of button presses since the last invocation.\n");
}

static int send_mouse_query(int fd) {
    if (fd < 0) {
        return -1;
    }

    const char query[] = "\x1b]777;mouse=query\a";
    size_t total_written = 0u;

    while (total_written < sizeof(query) - 1u) {
        ssize_t w = write(fd, query + total_written, (size_t)(sizeof(query) - 1u - total_written));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_MOUSE: write");
            return -1;
        }
        total_written += (size_t)w;
    }

    return 0;
}

static int read_mouse_response(int fd, int *out_x, int *out_y, unsigned int *out_left, unsigned int *out_right) {
    if (fd < 0 || !out_x || !out_y || !out_left || !out_right) {
        return -1;
    }

    char buffer[256];
    size_t offset = 0u;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_MOUSE: select");
            return -1;
        }
        if (ready == 0) {
            fprintf(stderr, "_TERM_MOUSE: timed out waiting for terminal response\n");
            return -1;
        }

        ssize_t rd = read(fd, buffer + offset, sizeof(buffer) - offset - 1u);
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_TERM_MOUSE: read");
            return -1;
        }
        if (rd == 0) {
            fprintf(stderr, "_TERM_MOUSE: unexpected EOF while waiting for response\n");
            return -1;
        }

        offset += (size_t)rd;
        buffer[offset] = '\0';

        char *newline = memchr(buffer, '\n', offset);
        if (!newline) {
            if (offset + 1u >= sizeof(buffer)) {
                fprintf(stderr, "_TERM_MOUSE: response too long\n");
                return -1;
            }
            continue;
        }

        *newline = '\0';
        const char prefix[] = "_TERM_MOUSE ";
        size_t prefix_len = strlen(prefix);
        if (strncmp(buffer, prefix, prefix_len) != 0) {
            fprintf(stderr, "_TERM_MOUSE: unexpected response '%s'\n", buffer);
            return -1;
        }

        int x = 0;
        int y = 0;
        unsigned int left = 0u;
        unsigned int right = 0u;
        int parsed = sscanf(buffer + prefix_len, "%d %d %u %u", &x, &y, &left, &right);
        if (parsed != 4) {
            fprintf(stderr, "_TERM_MOUSE: failed to parse response '%s'\n", buffer);
            return -1;
        }

        *out_x = x;
        *out_y = y;
        *out_left = left;
        *out_right = right;
        return 0;
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return 0;
    }

    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) {
        fprintf(stderr, "_TERM_MOUSE: failed to open /dev/tty, falling back to stdout/stdin\n");
        tty_fd = STDOUT_FILENO;
    }

    if (send_mouse_query(tty_fd) != 0) {
        if (tty_fd != STDOUT_FILENO) {
            close(tty_fd);
        }
        return EXIT_FAILURE;
    }

    int x = 0;
    int y = 0;
    unsigned int left = 0u;
    unsigned int right = 0u;
    int read_fd = tty_fd;
    if (tty_fd == STDOUT_FILENO) {
        read_fd = STDIN_FILENO;
    }

    if (read_mouse_response(read_fd, &x, &y, &left, &right) != 0) {
        if (tty_fd != STDOUT_FILENO) {
            close(tty_fd);
        }
        return EXIT_FAILURE;
    }

    if (tty_fd != STDOUT_FILENO) {
        close(tty_fd);
    }

    printf("{%d, %d, %u, %u}\n", x, y, left, right);
    return EXIT_SUCCESS;
}
