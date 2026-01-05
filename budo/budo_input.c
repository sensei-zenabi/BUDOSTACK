#define _POSIX_C_SOURCE 200809L

#include "budo_input.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

static struct termios budo_original_termios;
static int budo_termios_saved = 0;
static int budo_original_flags = -1;

static void budo_restore_terminal(void) {
    if (budo_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &budo_original_termios);
    }
    if (budo_original_flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, budo_original_flags);
    }
}

int budo_input_init(void) {
    if (tcgetattr(STDIN_FILENO, &budo_original_termios) == -1) {
        perror("budo_input_init: tcgetattr");
        return -1;
    }
    budo_termios_saved = 1;

    struct termios raw = budo_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("budo_input_init: tcsetattr");
        return -1;
    }

    budo_original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (budo_original_flags == -1) {
        perror("budo_input_init: fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, budo_original_flags | O_NONBLOCK) == -1) {
        perror("budo_input_init: fcntl(F_SETFL)");
        return -1;
    }

    if (atexit(budo_restore_terminal) != 0) {
        fprintf(stderr, "budo_input_init: failed to register cleanup\n");
        return -1;
    }

    return 0;
}

void budo_input_shutdown(void) {
    budo_restore_terminal();
}

static int budo_parse_escape_sequence(const unsigned char *buffer, ssize_t length, struct budo_input_event *event) {
    if (length <= 0 || !buffer || !event) {
        return 0;
    }

    if (buffer[0] != 0x1b) {
        return 0;
    }

    if (length >= 3 && buffer[1] == '[') {
        switch (buffer[2]) {
            case 'A':
                event->key = BUDO_KEY_UP;
                return 1;
            case 'B':
                event->key = BUDO_KEY_DOWN;
                return 1;
            case 'C':
                event->key = BUDO_KEY_RIGHT;
                return 1;
            case 'D':
                event->key = BUDO_KEY_LEFT;
                return 1;
            default:
                break;
        }
    }

    event->key = BUDO_KEY_ESCAPE;
    return 1;
}

int budo_input_poll(struct budo_input_event *event) {
    if (!event) {
        return 0;
    }

    event->key = BUDO_KEY_NONE;
    event->ch = '\0';

    unsigned char buffer[8];
    ssize_t rd = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (rd <= 0) {
        return 0;
    }

    if (budo_parse_escape_sequence(buffer, rd, event)) {
        return 1;
    }

    unsigned char ch = buffer[0];
    if (ch == '\r' || ch == '\n') {
        event->key = BUDO_KEY_ENTER;
        return 1;
    }
    if (ch == ' ') {
        event->key = BUDO_KEY_SPACE;
        return 1;
    }
    if (isprint(ch)) {
        event->key = BUDO_KEY_CHAR;
        event->ch = (char)ch;
        return 1;
    }

    return 0;
}

static int budo_send_mouse_query(int fd) {
    if (fd < 0) {
        return -1;
    }

    const char query[] = "\x1b]777;mouse=query\a";
    size_t total_written = 0u;

    while (total_written < sizeof(query) - 1u) {
        ssize_t w = write(fd, query + total_written, sizeof(query) - 1u - total_written);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("budo_input_query_mouse: write");
            return -1;
        }
        total_written += (size_t)w;
    }

    return 0;
}

static int budo_read_mouse_response(int fd,
                                   int *out_x,
                                   int *out_y,
                                   unsigned int *out_left,
                                   unsigned int *out_right,
                                   int timeout_ms) {
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
        struct timeval *tv_ptr = NULL;
        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tv_ptr = &tv;
        }

        int ready = select(fd + 1, &rfds, NULL, NULL, tv_ptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("budo_input_query_mouse: select");
            return -1;
        }
        if (ready == 0) {
            fprintf(stderr, "budo_input_query_mouse: timed out waiting for terminal response\n");
            return -1;
        }

        ssize_t rd = read(fd, buffer + offset, sizeof(buffer) - offset - 1u);
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("budo_input_query_mouse: read");
            return -1;
        }
        if (rd == 0) {
            fprintf(stderr, "budo_input_query_mouse: unexpected EOF while waiting for response\n");
            return -1;
        }

        offset += (size_t)rd;
        buffer[offset] = '\0';

        char *newline = memchr(buffer, '\n', offset);
        if (!newline) {
            if (offset + 1u >= sizeof(buffer)) {
                fprintf(stderr, "budo_input_query_mouse: response too long\n");
                return -1;
            }
            continue;
        }

        *newline = '\0';
        const char prefix[] = "_TERM_MOUSE ";
        size_t prefix_len = strlen(prefix);
        if (strncmp(buffer, prefix, prefix_len) != 0) {
            fprintf(stderr, "budo_input_query_mouse: unexpected response '%s'\n", buffer);
            return -1;
        }

        int x = 0;
        int y = 0;
        unsigned int left = 0u;
        unsigned int right = 0u;
        int parsed = sscanf(buffer + prefix_len, "%d %d %u %u", &x, &y, &left, &right);
        if (parsed != 4) {
            fprintf(stderr, "budo_input_query_mouse: failed to parse response '%s'\n", buffer);
            return -1;
        }

        *out_x = x;
        *out_y = y;
        *out_left = left;
        *out_right = right;
        return 0;
    }
}

int budo_input_query_mouse(struct budo_mouse_state *state, int timeout_ms) {
    if (!state) {
        return -1;
    }

    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) {
        fprintf(stderr, "budo_input_query_mouse: failed to open /dev/tty, falling back to stdout/stdin\n");
        tty_fd = STDOUT_FILENO;
    }

    if (budo_send_mouse_query(tty_fd) != 0) {
        if (tty_fd != STDOUT_FILENO) {
            close(tty_fd);
        }
        return -1;
    }

    int read_fd = tty_fd;
    if (tty_fd == STDOUT_FILENO) {
        read_fd = STDIN_FILENO;
    }

    int x = 0;
    int y = 0;
    unsigned int left = 0u;
    unsigned int right = 0u;
    if (budo_read_mouse_response(read_fd, &x, &y, &left, &right, timeout_ms) != 0) {
        if (tty_fd != STDOUT_FILENO) {
            close(tty_fd);
        }
        return -1;
    }

    if (tty_fd != STDOUT_FILENO) {
        close(tty_fd);
    }

    state->x = x;
    state->y = y;
    state->left_clicks = left;
    state->right_clicks = right;
    return 0;
}
