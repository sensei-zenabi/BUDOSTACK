#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_original_termios;
static int g_termios_saved = 0;

static void restore_terminal(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    }
}

static int read_byte(unsigned char *out) {
    while (1) {
        ssize_t result = read(STDIN_FILENO, out, 1);
        if (result == 1) {
            return 1;
        }
        if (result == 0) {
            return 0;
        }
        if (result == -1 && errno == EINTR) {
            continue;
        }
        return -1;
    }
}

int main(void) {
    if (tcgetattr(STDIN_FILENO, &g_original_termios) == -1) {
        perror("_KEYS: tcgetattr");
        return EXIT_FAILURE;
    }

    g_termios_saved = 1;
    if (atexit(restore_terminal) != 0) {
        fprintf(stderr, "_KEYS: failed to register terminal cleanup\n");
        return EXIT_FAILURE;
    }

    struct termios raw = g_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /* 100ms timeout when expecting escape sequence bytes */

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("_KEYS: tcsetattr");
        return EXIT_FAILURE;
    }

    fflush(stdout);

    for (;;) {
        unsigned char ch = 0;
        int status = read_byte(&ch);
        if (status == -1) {
            perror("_KEYS: read");
            return EXIT_FAILURE;
        } else if (status == 0) {
            continue;
        }

        int value = 0;

        if (ch == '\n' || ch == '\r') {
            value = 3; /* Enter */
        } else if (ch == '\t') {
            value = 5; /* Tab */
        } else if (ch == ' ') {
            value = 4; /* Space */
        } else if (ch == 127 || ch == 8) {
            value = 6; /* Backspace/Delete */
        } else if (ch == 27) {
            unsigned char seq0 = 0;
            status = read_byte(&seq0);
            if (status == -1) {
                perror("_KEYS: read");
                return EXIT_FAILURE;
            }
            if (status == 0) {
                value = 10; /* ESC pressed alone */
            } else if (seq0 != '[') {
                value = 10; /* ESC + non-arrow sequence */
            } else {
                unsigned char seq1 = 0;
                status = read_byte(&seq1);
                if (status == -1) {
                    perror("_KEYS: read");
                    return EXIT_FAILURE;
                }
                if (status == 0) {
                    value = 10;
                } else {
                    switch (seq1) {
                        case 'A':
                            value = 2; /* Up arrow */
                            break;
                        case 'B':
                            value = -2; /* Down arrow */
                            break;
                        case 'C':
                            value = 1; /* Right arrow */
                            break;
                        case 'D':
                            value = -1; /* Left arrow */
                            break;
                        default:
                            value = 10;
                            break;
                    }
                }
            }
        }

        if (value != 0) {
            printf("%d\n", value);
            fflush(stdout);
            break;
        }
    }

    return EXIT_SUCCESS;
}

