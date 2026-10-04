#define _POSIX_C_SOURCE 200809L
#include "terminal_input.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct termios saved_terminal;
static int active;
static int cleanup_registered;
static volatile sig_atomic_t interrupted;
static const int signals[] = { SIGINT, SIGTERM, SIGHUP };
static struct sigaction saved_signals[3];
static int installed_signals;
static int escape_state;
static int escape_length;
static struct timespec escape_time;

static void interrupt_input(int signo) {
    (void)signo;
    interrupted = 1;
}

int budostack_terminal_interrupted(void) {
    return interrupted != 0;
}

void budostack_terminal_input_stop(void) {
    if (!active) {
        return;
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal) == -1) {
        perror("terminal input: restore");
    }
    printf("\033[0m\033[?25h\033[H\033[2J");
    fflush(stdout);
    for (int i = 0; i < installed_signals; i++) {
        (void)sigaction(signals[i], &saved_signals[i], NULL);
    }
    installed_signals = 0;
    active = 0;
}

int budostack_terminal_input_start(void) {
    if (active) {
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &saved_terminal) == -1) {
        perror("terminal input: tcgetattr");
        return -1;
    }
    struct termios raw = saved_terminal;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("terminal input: tcsetattr");
        return -1;
    }
    active = 1;
    interrupted = 0;
    escape_state = 0;
    struct sigaction action = {0};
    action.sa_handler = interrupt_input;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 3; i++) {
        if (sigaction(signals[i], &action, &saved_signals[i]) == -1) {
            perror("terminal input: sigaction");
            budostack_terminal_input_stop();
            return -1;
        }
        installed_signals++;
    }
    if (!cleanup_registered) {
        if (atexit(budostack_terminal_input_stop) != 0) {
            fprintf(stderr, "terminal input: cannot register cleanup\n");
            budostack_terminal_input_stop();
            return -1;
        }
        cleanup_registered = 1;
    }
    printf("\033[?25l\033[0m\033[2J\033[H");
    fflush(stdout);
    return 0;
}

static long escape_age_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - escape_time.tv_sec) * 1000L +
           (now.tv_nsec - escape_time.tv_nsec) / 1000000L;
}

int budostack_terminal_read_key(int timeout_ms) {
    for (;;) {
        if (interrupted) {
            return BUDOSTACK_KEY_EOF;
        }
        if (escape_state && escape_age_ms() >= 500) {
            escape_state = 0;
        }
        struct pollfd input = { STDIN_FILENO, POLLIN, 0 };
        int wait = timeout_ms < 0 ? 50 : timeout_ms;
        if (escape_state && wait > 20) {
            wait = 20;
        }
        int ready = poll(&input, 1, wait);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("terminal input: poll");
            return BUDOSTACK_KEY_EOF;
        }
        if (ready == 0) {
            if (timeout_ms < 0) {
                continue;
            }
            return BUDOSTACK_KEY_NONE;
        }
        unsigned char ch;
        ssize_t count = read(STDIN_FILENO, &ch, 1);
        if (count == 0) {
            escape_state = 0;
            return BUDOSTACK_KEY_EOF;
        }
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            perror("terminal input: read");
            return BUDOSTACK_KEY_EOF;
        }
        if (ch == '\033') {
            escape_state = 1;
            escape_length = 0;
            clock_gettime(CLOCK_MONOTONIC, &escape_time);
            continue;
        }
        if (escape_state == 1) {
            if (ch == '[' || ch == 'O') {
                escape_state = 2;
                continue;
            }
            escape_state = 0;
            return ch;
        }
        if (escape_state == 2) {
            if (ch >= 0x40 && ch <= 0x7e) {
                escape_state = 0;
                switch (ch) {
                    case 'A': return BUDOSTACK_KEY_UP;
                    case 'B': return BUDOSTACK_KEY_DOWN;
                    case 'C': return BUDOSTACK_KEY_RIGHT;
                    case 'D': return BUDOSTACK_KEY_LEFT;
                    default: return BUDOSTACK_KEY_NONE;
                }
            }
            if (++escape_length > 16) {
                escape_state = 0;
                return BUDOSTACK_KEY_NONE;
            }
            continue;
        }
        return ch;
    }
}
