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

void budo_input_enable_mouse(int enable) {
    if (enable) {
        printf("\033[?1000h\033[?1006h");
    } else {
        printf("\033[?1000l\033[?1006l");
    }
    fflush(stdout);
}

static int parse_mouse_event(const unsigned char *buffer, int count, budo_input_state_t *state) {
    char text[32];
    int button = 0;
    int x = 0;
    int y = 0;
    char action = '\0';

    if (!state || count <= 0) {
        return 0;
    }
    if (count >= (int)sizeof(text)) {
        count = (int)sizeof(text) - 1;
    }
    memcpy(text, buffer, (size_t)count);
    text[count] = '\0';

    if (sscanf(text, "\033[<%d;%d;%d%c", &button, &x, &y, &action) == 4) {
        state->mouse_x = x;
        state->mouse_y = y;
        if (action == 'M') {
            state->mouse_buttons = button;
        } else {
            state->mouse_buttons = 0;
        }
        return 1;
    }

    return 0;
}

int budo_input_poll_state(budo_input_state_t *state) {
    unsigned char buffer[32];
    int count;

    if (!state) {
        return 0;
    }

    state->quit_requested = 0;
    state->key_up = 0;
    state->key_down = 0;
    state->key_left = 0;
    state->key_right = 0;
    state->key_space = 0;
    state->mouse_x = 0;
    state->mouse_y = 0;
    state->mouse_buttons = 0;

    count = read_input(buffer, sizeof(buffer));
    if (count == 0) {
        return 0;
    }

    if (buffer[0] == 0x1b && count >= 3 && buffer[1] == '[') {
        if (buffer[2] == '<') {
            return parse_mouse_event(buffer, count, state);
        }
        switch (buffer[2]) {
        case 'A':
            state->key_up = 1;
            return 1;
        case 'B':
            state->key_down = 1;
            return 1;
        case 'C':
            state->key_right = 1;
            return 1;
        case 'D':
            state->key_left = 1;
            return 1;
        default:
            return 0;
        }
    }

    if (buffer[0] == '\n' || buffer[0] == '\r') {
        return 1;
    }
    if (buffer[0] == ' ') {
        state->key_space = 1;
        return 1;
    }
    if (buffer[0] == 'q' || buffer[0] == 'Q') {
        state->quit_requested = 1;
        return 1;
    }

    return 0;
}
