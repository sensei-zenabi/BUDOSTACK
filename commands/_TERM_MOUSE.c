#define _POSIX_C_SOURCE 200809L

#include <errno.h>
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

static int send_mouse_query(void) {
    if (printf("\x1b]777;mouse=query\a") < 0) {
        perror("_TERM_MOUSE: printf");
        return -1;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_MOUSE: fflush");
        return -1;
    }
    return 0;
}

static int read_mouse_response(int *out_x, int *out_y, unsigned int *out_left, unsigned int *out_right) {
    if (!out_x || !out_y || !out_left || !out_right) {
        return -1;
    }

    char buffer[256];
    size_t offset = 0u;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
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

        ssize_t rd = read(STDIN_FILENO, buffer + offset, sizeof(buffer) - offset - 1u);
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

    if (send_mouse_query() != 0) {
        return EXIT_FAILURE;
    }

    int x = 0;
    int y = 0;
    unsigned int left = 0u;
    unsigned int right = 0u;
    if (read_mouse_response(&x, &y, &left, &right) != 0) {
        return EXIT_FAILURE;
    }

    printf("{%d, %d, %u, %u}\n", x, y, left, right);
    return EXIT_SUCCESS;
}
