#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_original;
static int g_termios_saved = 0;
static int g_original_flags = -1;

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

struct OutputBuffer {
    char *data;
    size_t length;
    size_t capacity;
};

enum KeyStateBits {
    KEYSTATE_LEFT = 1u << 0,
    KEYSTATE_RIGHT = 1u << 1,
    KEYSTATE_UP = 1u << 2,
    KEYSTATE_DOWN = 1u << 3,
    KEYSTATE_ACTION = 1u << 4,
    KEYSTATE_EXIT = 1u << 5
};

static int append_output(struct OutputBuffer *out, const char *text, bool csv_mode) {
    if (!out || !text) {
        return 0;
    }

    size_t text_len = strlen(text);
    size_t extra = text_len + (csv_mode && out->length > 0 ? 1 : 0) + 1;
    if (out->length + extra > out->capacity) {
        size_t new_capacity = out->capacity == 0 ? 128 : out->capacity * 2;
        while (new_capacity < out->length + extra) {
            new_capacity *= 2;
        }
        char *tmp = realloc(out->data, new_capacity);
        if (!tmp) {
            perror("realloc");
            return -1;
        }
        out->data = tmp;
        out->capacity = new_capacity;
    }

    if (csv_mode && out->length > 0) {
        out->data[out->length++] = ',';
    }
    memcpy(out->data + out->length, text, text_len);
    out->length += text_len;
    out->data[out->length] = '\0';
    return 0;
}

static void update_state(const char *name, unsigned int *state_bits) {
    if (!name || !state_bits) {
        return;
    }

    if (strcmp(name, "LEFT_ARROW") == 0 || strcmp(name, "A") == 0) {
        *state_bits |= KEYSTATE_LEFT;
    } else if (strcmp(name, "RIGHT_ARROW") == 0 || strcmp(name, "D") == 0) {
        *state_bits |= KEYSTATE_RIGHT;
    } else if (strcmp(name, "UP_ARROW") == 0 || strcmp(name, "W") == 0) {
        *state_bits |= KEYSTATE_UP;
    } else if (strcmp(name, "DOWN_ARROW") == 0 || strcmp(name, "S") == 0) {
        *state_bits |= KEYSTATE_DOWN;
    } else if (strcmp(name, "SPACE") == 0 || strcmp(name, "ENTER") == 0) {
        *state_bits |= KEYSTATE_ACTION;
    } else if (strcmp(name, "ESC") == 0 || strcmp(name, "CTRL_C") == 0) {
        *state_bits |= KEYSTATE_EXIT;
    }
}

static int process_event(const char *name, struct OutputBuffer *out, bool csv_mode, unsigned int *state_bits) {
    if (!name || !*name) {
        return 0;
    }

    if (append_output(out, name, csv_mode) != 0) {
        return -1;
    }

    update_state(name, state_bits);
    if (!csv_mode) {
        if (append_output(out, "\n", false) != 0) {
            return -1;
        }
    }
    return 0;
}

static int process_bytes(const unsigned char *data, size_t len, struct OutputBuffer *out, bool csv_mode,
                         unsigned int *state_bits) {
    size_t i = 0;
    while (i < len) {
        unsigned char b = data[i];
        if (b == 27) {
            size_t consumed = 0;
            const char *name = decode_escape(&data[i + 1], len - i - 1, &consumed);
            if (process_event(name, out, csv_mode, state_bits) != 0) {
                return -1;
            }
            i += 1 + consumed;
            continue;
        }

        const char *name = NULL;
        if (b == '\n' || b == '\r') {
            name = "ENTER";
        } else if (b == '\t') {
            name = "TAB";
        } else if (b == ' ') {
            name = "SPACE";
        } else if (b == 127 || b == 8) {
            name = "BACKSPACE";
        } else if (b >= '0' && b <= '9') {
            static char outbuf[2];
            outbuf[0] = (char)b;
            outbuf[1] = '\0';
            name = outbuf;
        } else if (isalpha(b)) {
            static char outbuf[2];
            outbuf[0] = (char)toupper(b);
            outbuf[1] = '\0';
            name = outbuf;
        } else if (b == 3) {
            name = "CTRL_C";
        } else if (isprint(b)) {
            static char outbuf[2];
            outbuf[0] = (char)b;
            outbuf[1] = '\0';
            name = outbuf;
        }

        if (name) {
            if (process_event(name, out, csv_mode, state_bits) != 0) {
                return -1;
            }
        }
        i++;
    }
    return 0;
}

static void print_help(void) {
    printf("_TERM_KEYBOARD\n");
    printf("Capture all key presses since the last invocation.\n");
    printf("Options:\n");
    printf("  --csv     Combine events onto a single comma-separated line.\n");
    printf("  --state   Output a bitfield summarizing held keys (arrows, WASD, SPACE/ENTER, ESC/CTRL+C).\n");
    printf("Names:\n");
    printf("  Letters: A-Z  Digits: 0-9\n");
    printf("  ENTER, SPACE, TAB, BACKSPACE, ESC, CTRL_C\n");
    printf("  Arrows: UP_ARROW, DOWN_ARROW, LEFT_ARROW, RIGHT_ARROW\n");
    printf("  Function keys: F1-F12\n");
    printf("  Navigation: HOME, END, PAGE_UP, PAGE_DOWN, INSERT, DELETE\n");
}

int main(int argc, char **argv) {
    bool csv_mode = false;
    bool state_mode = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv_mode = true;
        } else if (strcmp(argv[i], "--state") == 0) {
            state_mode = true;
        } else {
            fprintf(stderr, "_TERM_KEYBOARD: unknown argument '%s'\n", argv[i]);
            print_help();
            return 1;
        }
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

    struct OutputBuffer out = { 0 };
    unsigned int state_bits = 0u;

    if (len > 0) {
        if (process_bytes(buffer, (size_t)len, &out, csv_mode || state_mode, &state_bits) != 0) {
            free(out.data);
            free(buffer);
            return 1;
        }
    }

    if (state_mode) {
        printf("%u\n", state_bits);
    } else if (out.length > 0) {
        fputs(out.data, stdout);
    }

    free(out.data);
    free(buffer);
    return 0;
}
