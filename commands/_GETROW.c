#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int restore_terminal(const struct termios *state) {
    if (state == NULL)
        return 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, state) == -1) {
        perror("_GETROW: tcsetattr restore");
        return -1;
    }

    return 0;
}

int main(void) {
    struct termios original;
    if (tcgetattr(STDIN_FILENO, &original) == -1) {
        perror("_GETROW: tcgetattr");
        return EXIT_FAILURE;
    }

    struct termios raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("_GETROW: tcsetattr");
        return EXIT_FAILURE;
    }

    int exit_code = EXIT_FAILURE;
    const char query[] = "\033[6n";
    size_t query_len = sizeof(query) - 1;
    size_t offset = 0;

    while (offset < query_len) {
        ssize_t written = write(STDOUT_FILENO, query + offset, query_len - offset);
        if (written == -1) {
            if (errno == EINTR)
                continue;
            perror("_GETROW: write");
            goto restore;
        }
        offset += (size_t)written;
    }

    if (fflush(stdout) == EOF) {
        perror("_GETROW: fflush");
        goto restore;
    }

    char response[64];
    size_t index = 0;

    while (index < sizeof(response) - 1) {
        char ch;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        if (result == -1) {
            if (errno == EINTR)
                continue;
            perror("_GETROW: read");
            goto restore;
        }
        if (result == 0) {
            fprintf(stderr, "_GETROW: unexpected EOF while reading cursor position\n");
            goto restore;
        }

        response[index++] = ch;
        if (ch == 'R')
            break;
    }

    if (index == sizeof(response) - 1 && response[index - 1] != 'R') {
        fprintf(stderr, "_GETROW: cursor response too long\n");
        goto restore;
    }

    response[index] = '\0';

    if (index < 3 || response[0] != '\033' || response[1] != '[') {
        fprintf(stderr, "_GETROW: invalid cursor response '%s'\n", response);
        goto restore;
    }

    char *endptr = NULL;
    errno = 0;
    long row = strtol(response + 2, &endptr, 10);
    if (errno != 0 || endptr == response + 2 || *endptr != ';') {
        fprintf(stderr, "_GETROW: failed to parse row from response '%s'\n", response);
        goto restore;
    }

    const char *col_start = endptr + 1;
    errno = 0;
    long column = strtol(col_start, &endptr, 10);
    if (errno != 0 || endptr == col_start || *endptr != 'R') {
        fprintf(stderr, "_GETROW: failed to parse column from response '%s'\n", response);
        goto restore;
    }

    if (row <= 0 || column <= 0) {
        fprintf(stderr, "_GETROW: invalid row (%ld) or column (%ld)\n", row, column);
        goto restore;
    }

    if (printf("%ld\n", row) < 0) {
        perror("_GETROW: printf");
        goto restore;
    }

    exit_code = EXIT_SUCCESS;

restore:
    if (restore_terminal(&original) != 0)
        exit_code = EXIT_FAILURE;

    return exit_code;
}
