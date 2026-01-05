#define _POSIX_C_SOURCE 200809L

#include "budo_input.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_saved_termios;
static int g_saved_flags = -1;
static int g_input_ready = 0;

int budo_input_init(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &g_saved_termios) == -1) {
        perror("tcgetattr");
        return -1;
    }
    g_saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_saved_flags == -1) {
        perror("fcntl");
        return -1;
    }

    raw = g_saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, g_saved_flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return -1;
    }
    g_input_ready = 1;
    return 0;
}

void budo_input_shutdown(void) {
    if (!g_input_ready) {
        return;
    }
    if (g_saved_flags != -1) {
        if (fcntl(STDIN_FILENO, F_SETFL, g_saved_flags) == -1) {
            perror("fcntl");
        }
    }
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios) == -1) {
        perror("tcsetattr");
    }
    g_input_ready = 0;
}

static int read_input(unsigned char *buffer, size_t size) {
    ssize_t count;

    if (!buffer || size == 0) {
        return 0;
    }

    count = read(STDIN_FILENO, buffer, size);
    if (count <= 0) {
        if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return 0;
    }
    return (int)count;
}

int budo_input_poll(budo_key_t *out_key) {
    unsigned char buffer[8];
    int count;

    if (!out_key) {
        return 0;
    }

    *out_key = BUDO_KEY_NONE;
    count = read_input(buffer, sizeof(buffer));
    if (count == 0) {
        return 0;
    }

    if (buffer[0] == 0x1b && count >= 3 && buffer[1] == '[') {
        switch (buffer[2]) {
        case 'A':
            *out_key = BUDO_KEY_UP;
            return 1;
        case 'B':
            *out_key = BUDO_KEY_DOWN;
            return 1;
        case 'C':
            *out_key = BUDO_KEY_RIGHT;
            return 1;
        case 'D':
            *out_key = BUDO_KEY_LEFT;
            return 1;
        default:
            return 0;
        }
    }

    if (buffer[0] == '\n' || buffer[0] == '\r') {
        *out_key = BUDO_KEY_ENTER;
        return 1;
    }
    if (buffer[0] == ' ') {
        *out_key = BUDO_KEY_SPACE;
        return 1;
    }
    if (buffer[0] == 'q' || buffer[0] == 'Q') {
        *out_key = BUDO_KEY_QUIT;
        return 1;
    }

    return 0;
}
