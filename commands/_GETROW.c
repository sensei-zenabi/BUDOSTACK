#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define PROGRAM_NAME "_GETROW"

static int restore_terminal(int fd, const struct termios *orig) {
    if (tcsetattr(fd, TCSANOW, orig) == -1) {
        perror(PROGRAM_NAME ": tcsetattr");
        return -1;
    }
    return 0;
}

static int query_cursor_position(int *out_row, int *out_col) {
    if (out_row == NULL || out_col == NULL) {
        errno = EINVAL;
        perror(PROGRAM_NAME ": invalid output pointer");
        return -1;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, PROGRAM_NAME ": stdin and stdout must be a terminal\n");
        return -1;
    }

    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        perror(PROGRAM_NAME ": tcgetattr");
        return -1;
    }

    struct termios raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10; /* 1 second timeout */

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror(PROGRAM_NAME ": tcsetattr");
        return -1;
    }

    int status = -1;

    if (fflush(stdout) == EOF) {
        perror(PROGRAM_NAME ": fflush");
        goto restore;
    }

    const char query[] = "\033[6n";
    ssize_t written = write(STDOUT_FILENO, query, sizeof(query) - 1);
    if (written != (ssize_t)(sizeof(query) - 1)) {
        if (written == -1)
            perror(PROGRAM_NAME ": write");
        else
            fprintf(stderr, PROGRAM_NAME ": short write when querying cursor\n");
        goto restore;
    }

    char response[64];
    size_t len = 0;
    int timeouts = 0;
    int done = 0;

    while (len < sizeof(response) - 1 && timeouts < 5) {
        ssize_t nread = read(STDIN_FILENO, response + len, sizeof(response) - 1 - len);
        if (nread > 0) {
            len += (size_t)nread;
            if (response[len - 1] == 'R') {
                done = 1;
                break;
            }
        } else if (nread == 0) {
            ++timeouts;
        } else if (errno != EINTR) {
            perror(PROGRAM_NAME ": read");
            goto restore;
        }
    }

    if (!done) {
        fprintf(stderr, PROGRAM_NAME ": failed to read cursor position response\n");
        goto restore;
    }

    response[len] = '\0';

    if (sscanf(response, "\033[%d;%dR", out_row, out_col) != 2) {
        fprintf(stderr, PROGRAM_NAME ": unexpected response '%s'\n", response);
        goto restore;
    }

    status = 0;

restore:
    if (restore_terminal(STDIN_FILENO, &orig) == -1)
        status = -1;

    return status;
}

int main(void) {
    int row = 0;
    int col = 0;

    if (query_cursor_position(&row, &col) != 0)
        return EXIT_FAILURE;

    printf("%d\n", row);
    return EXIT_SUCCESS;
}

