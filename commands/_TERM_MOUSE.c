#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_original_termios;
static int g_termios_saved = 0;
static int g_original_flags = -1;

static void disable_mouse_reporting(void) {
    if (printf("\x1b[?1003l\x1b[?1006l\x1b[?1016l") < 0) {
        perror("_TERM_MOUSE: printf(disable)");
    }
    fflush(stdout);
}

static void restore_terminal(void) {
    disable_mouse_reporting();

    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    }
    if (g_original_flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, g_original_flags);
    }
}

static int setup_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &g_original_termios) == -1) {
        perror("_TERM_MOUSE: tcgetattr");
        return -1;
    }
    g_termios_saved = 1;

    struct termios raw = g_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("_TERM_MOUSE: tcsetattr");
        return -1;
    }

    g_original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_original_flags == -1) {
        perror("_TERM_MOUSE: fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, g_original_flags | O_NONBLOCK) == -1) {
        perror("_TERM_MOUSE: fcntl(F_SETFL)");
        return -1;
    }

    if (atexit(restore_terminal) != 0) {
        fprintf(stderr, "_TERM_MOUSE: failed to register cleanup\n");
        return -1;
    }

    return 0;
}

static int enable_mouse_reporting(void) {
    if (printf("\x1b[?1003h\x1b[?1006h\x1b[?1016h") < 0) {
        perror("_TERM_MOUSE: printf(enable)");
        return -1;
    }
    if (fflush(stdout) != 0) {
        perror("_TERM_MOUSE: fflush(enable)");
        return -1;
    }
    return 0;
}

static int parse_number(const unsigned char *buf, size_t len, size_t *idx, long *out_value) {
    if (*idx >= len || !isdigit((unsigned char)buf[*idx])) {
        return -1;
    }

    long value = 0;
    while (*idx < len && isdigit((unsigned char)buf[*idx])) {
        value = value * 10 + (buf[*idx] - '0');
        (*idx)++;
    }

    *out_value = value;
    return 0;
}

static int parse_event(const unsigned char *buf,
                       size_t len,
                       size_t *consumed,
                       int *xpos,
                       int *ypos,
                       int *left_pressed,
                       int *right_pressed) {
    if (len < 6 || buf[0] != '\x1b' || buf[1] != '[' || buf[2] != '<') {
        return -1;
    }

    size_t idx = 3;
    long button_code = 0;
    long x = 0;
    long y = 0;

    if (parse_number(buf, len, &idx, &button_code) != 0) {
        return -1;
    }
    if (idx >= len || buf[idx] != ';') {
        return -1;
    }
    idx++;

    if (parse_number(buf, len, &idx, &x) != 0) {
        return -1;
    }
    if (idx >= len || buf[idx] != ';') {
        return -1;
    }
    idx++;

    if (parse_number(buf, len, &idx, &y) != 0) {
        return -1;
    }
    if (idx >= len || (buf[idx] != 'M' && buf[idx] != 'm')) {
        return -1;
    }

    char type = (char)buf[idx];
    *consumed = idx + 1;

    *xpos = (int)x;
    *ypos = (int)y;

    int button = (int)(button_code & 0x3);
    if (type == 'M') {
        if (button == 0) {
            *left_pressed = 1;
        } else if (button == 2) {
            *right_pressed = 1;
        }
    } else {
        if (button == 0) {
            *left_pressed = 0;
        } else if (button == 2) {
            *right_pressed = 0;
        }
    }

    return 0;
}

static ssize_t read_all_bytes(unsigned char **out_buffer) {
    size_t len = 0;
    size_t cap = 0;
    unsigned char *buffer = NULL;
    unsigned char chunk[64];

    for (;;) {
        ssize_t rd = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (rd > 0) {
            if (len + (size_t)rd > cap) {
                size_t new_cap = (cap == 0) ? 128 : cap * 2;
                while (new_cap < len + (size_t)rd) {
                    new_cap *= 2;
                }
                unsigned char *tmp = (unsigned char *)realloc(buffer, new_cap);
                if (!tmp) {
                    perror("_TERM_MOUSE: realloc");
                    free(buffer);
                    return -1;
                }
                buffer = tmp;
                cap = new_cap;
            }
            for (ssize_t i = 0; i < rd; ++i) {
                buffer[len++] = chunk[i];
            }
            continue;
        }
        if (rd == 0 || (rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            break;
        }
        if (rd == -1 && errno == EINTR) {
            continue;
        }
        perror("_TERM_MOUSE: read");
        free(buffer);
        return -1;
    }

    *out_buffer = buffer;
    return (ssize_t)len;
}

int main(void) {
    if (setup_terminal() != 0) {
        return EXIT_FAILURE;
    }

    if (enable_mouse_reporting() != 0) {
        return EXIT_FAILURE;
    }

    unsigned char *buffer = NULL;
    ssize_t len = read_all_bytes(&buffer);
    if (len < 0) {
        free(buffer);
        return EXIT_FAILURE;
    }

    int xpos = 0;
    int ypos = 0;
    int left_pressed = 0;
    int right_pressed = 0;

    size_t idx = 0;
    while (idx < (size_t)len) {
        size_t consumed = 0;
        if (buffer[idx] == '\x1b') {
            if (parse_event(buffer + idx, (size_t)len - idx, &consumed, &xpos, &ypos, &left_pressed, &right_pressed) == 0) {
                idx += consumed;
                continue;
            }
        }
        idx++;
    }

    printf("{%d, %d, %d, %d}\n", xpos, ypos, left_pressed, right_pressed);

    free(buffer);
    return EXIT_SUCCESS;
}

