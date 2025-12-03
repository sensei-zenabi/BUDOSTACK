#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_original;
static int g_termios_saved = 0;
static int g_original_flags = -1;

#define MAX_EVENTS 20

static void restore_terminal(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_original);
    }
    if (g_original_flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, g_original_flags);
    }
}

static int enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &g_original) == -1) {
        perror("_TERM_KEYBOARD: tcgetattr");
        return -1;
    }
    g_termios_saved = 1;

    struct termios raw = g_original;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("_TERM_KEYBOARD: tcsetattr");
        return -1;
    }

    g_original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_original_flags == -1) {
        perror("_TERM_KEYBOARD: fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, g_original_flags | O_NONBLOCK) == -1) {
        perror("_TERM_KEYBOARD: fcntl(F_SETFL)");
        return -1;
    }

    if (atexit(restore_terminal) != 0) {
        fprintf(stderr, "_TERM_KEYBOARD: failed to register cleanup\n");
        return -1;
    }

    return 0;
}

static int append_byte(unsigned char **buffer, size_t *len, size_t *cap, unsigned char byte) {
    if (*len + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 64 : *cap * 2;
        unsigned char *tmp = (unsigned char *)realloc(*buffer, new_cap);
        if (!tmp) {
            perror("realloc");
            return -1;
        }
        *buffer = tmp;
        *cap = new_cap;
    }
    (*buffer)[(*len)++] = byte;
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
            for (ssize_t i = 0; i < rd; ++i) {
                if (append_byte(&buffer, &len, &cap, chunk[i]) != 0) {
                    free(buffer);
                    return -1;
                }
            }
            continue;
        }
        if (rd == 0 || (rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            break;
        }
        if (rd == -1 && errno == EINTR) {
            continue;
        }
        perror("_TERM_KEYBOARD: read");
        free(buffer);
        return -1;
    }

    *out_buffer = buffer;
    return (ssize_t)len;
}

static const char *map_csi_numeric(int number) {
    switch (number) {
        case 1:
            return "HOME";
        case 2:
            return "INSERT";
        case 3:
            return "DELETE";
        case 4:
            return "END";
        case 5:
            return "PAGE_UP";
        case 6:
            return "PAGE_DOWN";
        case 11:
            return "F1";
        case 12:
            return "F2";
        case 13:
            return "F3";
        case 14:
            return "F4";
        case 15:
            return "F5";
        case 17:
            return "F6";
        case 18:
            return "F7";
        case 19:
            return "F8";
        case 20:
            return "F9";
        case 21:
            return "F10";
        case 23:
            return "F11";
        case 24:
            return "F12";
        default:
            return NULL;
    }
}

static const char *decode_escape(const unsigned char *buf, size_t len, size_t *consumed) {
    if (len == 0) {
        *consumed = 0;
        return "ESC";
    }

    if (buf[0] == '[') {
        if (len >= 2) {
            switch (buf[1]) {
                case 'A':
                    *consumed = 2;
                    return "UP_ARROW";
                case 'B':
                    *consumed = 2;
                    return "DOWN_ARROW";
                case 'C':
                    *consumed = 2;
                    return "RIGHT_ARROW";
                case 'D':
                    *consumed = 2;
                    return "LEFT_ARROW";
                case 'H':
                    *consumed = 2;
                    return "HOME";
                case 'F':
                    *consumed = 2;
                    return "END";
                default:
                    break;
            }
        }
        size_t idx = 1;
        int number = 0;
        while (idx < len && isdigit((unsigned char)buf[idx])) {
            number = number * 10 + (buf[idx] - '0');
            idx++;
        }
        if (idx < len && buf[idx] == '~') {
            const char *mapped = map_csi_numeric(number);
            *consumed = idx + 1;
            return mapped ? mapped : "ESC";
        }
    } else if (buf[0] == 'O' && len >= 2) {
        switch (buf[1]) {
            case 'P':
                *consumed = 2;
                return "F1";
            case 'Q':
                *consumed = 2;
                return "F2";
            case 'R':
                *consumed = 2;
                return "F3";
            case 'S':
                *consumed = 2;
                return "F4";
            case 'A':
                *consumed = 2;
                return "UP_ARROW";
            case 'B':
                *consumed = 2;
                return "DOWN_ARROW";
            case 'C':
                *consumed = 2;
                return "RIGHT_ARROW";
            case 'D':
                *consumed = 2;
                return "LEFT_ARROW";
            default:
                break;
        }
    }

    *consumed = 1;
    return "ESC";
}

static int append_event(const char *name, char *events[], size_t *count) {
    if (!name || !*name || !events || !count) {
        return 0;
    }

    if (*count >= MAX_EVENTS) {
        free(events[0]);
        memmove(&events[0], &events[1], (MAX_EVENTS - 1) * sizeof(char *));
        *count = MAX_EVENTS - 1;
    }

    char *dup = strdup(name);
    if (!dup) {
        perror("strdup");
        return -1;
    }

    events[*count] = dup;
    (*count)++;
    return 0;
}

static int process_bytes(const unsigned char *data, size_t len, char *events[], size_t *count) {
    size_t i = 0;
    while (i < len) {
        unsigned char b = data[i];
        if (b == 27) {
            size_t consumed = 0;
            const char *name = decode_escape(&data[i + 1], len - i - 1, &consumed);
            if (append_event(name, events, count) != 0) {
                return -1;
            }
            i += 1 + consumed;
            continue;
        }

        if (b == '\n' || b == '\r') {
            if (append_event("ENTER", events, count) != 0) {
                return -1;
            }
        } else if (b == '\t') {
            if (append_event("TAB", events, count) != 0) {
                return -1;
            }
        } else if (b == ' ') {
            if (append_event("SPACE", events, count) != 0) {
                return -1;
            }
        } else if (b == 127 || b == 8) {
            if (append_event("BACKSPACE", events, count) != 0) {
                return -1;
            }
        } else if (b >= '0' && b <= '9') {
            char out[2] = { (char)b, '\0' };
            if (append_event(out, events, count) != 0) {
                return -1;
            }
        } else if (isalpha(b)) {
            char out[2] = { (char)toupper(b), '\0' };
            if (append_event(out, events, count) != 0) {
                return -1;
            }
        } else if (b == 3) {
            if (append_event("CTRL_C", events, count) != 0) {
                return -1;
            }
        } else if (isprint(b)) {
            char out[2] = { (char)b, '\0' };
            if (append_event(out, events, count) != 0) {
                return -1;
            }
        }
        i++;
    }
    return 0;
}

static void print_help(void) {
    printf("_TERM_KEYBOARD\n");
    printf("Capture up to %d key presses since the last invocation and return them\n", MAX_EVENTS);
    printf("as a TASK array literal. Intended for use from TASK scripts via\n");
    printf("  RUN _TERM_KEYBOARD TO $EVENT_ARRAY\n\n");
    printf("Names:\n");
    printf("  Letters: A-Z  Digits: 0-9\n");
    printf("  ENTER, SPACE, TAB, BACKSPACE, ESC, CTRL_C\n");
    printf("  Arrows: UP_ARROW, DOWN_ARROW, LEFT_ARROW, RIGHT_ARROW\n");
    printf("  Function keys: F1-F12\n");
    printf("  Navigation: HOME, END, PAGE_UP, PAGE_DOWN, INSERT, DELETE\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (enable_raw_mode() != 0) {
        return 1;
    }

    unsigned char *buffer = NULL;
    ssize_t len = read_all_bytes(&buffer);
    if (len < 0) {
        free(buffer);
        return 1;
    }

    char *events[MAX_EVENTS] = { 0 };
    size_t event_count = 0;

    if (len > 0) {
        if (process_bytes(buffer, (size_t)len, events, &event_count) != 0) {
            for (size_t i = 0; i < MAX_EVENTS; ++i) {
                free(events[i]);
            }
            free(buffer);
            return 1;
        }
    }

    printf("{");
    for (size_t i = 0; i < event_count; ++i) {
        if (i > 0) {
            printf(", ");
        }
        printf("%s", events[i]);
    }
    printf("}\n");

    for (size_t i = 0; i < MAX_EVENTS; ++i) {
        free(events[i]);
    }
    free(buffer);
    return 0;
}
