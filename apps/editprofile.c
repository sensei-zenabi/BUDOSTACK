#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "../lib/retroprofile.h"

typedef enum {
    KEY_UNKNOWN = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_CTRL_S,
    KEY_CTRL_Q,
    KEY_OTHER,
    KEY_ENTER,
} Key;

static struct termios original_termios;
static struct termios raw_termios;
static int raw_enabled = 0;

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }

    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_lflag &= ~(IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    /* Preserve newline translation so external terminals keep carriage returns. */
    raw.c_oflag = original_termios.c_oflag;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    raw_termios = raw;
    raw_enabled = 1;
    write(STDOUT_FILENO, "\x1b[?25l", 6);
    atexit(restore_terminal);
}

static void suspend_raw_mode(void) {
    if (!raw_enabled)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

static void resume_raw_mode(void) {
    if (!raw_enabled)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios);
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

static Key read_key(char *out_char) {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return KEY_UNKNOWN;

    if (out_char != NULL)
        *out_char = c;

    if (c == '\x1b') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return KEY_OTHER;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return KEY_OTHER;

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A':
                    return KEY_UP;
                case 'B':
                    return KEY_DOWN;
                case 'C':
                    return KEY_RIGHT;
                case 'D':
                    return KEY_LEFT;
                default:
                    return KEY_OTHER;
            }
        }
        return KEY_OTHER;
    }

    if (c == 19) {
        if (out_char != NULL)
            *out_char = 0;
        return KEY_CTRL_S;
    }
    if (c == 17) {
        if (out_char != NULL)
            *out_char = 0;
        return KEY_CTRL_Q;
    }
    if (c == '\r' || c == '\n') {
        if (out_char != NULL)
            *out_char = 0;
        return KEY_ENTER;
    }

    return KEY_OTHER;
}

static const char *color_roles[16] = {
    "0 canvas (code blocks)",
    "1 plain text / prose",
    "2 control-flow keywords",
    "3 datatype keywords",
    "4 string / char literals",
    "5 numeric literals",
    "6 function identifiers",
    "7 punctuation & brackets",
    "8 preprocessor directives",
    "9 comments / docs",
    "10 markdown headers",
    "11 list bullets/markers",
    "12 markup tags",
    "13 inline code spans",
    "14 bold emphasis",
    "15 italic emphasis",
};

static void copy_profiles(RetroProfile *out, size_t out_count) {
    size_t count = retroprofile_count();
    if (out_count < count)
        count = out_count;
    for (size_t i = 0; i < count; ++i) {
        const RetroProfile *src = retroprofile_get(i);
        if (src == NULL)
            continue;
        out[i] = *src;
    }
}

static int clamp_channel(int value) {
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

static void draw_profile_header(const RetroProfile *profile, size_t profile_index, size_t total, int dirty) {
    char name[25];
    char key[13];
    snprintf(name, sizeof(name), "%.24s", profile->display_name);
    snprintf(key, sizeof(key), "%.12s", profile->key);

    printf("\x1b[2J\x1b[H");
    printf("RetroProfile Editor %zu/%zu  %s [%s]%s\n",
           profile_index + 1,
           total,
           name,
           key,
           dirty ? " *" : "");
    printf("Arrows: move  Tab: next channel  +/-: fine step  </>: coarse step\n");
    printf("p: next  c: copy to defaults  s/Ctrl+S: save  w: write .prf  l: load .prf\n");
    printf("a: apply profile  q/Ctrl+Q: quit (changes apply after restart)\n\n");
}

static void draw_row_prefix(int selected) {
    printf(selected ? "> " : "  ");
}

static void print_color_cell(const RetroColor *color, int selected_channel) {
    printf("#%02X%02X%02X ", color->r, color->g, color->b);
    printf("\x1b[48;2;%u;%u;%um  \x1b[0m ", color->r, color->g, color->b);
    const char labels[3] = {'R', 'G', 'B'};
    const uint8_t components[3] = {color->r, color->g, color->b};
    for (int i = 0; i < 3; ++i) {
        if (i == selected_channel)
            printf("[%c:%3u]", labels[i], components[i]);
        else
            printf(" %c:%3u ", labels[i], components[i]);
        if (i != 2)
            putchar(' ');
    }
}

static void draw_color_line(const char *label, int row_number, const RetroColor *color, int selected_row, int selected_channel) {
    draw_row_prefix(selected_row == row_number);
    printf("%02d %-24.24s ", row_number, label);
    print_color_cell(color, selected_row == row_number ? selected_channel : -1);
    putchar('\n');
}

static void draw_screen(const RetroProfile *profile, size_t profile_index, size_t total_profiles, int selected_row, int selected_channel, const int dirty_flags[4]) {
    draw_profile_header(profile, profile_index, total_profiles, dirty_flags[profile_index]);

    for (int i = 0; i < 16; ++i)
        draw_color_line(color_roles[i], i, &profile->colors[i], selected_row, selected_channel);

    draw_color_line("default foreground", 16, &profile->defaults.foreground, selected_row, selected_channel);
    draw_color_line("default background", 17, &profile->defaults.background, selected_row, selected_channel);
    draw_color_line("cursor highlight", 18, &profile->defaults.cursor, selected_row, selected_channel);

    fflush(stdout);
}

static void adjust_channel(RetroColor *color, int channel, int delta) {
    if (channel < 0 || channel > 2)
        return;
    int value = 0;
    if (channel == 0)
        value = color->r;
    else if (channel == 1)
        value = color->g;
    else
        value = color->b;

    value = clamp_channel(value + delta);

    if (channel == 0)
        color->r = (uint8_t)value;
    else if (channel == 1)
        color->g = (uint8_t)value;
    else
        color->b = (uint8_t)value;
}

static RetroColor *selected_color(RetroProfile *profile, int row) {
    if (row < 0)
        return NULL;
    if (row < 16)
        return &profile->colors[row];
    if (row == 16)
        return &profile->defaults.foreground;
    if (row == 17)
        return &profile->defaults.background;
    if (row == 18)
        return &profile->defaults.cursor;
    return NULL;
}

static int prompt_path(const char *message, char *buffer, size_t buffer_size, const char *default_path) {
    if (buffer == NULL || buffer_size == 0)
        return -1;

    suspend_raw_mode();
    printf("\n%s (default: %s)\n> ", message, default_path);
    fflush(stdout);

    char *result = fgets(buffer, (int)buffer_size, stdin);
    if (result == NULL) {
        resume_raw_mode();
        return -1;
    }

    size_t len = strcspn(buffer, "\r\n");
    buffer[len] = '\0';
    if (buffer[0] == '\0' && default_path != NULL)
        snprintf(buffer, buffer_size, "%s", default_path);

    resume_raw_mode();
    return 0;
}

static void mark_dirty_all(int dirty_flags[4], size_t profile_count) {
    for (size_t i = 0; i < profile_count && i < 4; ++i)
        dirty_flags[i] = 1;
}

int main(void) {
    RetroProfile editable[4];
    int dirty_flags[4] = {0, 0, 0, 0};
    size_t profile_count = retroprofile_count();
    if (profile_count > 4)
        profile_count = 4;

    copy_profiles(editable, profile_count);

    int current_profile = 0;
    int selected_row = 0;
    int selected_channel = 0;
    char last_prf_path[PATH_MAX] = "users/retroprofile.prf";

    enable_raw_mode();
    draw_screen(&editable[current_profile], (size_t)current_profile, profile_count, selected_row, selected_channel, dirty_flags);

    for (;;) {
        char raw_char = 0;
        Key key = read_key(&raw_char);
        if (key == KEY_UNKNOWN)
            continue;

        if (key == KEY_UP) {
            if (selected_row > 0)
                --selected_row;
        } else if (key == KEY_DOWN) {
            if (selected_row < 18)
                ++selected_row;
        } else if (key == KEY_LEFT) {
            if (selected_channel > 0)
                --selected_channel;
        } else if (key == KEY_RIGHT) {
            if (selected_channel < 2)
                ++selected_channel;
        } else if (key == KEY_CTRL_Q) {
            break;
        } else if (key == KEY_CTRL_S) {
            if (retroprofile_save_prf(retroprofile_override_path(), editable, profile_count) == 0) {
                for (size_t i = 0; i < profile_count && i < 4; ++i)
                    dirty_flags[i] = 0;
                printf("\nSaved overrides to %s. Restart to apply.\n", retroprofile_override_path());
            } else {
                printf("\nFailed to save overrides (%s).\n", strerror(errno));
            }
            fflush(stdout);
            continue;
        } else if (key == KEY_ENTER) {
            /* no-op */
        } else if (key == KEY_OTHER) {
            char ch = raw_char;
            if (ch == 'q' || ch == 'Q' || ch == 17) {
                break;
            } else if (ch == 's' || ch == 'S') {
                if (retroprofile_save_prf(retroprofile_override_path(), editable, profile_count) == 0) {
                    for (size_t i = 0; i < profile_count && i < 4; ++i)
                        dirty_flags[i] = 0;
                    printf("\nSaved overrides to %s. Restart to apply.\n", retroprofile_override_path());
                } else {
                    printf("\nFailed to save overrides (%s).\n", strerror(errno));
                }
                fflush(stdout);
                continue;
            } else if (ch == 'w' || ch == 'W') {
                char path[PATH_MAX];
                if (prompt_path("Save .prf file", path, sizeof(path), last_prf_path) == 0) {
                    if (retroprofile_save_prf(path, editable, profile_count) == 0) {
                        snprintf(last_prf_path, sizeof(last_prf_path), "%s", path);
                        printf("\nSaved presets to %s. Restart to apply.\n", path);
                    } else {
                        printf("\nFailed to save %s (%s).\n", path, strerror(errno));
                    }
                }
                fflush(stdout);
                continue;
            } else if (ch == 'l' || ch == 'L') {
                char path[PATH_MAX];
                if (prompt_path("Load .prf file", path, sizeof(path), last_prf_path) == 0) {
                    if (retroprofile_load_prf(path, editable, profile_count) == 0) {
                        snprintf(last_prf_path, sizeof(last_prf_path), "%s", path);
                        mark_dirty_all(dirty_flags, profile_count);
                        printf("\nLoaded presets from %s. Save overrides to apply after restart.\n", path);
                    } else {
                        printf("\nFailed to load %s (%s).\n", path, strerror(errno));
                    }
                }
                fflush(stdout);
                continue;
            } else if (ch == 'a' || ch == 'A') {
                const RetroProfile *current = &editable[current_profile];
                if (retroprofile_set_active(current->key) == 0)
                    printf("\nApplied active profile: %s. Restart to see it.\n", current->key);
                else
                    printf("\nFailed to set active profile (%s).\n", strerror(errno));
                fflush(stdout);
                continue;
            } else if (ch == '+') {
                RetroColor *color = selected_color(&editable[current_profile], selected_row);
                if (color) {
                    adjust_channel(color, selected_channel, 1);
                    dirty_flags[current_profile] = 1;
                }
            } else if (ch == '-') {
                RetroColor *color = selected_color(&editable[current_profile], selected_row);
                if (color) {
                    adjust_channel(color, selected_channel, -1);
                    dirty_flags[current_profile] = 1;
                }
            } else if (ch == '>') {
                RetroColor *color = selected_color(&editable[current_profile], selected_row);
                if (color) {
                    adjust_channel(color, selected_channel, 10);
                    dirty_flags[current_profile] = 1;
                }
            } else if (ch == '<') {
                RetroColor *color = selected_color(&editable[current_profile], selected_row);
                if (color) {
                    adjust_channel(color, selected_channel, -10);
                    dirty_flags[current_profile] = 1;
                }
            } else if (ch == 'p' || ch == 'P') {
                current_profile = (current_profile + 1) % (int)profile_count;
                selected_row = 0;
                selected_channel = 0;
            } else if (ch == '\t') {
                selected_channel = (selected_channel + 1) % 3;
            } else if (ch == 'c' || ch == 'C') {
                if (selected_row < 16) {
                    RetroColor source = editable[current_profile].colors[selected_row];
                    editable[current_profile].defaults.foreground = source;
                    editable[current_profile].defaults.background = source;
                    editable[current_profile].defaults.cursor = source;
                    dirty_flags[current_profile] = 1;
                }
            }
        }

        draw_screen(&editable[current_profile], (size_t)current_profile, profile_count, selected_row, selected_channel, dirty_flags);
    }

    printf("\nNo changes applied to running session. Restart to see new palettes.\n");
    return EXIT_SUCCESS;
}

