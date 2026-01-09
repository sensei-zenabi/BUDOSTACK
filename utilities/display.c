#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "../lib/libimage.h"

static int query_cursor_position(long *row_out, long *col_out) {
    if (row_out == NULL || col_out == NULL) {
        return -1;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return -1;
    }

    struct termios original;
    if (tcgetattr(STDIN_FILENO, &original) == -1) {
        perror("display: tcgetattr");
        return -1;
    }

    struct termios raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("display: tcsetattr");
        return -1;
    }

    int rc = -1;
    int saved_errno = 0;

    const char query[] = "\033[6n";
    size_t offset = 0;
    while (offset < sizeof(query) - 1) {
        ssize_t written = write(STDOUT_FILENO, query + offset, (sizeof(query) - 1) - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("display: write");
            goto restore;
        }
        offset += (size_t)written;
    }

    if (fflush(stdout) == EOF) {
        perror("display: fflush");
        goto restore;
    }

    char response[64];
    size_t index = 0;
    while (index < sizeof(response) - 1) {
        char ch;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("display: read");
            goto restore;
        }
        if (result == 0) {
            fprintf(stderr, "display: unexpected EOF while reading cursor position\n");
            goto restore;
        }
        response[index++] = ch;
        if (ch == 'R') {
            break;
        }
    }

    if (index == sizeof(response) - 1 && response[index - 1] != 'R') {
        fprintf(stderr, "display: cursor response too long\n");
        goto restore;
    }

    response[index] = '\0';

    if (index < 3 || response[0] != '\033' || response[1] != '[') {
        fprintf(stderr, "display: invalid cursor response '%s'\n", response);
        goto restore;
    }

    char *endptr = NULL;
    errno = 0;
    long row = strtol(response + 2, &endptr, 10);
    if (errno != 0 || endptr == response + 2 || *endptr != ';') {
        fprintf(stderr, "display: failed to parse row from response '%s'\n", response);
        goto restore;
    }

    const char *col_start = endptr + 1;
    errno = 0;
    long col = strtol(col_start, &endptr, 10);
    if (errno != 0 || endptr == col_start || *endptr != 'R') {
        fprintf(stderr, "display: failed to parse column from response '%s'\n", response);
        goto restore;
    }

    if (row <= 0 || col <= 0) {
        fprintf(stderr, "display: invalid cursor position row %ld column %ld\n", row, col);
        goto restore;
    }

    *row_out = row;
    *col_out = col;
    rc = 0;

restore:
    saved_errno = errno;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original) == -1) {
        perror("display: tcsetattr restore");
        rc = -1;
    }
    errno = saved_errno;
    return rc;
}

static int display_text(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror("display: fopen failed");
        return EXIT_FAILURE;
    }

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (putchar(ch) == EOF) {
            perror("display: putchar failed");
            fclose(fp);
            return EXIT_FAILURE;
        }
    }

    if (ferror(fp) != 0) {
        perror("display: fgetc failed");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: display <file>\n");
        return EXIT_FAILURE;
    }

    const char *path = argv[1];
    long row = 1;
    long col = 1;
    int have_cursor = query_cursor_position(&row, &col);
    int origin_y = (have_cursor == 0 && row > 0 && row <= INT_MAX) ? (int)(row - 1) : 0;
    (void)col;
    LibImageResult result = libimage_render_file_streamed_at(path, 0, origin_y);

    if (result == LIBIMAGE_SUCCESS) {
        return EXIT_SUCCESS;
    }

    if (result == LIBIMAGE_UNSUPPORTED_FORMAT) {
        return display_text(path);
    }

    const char *message = libimage_last_error();
    if (message != NULL && message[0] != '\0') {
        fprintf(stderr, "display: %s\n", message);
    } else {
        fprintf(stderr, "display: failed to render image\n");
    }
    return EXIT_FAILURE;
}
