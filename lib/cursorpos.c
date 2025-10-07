#define _POSIX_C_SOURCE 200809L

#include "cursorpos.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static int write_all(int fd, const char *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, data + written, len - written);
        if (rc > 0) {
            written += (size_t)rc;
            continue;
        }
        if (rc == -1 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

int cursorpos_query(int *row_out, int *col_out) {
    if (row_out == NULL || col_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    int fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd == -1)
        return -1;

    struct termios original;
    if (tcgetattr(fd, &original) == -1) {
        close(fd);
        return -1;
    }

    struct termios raw = original;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /* 100ms units */

    if (tcsetattr(fd, TCSANOW, &raw) == -1) {
        close(fd);
        return -1;
    }

    int ret = -1;

    if (tcflush(fd, TCIFLUSH) == -1)
        goto restore;

    static const char query[] = "\033[6n";
    if (write_all(fd, query, sizeof(query) - 1) == -1)
        goto restore;

    char response[64];
    size_t len = 0;
    int timeouts = 0;

    while (len < sizeof(response) - 1) {
        char ch;
        ssize_t rc = read(fd, &ch, 1);
        if (rc == 1) {
            response[len++] = ch;
            if (ch == 'R')
                break;
        } else if (rc == 0) {
            if (++timeouts >= 20) {
                errno = ETIMEDOUT;
                goto restore;
            }
        } else if (rc == -1 && errno == EINTR) {
            continue;
        } else {
            goto restore;
        }
    }

    if (len == 0 || response[len - 1] != 'R') {
        errno = ETIMEDOUT;
        goto restore;
    }

    response[len] = '\0';

    char *cursor = response;
    while (*cursor == '\r' || *cursor == '\n')
        ++cursor;

    if (*cursor == '\033')
        ++cursor;

    if (*cursor != '[') {
        errno = EILSEQ;
        goto restore;
    }
    ++cursor;

    char *endptr = NULL;
    errno = 0;
    long row = strtol(cursor, &endptr, 10);
    if (errno != 0 || endptr == cursor || row < INT_MIN || row > INT_MAX) {
        errno = EILSEQ;
        goto restore;
    }
    if (*endptr != ';') {
        errno = EILSEQ;
        goto restore;
    }
    cursor = endptr + 1;

    errno = 0;
    long col = strtol(cursor, &endptr, 10);
    if (errno != 0 || endptr == cursor || col < INT_MIN || col > INT_MAX) {
        errno = EILSEQ;
        goto restore;
    }
    if (*endptr != 'R') {
        errno = EILSEQ;
        goto restore;
    }

    *row_out = (int)row;
    *col_out = (int)col;
    ret = 0;

restore:
    {
        int saved_errno = errno;
        if (tcsetattr(fd, TCSANOW, &original) == -1 && ret == 0) {
            saved_errno = errno;
            ret = -1;
        }
        if (close(fd) == -1 && ret == 0) {
            saved_errno = errno;
            ret = -1;
        }
        errno = saved_errno;
    }
    return ret;
}
