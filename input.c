#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <wchar.h>
#include <locale.h>
#include <limits.h>
#include "input.h"

#define INPUT_SIZE 1024
#define MAX_HISTORY 100  /* Maximum number of commands to store in history */

/* List of available commands for autocomplete */
static const char *commands[] = {
	"help",
	"run",   // NEW: Added "run" command for executing arbitrary shell input.
    "exit"
};

static const int num_commands = sizeof(commands) / sizeof(commands[0]);

/* Helper function prototypes */
struct completion_state {
    int active;
    size_t token_start;
    size_t token_end;
    int used_filenames;
    char quote_char;
    char **matches;
    size_t match_count;
    size_t index;
};

static int utf8_string_display_width(const char *s);
static int utf8_display_width_range(const char *buffer, size_t start, size_t end);
static size_t utf8_prev_char_start(const char *buffer, size_t cursor);
static size_t utf8_next_char_start(const char *buffer, size_t cursor, size_t length);
static size_t utf8_sequence_length(unsigned char first_byte);
static size_t utf8_read_sequence(int first_byte, char *dst, size_t dst_size);
static void redraw_from_cursor(const char *buffer, size_t cursor, int clear_extra_space);
static void move_to_end_of_line(const char *buffer, size_t *cursor, size_t pos);
static void clear_line_contents(const char *buffer, size_t *pos, size_t *cursor);
static char *system_clipboard_read(void);
static void insert_text_at_cursor(const char *text, char *buffer, size_t *pos, size_t *cursor);
static size_t sanitize_text_input(const char *src, char *dest, size_t dest_size);
static size_t find_token_start(const char *buffer, size_t pos);
static void unescape_token(const char *src, char *dest, size_t dest_size);
static void escape_token(const char *src, char *dest, size_t dest_size);
static char **collect_command_matches(const char *token, size_t *match_count);
static char **collect_filename_matches(const char *token, size_t *match_count);
static void clear_completion_state(struct completion_state *state);
static void format_completion(const char *completion, int used_filenames, char quote_char,
                              char *formatted, size_t formatted_size);

/*
 * read_input()
 *
 * Modified to include a fixed-size history buffer for command history.
 * Features:
 * - Raw mode input handling (non-canonical, no echo)
 * - Up/Down arrow keys to navigate through previously entered commands
 * - Left/Right arrow keys for in-line cursor movement
 * - TAB key for autocomplete (command or filename based on position)
 *
 * Design principles:
 * - Separation of Concerns: History management, input reading, and display updates are handled here.
 * - Memory Management: Uses a fixed-size history buffer and shifts entries when full.
 * - Usability: Provides immediate feedback by replacing the current input with history commands.
 */
char* read_input(void) {
    static char buffer[INPUT_SIZE];
    size_t pos = 0;        // End of current input
    size_t cursor = 0;     // Current cursor position within buffer
    struct termios oldt, newt;
    int in_paste_mode = 0;
    static struct completion_state completion_state = {0};

    /* Static history storage */
    static char *history[MAX_HISTORY] = {0};
    static int history_count = 0;
    static int history_index = 0;

    /* Get current terminal settings and disable canonical mode and echo */
    if (tcgetattr(STDIN_FILENO, &oldt) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    newt = oldt;
    newt.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    newt.c_cflag |= CS8;
    newt.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
    newt.c_cc[VMIN] = 1;
    newt.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    /* Enable bracketed paste so pasted text is wrapped in ESC[200~ ... ESC[201~ */
    if (write(STDOUT_FILENO, "\x1b[?2004h", 9) == -1) {
        perror("write");
    }

    memset(buffer, 0, sizeof(buffer));
    fflush(stdout);

    while (1) {
        int c = getchar();
        if ((c == '\n' || c == '\r') && !in_paste_mode) {
            clear_completion_state(&completion_state);
            putchar('\n');
            break;
        }
        /* Handle escape sequences for arrow keys (command history navigation) */
        else if (c == '\033') {
            clear_completion_state(&completion_state);
            int next1 = getchar();
            if (next1 == '[') {
                int next2 = getchar();
                if (next2 == '2') {
                    int next3 = getchar();
                    if (next3 == '0') {
                        int next4 = getchar();
                        if (next4 == '0' || next4 == '1') {
                            int next5 = getchar();
                            if (next5 == '~') {
                                in_paste_mode = (next4 == '0');
                                continue;
                            }
                            ungetc(next5, stdin);
                        }
                        ungetc(next4, stdin);
                    }
                    ungetc(next3, stdin);
                }
                if (next2 == 'A') { /* Up arrow */
                    if (history_count > 0 && history_index > 0) {
                                                move_to_end_of_line(buffer, &cursor, pos);
						clear_line_contents(buffer, &pos, &cursor);
						history_index--;
                        strcpy(buffer, history[history_index]);
                        pos = strlen(buffer);
                        cursor = pos;
                        printf("%s", buffer);
                        fflush(stdout);
                    }
                    continue;
                } else if (next2 == 'B') { /* Down arrow */
                    if (history_count > 0 && history_index < history_count - 1) {
                        move_to_end_of_line(buffer, &cursor, pos);
                        clear_line_contents(buffer, &pos, &cursor);

                        // Load next history entry
                        history_index++;
                        strcpy(buffer, history[history_index]);
                        pos = strlen(buffer);
                        cursor = pos;
                        printf("%s", buffer);
                        fflush(stdout);
                    } else if (history_count > 0 && history_index == history_count - 1) {
                        move_to_end_of_line(buffer, &cursor, pos);
                        clear_line_contents(buffer, &pos, &cursor);

                        // Go to "empty buffer" after history
                        history_index = history_count;
                        buffer[0] = '\0';
                        pos = 0;
                        cursor = 0;
                        fflush(stdout);
                    }
                    continue;
                } else if (next2 == 'C') { /* Right arrow */
                    if (cursor < pos) {
                        size_t next = utf8_next_char_start(buffer, cursor, pos);
                        fwrite(buffer + cursor, 1, next - cursor, stdout);
                        cursor = next;
                    }
                    continue;
                } else if (next2 == 'D') { /* Left arrow */
                    if (cursor > 0) {
                        size_t prev = utf8_prev_char_start(buffer, cursor);
                        int move_width = utf8_display_width_range(buffer, prev, cursor);

                        // FIX: Left arrow should ONLY move the cursor left,
                        // not delete characters. So we print just '\b' and
                        // DO NOT print " \b".
                        //
                        // This way:
                        //  - All text stays visible on the line
                        //  - Cursor moves left across characters (and across wrapped lines)
                        for (int i = 0; i < move_width; i++) {
                            printf("\b");
                        }

                        cursor = prev;
                    }
                    continue;
                } else if (next2 == '3') { /* Delete key sequence: ESC [ 3 ~ */
                    int next3 = getchar();
                    if (next3 == '~') {
                        if (cursor < pos) {
                            size_t next = utf8_next_char_start(buffer, cursor, pos);
                            size_t removed_bytes = next - cursor;
                            memmove(buffer + cursor, buffer + next, pos - next + 1);
                            pos -= removed_bytes;
                            redraw_from_cursor(buffer, cursor, 1);
                        }
                        continue;
                    }
                }
                /* Ignore other escape sequences */
            }
        }
        /* TAB pressed: trigger autocomplete */
        else if (c == '\t') {
            /* Find beginning of current token */
            size_t token_start = find_token_start(buffer, cursor);
            char token[INPUT_SIZE];
            size_t token_len = cursor - token_start;
            strncpy(token, buffer + token_start, token_len);
            token[token_len] = '\0';
            if (token_len == 0)
                continue; /* Nothing to complete */

            char quote_char = '\0';
            size_t content_start = 0;
            size_t content_len = token_len;
            if (token[0] == '"' || token[0] == '\'') {
                quote_char = token[0];
                content_start = 1;
                if (token_len > 1 && token[token_len - 1] == quote_char) {
                    content_len = token_len - 1;
                }
            }

            char raw_token[INPUT_SIZE];
            if (content_start >= token_len) {
                continue;
            }
            {
                char unescaped[INPUT_SIZE];
                size_t copy_len = content_len - content_start;
                if (copy_len >= sizeof(unescaped)) {
                    copy_len = sizeof(unescaped) - 1;
                }
                memcpy(unescaped, token + content_start, copy_len);
                unescaped[copy_len] = '\0';
                unescape_token(unescaped, raw_token, sizeof(raw_token));
            }

            if (completion_state.active && completion_state.token_start == token_start &&
                completion_state.token_end == cursor) {
                if (completion_state.match_count > 0) {
                    completion_state.index =
                        (completion_state.index + 1u) % completion_state.match_count;
                }
            } else {
                clear_completion_state(&completion_state);
                if (token_start == 0) {
                    completion_state.matches = collect_command_matches(token,
                                                                      &completion_state.match_count);
                    if (completion_state.match_count > 0) {
                        completion_state.used_filenames = 0;
                    } else {
                        completion_state.matches =
                            collect_filename_matches(raw_token, &completion_state.match_count);
                        completion_state.used_filenames = 1;
                    }
                } else {
                    completion_state.matches =
                        collect_filename_matches(raw_token, &completion_state.match_count);
                    completion_state.used_filenames = 1;
                }
                completion_state.token_start = token_start;
                completion_state.token_end = cursor;
                completion_state.quote_char = quote_char;
                completion_state.index = 0;
                completion_state.active = completion_state.match_count > 1;
            }

            if (completion_state.match_count > 0) {
                char formatted[INPUT_SIZE * 2];
                format_completion(completion_state.matches[completion_state.index],
                                  completion_state.used_filenames,
                                  completion_state.quote_char,
                                  formatted,
                                  sizeof(formatted));
                size_t comp_len = strlen(formatted);
                size_t tail_len = pos - cursor;
                int old_line_width = utf8_display_width_range(buffer, 0, pos);
                int erase_width = utf8_display_width_range(buffer, token_start, cursor);
                for (int i = 0; i < erase_width; i++) {
                    printf("\b");
                }
                if (token_start + comp_len + tail_len >= INPUT_SIZE) {
                    size_t available = INPUT_SIZE - 1u - token_start - tail_len;
                    if (available == 0) {
                        continue;
                    }
                    comp_len = available;
                    formatted[comp_len] = '\0';
                }
                memmove(buffer + token_start + comp_len, buffer + cursor, tail_len + 1);
                memcpy(buffer + token_start, formatted, comp_len);
                pos = token_start + comp_len + tail_len;
                cursor = token_start + comp_len;

                fwrite(formatted, 1, comp_len, stdout);
                fwrite(buffer + cursor, 1, pos - cursor, stdout);

                int new_line_width = utf8_display_width_range(buffer, 0, pos);
                int clear_width = old_line_width - new_line_width;
                if (clear_width > 0) {
                    for (int i = 0; i < clear_width; i++) {
                        printf(" ");
                    }
                }
                int tail_width = utf8_display_width_range(buffer, cursor, pos);
                int move_back = tail_width + (clear_width > 0 ? clear_width : 0);
                for (int i = 0; i < move_back; i++) {
                    printf("\b");
                }
                fflush(stdout);
            }
        }
        /* Handle backspace */
        else if (c == 127 || c == 8) {
            clear_completion_state(&completion_state);
            if (cursor > 0) {
                size_t char_start = utf8_prev_char_start(buffer, cursor);
                size_t removed_bytes = cursor - char_start;
                size_t copy_len = removed_bytes;
                if (copy_len > MB_LEN_MAX)
                    copy_len = MB_LEN_MAX;
                char removed[MB_LEN_MAX + 1];
                memcpy(removed, buffer + char_start, copy_len);
                removed[copy_len] = '\0';
                int removed_width = utf8_string_display_width(removed);
                memmove(buffer + char_start, buffer + cursor, pos - cursor + 1);
                cursor = char_start;
                pos -= removed_bytes;
                for (int i = 0; i < removed_width; i++) {
                    printf("\b");
                }
                redraw_from_cursor(buffer, cursor, 1);
            }
        }
        /* Paste clipboard with Ctrl+V */
        else if (c == 0x16) {
            clear_completion_state(&completion_state);
            char *clipboard = system_clipboard_read();
            if (clipboard) {
                insert_text_at_cursor(clipboard, buffer, &pos, &cursor);
                free(clipboard);
            }
        }
        /* Regular character input */
        else {
            clear_completion_state(&completion_state);
            char utf8_seq[MB_LEN_MAX];
            size_t seq_len = utf8_read_sequence(c, utf8_seq, sizeof(utf8_seq));
            if (seq_len == 0) {
                continue;
            }
            if (seq_len == 1) {
                unsigned char byte = (unsigned char)utf8_seq[0];
                if (byte == '\t') {
                    utf8_seq[0] = ' ';
                } else if (byte < 0x20u || byte == 0x7Fu) {
                    continue;
                }
            }
            if (pos + seq_len >= INPUT_SIZE) {
                continue;
            }
            memmove(buffer + cursor + seq_len, buffer + cursor, pos - cursor + 1);
            memcpy(buffer + cursor, utf8_seq, seq_len);
            pos += seq_len;
            cursor += seq_len;
            fwrite(utf8_seq, 1, seq_len, stdout);
            redraw_from_cursor(buffer, cursor, 0);
        }
    }
    buffer[pos] = '\0';

    /* Disable bracketed paste and restore terminal settings */
    if (write(STDOUT_FILENO, "\x1b[?2004l", 9) == -1) {
        perror("write");
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &oldt) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    /* Add nonempty command to history */
    if (strlen(buffer) > 0) {
        if (history_count == MAX_HISTORY) {
            free(history[0]);
            for (int i = 1; i < MAX_HISTORY; i++) {
                history[i - 1] = history[i];
            }
            history_count--;
        }
        history[history_count] = strdup(buffer);
        if (!history[history_count]) {
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        history_count++;
        history_index = history_count; // Reset history index for new input
    }

    /* Return a duplicate of the buffer (caller must free it) */
    return strdup(buffer);
}

static void redraw_from_cursor(const char *buffer, size_t cursor, int clear_extra_space) {
    const char *tail = buffer + cursor;
    int tail_width = utf8_string_display_width(tail);
    printf("%s", tail);
    if (clear_extra_space) {
        printf(" ");
    }
    int move_back = tail_width + (clear_extra_space ? 1 : 0);
    for (int i = 0; i < move_back; i++) {
        printf("\b");
    }
    fflush(stdout);
}

static size_t utf8_sequence_length(unsigned char first_byte) {
    if (first_byte < 0x80u) {
        return 1u;
    }
    if (first_byte >= 0xC2u && first_byte <= 0xDFu) {
        return 2u;
    }
    if (first_byte >= 0xE0u && first_byte <= 0xEFu) {
        return 3u;
    }
    if (first_byte >= 0xF0u && first_byte <= 0xF4u) {
        return 4u;
    }
    return 1u;
}

static size_t utf8_read_sequence(int first_byte, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) {
        return 0u;
    }
    unsigned char first = (unsigned char)first_byte;
    dst[0] = (char)first;
    size_t expected = utf8_sequence_length(first);
    size_t have = 1u;
    while (have < expected && have < dst_size) {
        int next = getchar();
        if (next == EOF) {
            break;
        }
        unsigned char next_byte = (unsigned char)next;
        if ((next_byte & 0xC0u) != 0x80u) {
            ungetc(next, stdin);
            break;
        }
        dst[have++] = (char)next_byte;
    }
    return have;
}

static size_t utf8_next_char_start(const char *buffer, size_t cursor, size_t length) {
    if (!buffer || cursor >= length) {
        return length;
    }
    size_t index = cursor + 1u;
    while (index < length && ((unsigned char)buffer[index] & 0xC0u) == 0x80u) {
        index++;
    }
    return index;
}

static char *system_clipboard_read(void) {
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!fp) {
        return NULL;
    }

    size_t cap = 256u;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }

    size_t len = 0u;
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2u;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
        }
        buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

static void insert_text_at_cursor(const char *text, char *buffer, size_t *pos, size_t *cursor) {
    if (!text || !buffer || !pos || !cursor) {
        return;
    }

    char sanitized[INPUT_SIZE];
    size_t text_len = sanitize_text_input(text, sanitized, sizeof(sanitized));
    if (text_len == 0) {
        return;
    }

    size_t available = INPUT_SIZE - 1u - *pos;
    if (text_len > available) {
        text_len = available;
    }
    if (text_len == 0) {
        return;
    }

    memmove(buffer + *cursor + text_len, buffer + *cursor, *pos - *cursor + 1);
    memcpy(buffer + *cursor, sanitized, text_len);
    *pos += text_len;
    *cursor += text_len;

    fwrite(sanitized, 1, text_len, stdout);
    redraw_from_cursor(buffer, *cursor, 0);
}

static size_t find_token_start(const char *buffer, size_t pos) {
    size_t token_start = 0;
    int in_quotes = 0;
    char quote_char = '\0';
    size_t i = 0;
    while (i < pos) {
        char c = buffer[i];
        if (!in_quotes && c == '\\') {
            if (i + 1 < pos) {
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        if (c == '"' || c == '\'') {
            if (in_quotes && c == quote_char) {
                in_quotes = 0;
                quote_char = '\0';
            } else if (!in_quotes) {
                in_quotes = 1;
                quote_char = c;
            }
            i++;
            continue;
        }
        if (!in_quotes && (c == ' ' || c == '\t')) {
            token_start = i + 1;
        }
        i++;
    }
    return token_start;
}

static void unescape_token(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dest_size; si++) {
        if (src[si] == '\\' && src[si + 1] != '\0') {
            dest[di++] = src[si + 1];
            si++;
        } else {
            dest[di++] = src[si];
        }
    }
    dest[di] = '\0';
}

static void escape_token(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dest_size; si++) {
        char c = src[si];
        if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '\\') {
            if (di + 2 >= dest_size) {
                break;
            }
            dest[di++] = '\\';
        }
        dest[di++] = c;
    }
    dest[di] = '\0';
}

static int utf8_display_width_range(const char *buffer, size_t start, size_t end) {
    if (!buffer || end <= start) {
        return 0;
    }
    size_t span = end - start;
    if (span >= INPUT_SIZE) {
        span = INPUT_SIZE - 1u;
    }
    char temp[INPUT_SIZE];
    memcpy(temp, buffer + start, span);
    temp[span] = '\0';
    return utf8_string_display_width(temp);
}

// FIX: Replaces move_cursor_columns().
// This function moves VISUALLY to the end of the line by reprinting characters.
// Reprinting lets the terminal do the wrapping — ESC[nC does NOT wrap.
static void move_to_end_of_line(const char *buffer, size_t *cursor, size_t pos) {
    while (*cursor < pos) {
        size_t next = utf8_next_char_start(buffer, *cursor, pos);

        // FIX: printing characters ensures correct wrap behaviour
        fwrite(buffer + *cursor, 1, next - *cursor, stdout);

        *cursor = next;
    }
}

// FIX: Clears entire input line using backspace + space.
// Backspace is the ONLY cursor movement that safely moves across wrapped rows.
static void clear_line_contents(const char *buffer, size_t *pos, size_t *cursor) {
    while (*pos > 0) {
        size_t prev = utf8_prev_char_start(buffer, *pos);
        int width = utf8_display_width_range(buffer, prev, *pos);

        // FIX: "\b \b" clears one printed cell at a time
        // and works perfectly even on wrapped lines.
        for (int i = 0; i < width; i++) {
            printf("\b \b");
        }

        *pos = prev;
    }
    *cursor = 0;
    fflush(stdout);
}

static int utf8_string_display_width(const char *s) {
    if (!s)
        return 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    int width = 0;
    const char *p = s;
    while (*p != '\0') {
        wchar_t wc;
        size_t consumed = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (consumed == (size_t)-1) {
            memset(&state, 0, sizeof(state));
            consumed = 1;
            width++;
        } else if (consumed == (size_t)-2) {
            /* Incomplete sequence at end; treat remaining bytes individually */
            width += (int)strlen(p);
            break;
        } else if (consumed == 0) {
            break;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0)
                char_width = 1;
            width += char_width;
        }
        p += consumed;
    }
    return width;
}

static size_t utf8_prev_char_start(const char *buffer, size_t cursor) {
    if (cursor == 0)
        return 0;
    size_t index = cursor - 1;
    while (index > 0 && ((unsigned char)buffer[index] & 0xC0) == 0x80)
        index--;
    return index;
}

static size_t sanitize_text_input(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) {
        return 0;
    }

    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dest_size; si++) {
        unsigned char c = (unsigned char)src[si];
        if (c == '\t') {
            dest[di++] = ' ';
            continue;
        }
        if (c < 0x20u || c == 0x7Fu) {
            continue;
        }
        dest[di++] = src[si];
    }
    dest[di] = '\0';
    return di;
}

static void clear_completion_state(struct completion_state *state) {
    if (!state) {
        return;
    }
    if (state->matches) {
        for (size_t i = 0; i < state->match_count; i++) {
            free(state->matches[i]);
        }
        free(state->matches);
    }
    state->matches = NULL;
    state->match_count = 0;
    state->index = 0;
    state->active = 0;
    state->used_filenames = 0;
    state->quote_char = '\0';
    state->token_start = 0;
    state->token_end = 0;
}

static char **collect_command_matches(const char *token, size_t *match_count) {
    if (match_count) {
        *match_count = 0;
    }
    if (!token) {
        return NULL;
    }

    size_t count = 0;
    size_t cap = 0;
    char **matches = NULL;
    size_t token_len = strlen(token);

    for (int i = 0; i < num_commands; i++) {
        if (strncmp(commands[i], token, token_len) == 0) {
            if (count == cap) {
                size_t new_cap = cap == 0 ? 8u : cap * 2u;
                char **new_matches = realloc(matches, new_cap * sizeof(*matches));
                if (!new_matches) {
                    perror("realloc");
                    clear_completion_state(&(struct completion_state){.matches = matches,
                                                                     .match_count = count});
                    return NULL;
                }
                matches = new_matches;
                cap = new_cap;
            }
            matches[count] = strdup(commands[i]);
            if (!matches[count]) {
                perror("strdup");
                clear_completion_state(&(struct completion_state){.matches = matches,
                                                                 .match_count = count});
                return NULL;
            }
            count++;
        }
    }

    if (match_count) {
        *match_count = count;
    }
    return matches;
}

static char **collect_filename_matches(const char *token, size_t *match_count) {
    if (match_count) {
        *match_count = 0;
    }
    if (!token) {
        return NULL;
    }

    char dir[INPUT_SIZE];
    char prefix[INPUT_SIZE];
    const char *last_slash = strrchr(token, '/');
    if (last_slash) {
        size_t dir_len = last_slash - token + 1;
        strncpy(dir, token, dir_len);
        dir[dir_len] = '\0';
        strcpy(prefix, last_slash + 1);
    } else {
        strcpy(dir, "./");
        strcpy(prefix, token);
    }

    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }

    size_t count = 0;
    size_t cap = 0;
    char **matches = NULL;
    struct dirent *entry;
    size_t prefix_len = strlen(prefix);
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
            char full_completion[INPUT_SIZE];
            if (snprintf(full_completion, sizeof(full_completion), "%s%s", dir,
                         entry->d_name) >= (int)sizeof(full_completion)) {
                continue;
            }
            if (count == cap) {
                size_t new_cap = cap == 0 ? 8u : cap * 2u;
                char **new_matches = realloc(matches, new_cap * sizeof(*matches));
                if (!new_matches) {
                    perror("realloc");
                    closedir(d);
                    clear_completion_state(&(struct completion_state){.matches = matches,
                                                                     .match_count = count});
                    return NULL;
                }
                matches = new_matches;
                cap = new_cap;
            }
            matches[count] = strdup(full_completion);
            if (!matches[count]) {
                perror("strdup");
                closedir(d);
                clear_completion_state(&(struct completion_state){.matches = matches,
                                                                 .match_count = count});
                return NULL;
            }
            count++;
        }
    }
    closedir(d);

    if (match_count) {
        *match_count = count;
    }
    return matches;
}

static void format_completion(const char *completion, int used_filenames, char quote_char,
                              char *formatted, size_t formatted_size) {
    if (!completion || !formatted || formatted_size == 0) {
        return;
    }
    if (used_filenames) {
        if (quote_char != '\0') {
            snprintf(formatted, formatted_size, "%c%s%c", quote_char, completion, quote_char);
        } else {
            escape_token(completion, formatted, formatted_size);
        }
    } else {
        snprintf(formatted, formatted_size, "%s", completion);
    }
}
