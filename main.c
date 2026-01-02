#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200112L   /* Changed per instructions to POSIX.1-200112L */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <termios.h>    // For terminal control (raw mode)
#include <time.h>       // For time functions and nanosleep
#include <sys/ioctl.h>  // For querying terminal window size
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>     // For signal handling
#include <sys/select.h> // For select()
#include <dirent.h>     // For directory handling
#include <sys/stat.h>   // For stat()
#include <locale.h>
#include <wchar.h>

#include "commandparser.h"
#include "input.h"      // Include the input handling header

extern void printlogo();
extern void login();
extern void say();

/* Global variable to control paging.
 * 1: paging enabled (default)
 * 0: paging disabled (used for realtime commands)
 */
int paging_enabled = 1;
int espeak_enable = 0;

/* Global variable to store the base directory (extracted from the executable path)
 * so that relative paths like apps/ can be resolved.
 */
char base_directory[PATH_MAX] = {0};

/* Global dynamic list to store realtime commands loaded from apps/ and commands/ folders. */
char **realtime_commands = NULL;
int realtime_command_count = 0;

/*
 * Global copies of the original command-line arguments.
 * These will be used by the "restart" command when re-executing the new binary.
 */
static int g_argc;
static char **g_argv;

static FILE *log_file = NULL;
static char log_file_path[PATH_MAX] = {0};

static void stop_logging(void) {
    if (log_file != NULL) {
        if (fclose(log_file) != 0) {
            perror("_TOFILE: fclose");
        }
        log_file = NULL;
    }
    log_file_path[0] = '\0';
}

static int start_logging(const char *path) {
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "_TOFILE: missing file path for --start\n");
        return -1;
    }

    if (snprintf(log_file_path, sizeof(log_file_path), "%s", path) >= (int)sizeof(log_file_path)) {
        fprintf(stderr, "_TOFILE: log path too long\n");
        log_file_path[0] = '\0';
        return -1;
    }

    char parent_dir[PATH_MAX];
    if (snprintf(parent_dir, sizeof(parent_dir), "%s", path) >= (int)sizeof(parent_dir)) {
        fprintf(stderr, "_TOFILE: log path too long\n");
        log_file_path[0] = '\0';
        return -1;
    }
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash != NULL && last_slash != parent_dir) {
        *last_slash = '\0';
    } else if (last_slash == parent_dir) {
        parent_dir[1] = '\0';
    }
    if (last_slash != NULL) {
        struct stat sb;
        if (stat(parent_dir, &sb) != 0) {
            perror("_TOFILE: parent directory");
            log_file_path[0] = '\0';
            return -1;
        }
        if (!S_ISDIR(sb.st_mode)) {
            fprintf(stderr, "_TOFILE: parent path is not a directory: %s\n", parent_dir);
            log_file_path[0] = '\0';
            return -1;
        }
        if (access(parent_dir, W_OK) != 0) {
            perror("_TOFILE: directory not writable");
            log_file_path[0] = '\0';
            return -1;
        }
    }

    stop_logging();

    log_file = fopen(path, "w");
    if (!log_file) {
        perror("_TOFILE: fopen");
        log_file_path[0] = '\0';
        return -1;
    }

    if (setvbuf(log_file, NULL, _IOLBF, 0) != 0) {
        perror("_TOFILE: setvbuf");
        stop_logging();
        return -1;
    }

    printf("_TOFILE: logging started to %s\n", log_file_path);
    return 0;
}

static void log_output(const char *data, size_t len) {
    if (log_file == NULL || data == NULL || len == 0)
        return;

    size_t written = fwrite(data, 1, len, log_file);
    if (written < len) {
        perror("_TOFILE: fwrite");
        stop_logging();
    }
    fflush(log_file);
}

static int realtime_command_exists(const char *command_name) {
    for (int i = 0; i < realtime_command_count; i++) {
        if (strcmp(realtime_commands[i], command_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int add_realtime_command(const char *command_name) {
    if (realtime_command_exists(command_name)) {
        return 0;
    }

    char **new_list = realloc(realtime_commands, (realtime_command_count + 1) * sizeof(char *));
    if (!new_list) {
        perror("realloc");
        return -1;
    }
    realtime_commands = new_list;

    realtime_commands[realtime_command_count] = strdup(command_name);
    if (!realtime_commands[realtime_command_count]) {
        perror("strdup");
        return -1;
    }
    realtime_command_count++;
    return 0;
}

static void trim_realtime_line(char *line) {
    if (line == NULL) {
        return;
    }

    char *start = line;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != line) {
        memmove(line, start, strlen(start) + 1);
    }

    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[len - 1] = '\0';
        len--;
    }
}

static void load_realtime_commands_from_dir(const char *relative_dir) {
    char target_path[PATH_MAX];
    if (snprintf(target_path, sizeof(target_path), "%s/%s", base_directory, relative_dir) >= (int)sizeof(target_path)) {
        perror("snprintf");
        return;
    }

    DIR *dir = opendir(target_path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", target_path, entry->d_name) >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat sb;
        if (stat(full_path, &sb) == 0 && S_ISREG(sb.st_mode) && access(full_path, X_OK) == 0) {
            if (add_realtime_command(entry->d_name) == -1) {
                closedir(dir);
                return;
            }
        }
    }

    closedir(dir);
}

static void load_realtime_commands_from_file(const char *relative_path) {
    char target_path[PATH_MAX];
    if (snprintf(target_path, sizeof(target_path), "%s/%s", base_directory, relative_path) >= (int)sizeof(target_path)) {
        perror("snprintf");
        return;
    }

    FILE *fp = fopen(target_path, "r");
    if (!fp) {
        if (errno != ENOENT) {
            perror("fopen");
        }
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        trim_realtime_line(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (add_realtime_command(line) == -1) {
            fclose(fp);
            return;
        }
    }

    if (ferror(fp)) {
        perror("fgets");
    }
    fclose(fp);
}

/* load_realtime_commands()
 *
 * This function scans the "apps/", "commands/", and "games/" directories (relative to the base_directory)
 * and adds the name of each executable file found to the realtime_commands list. It also loads
 * explicit nopaging utilities from the utilities/nopaging.ini file.
 */
void load_realtime_commands(void) {
    load_realtime_commands_from_dir("apps");
    load_realtime_commands_from_dir("commands");
    load_realtime_commands_from_dir("games");
    load_realtime_commands_from_file("utilities/nopaging.ini");
}

/* free_realtime_commands()
 *
 * This function frees the memory allocated for the realtime_commands list.
 */
void free_realtime_commands(void) {
    for (int i = 0; i < realtime_command_count; i++) {
        free(realtime_commands[i]);
    }
    free(realtime_commands);
    realtime_commands = NULL;
    realtime_command_count = 0;
}

/* delay function using busy-wait based on clock() */
void delay(double seconds) {
    clock_t start_time = clock();
    while ((double)(clock() - start_time) / CLOCKS_PER_SEC < seconds) {
        /* Busy waiting */
    }
}

/* delayPrint() prints the provided string one character at a time,
 * waiting for delayTime seconds between each character.
 */
void delayPrint(const char *str, double delayTime) {
    for (int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
        fflush(stdout);
        delay(delayTime);
    }
}

/* This function can be called by any command to disable paging. */
void disable_paging(void) {
    paging_enabled = 0;
}

/* Forward declaration for search mode. */
int search_mode(const char **lines, size_t line_count, const char *query);

/* Displays the current working directory as the prompt. */
void display_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("shell$ ");
}

/* search_mode remains unchanged from the original implementation. */
int search_mode(const char **lines, size_t line_count, const char *query) {
    int *matches = malloc(line_count * sizeof(int));
    if (!matches) {
        perror("malloc");
        return -1;
    }
    int match_count = 0;
    for (size_t i = 0; i < line_count; i++) {
        if (strstr(lines[i], query) != NULL) {
            matches[match_count++] = i;
        }
    }
    if (match_count == 0) {
        free(matches);
        printf("No matches found. Press any key to continue...");
        getchar();
        return -1;
    }
    int active = 0;
    int menu_start = 0;
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    int menu_height = w.ws_row - 1;
    while (1) {
        printf("\033[H\033[J"); // Clear screen.
        int end = menu_start + menu_height;
        if (end > match_count)
            end = match_count;
        for (int i = menu_start; i < end; i++) {
            if (i == active) {
                printf("\033[7m"); // Highlight active match.
            }
            printf("Line %d: %s", matches[i] + 1, lines[matches[i]]);
            if (i == active) {
                printf("\033[0m"); // Reset formatting.
            }
            printf("\n");
        }
        printf("\nUse Up/Down arrows to select, Enter to jump, 'q' to cancel.\n");
        fflush(stdout);
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (ch == 'q') {
            active = -1;
            break;
        } else if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == '\033') { // Arrow key
            if (getchar() == '[') {
                int code = getchar();
                if (code == 'A') { // Up arrow.
                    if (active > 0) {
                        active--;
                        if (active < menu_start)
                            menu_start = active;
                    }
                } else if (code == 'B') { // Down arrow.
                    if (active < match_count - 1) {
                        active++;
                        if (active >= menu_start + menu_height)
                            menu_start = active - menu_height + 1;
                    }
                }
            }
        }
    }
    int result = -1;
    if (active != -1) {
        result = matches[active];
    }
    free(matches);
    return result;
}

static int get_terminal_rows(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
    }
    if (w.ws_row < 1) {
        w.ws_row = 24;
    }
    return (int)w.ws_row;
}

static int get_terminal_cols(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_col = 80;
    }
    if (w.ws_col < 1) {
        w.ws_col = 80;
    }
    return (int)w.ws_col;
}

static int pager_page_height(int rows) {
    int page_height = rows - 2;
    if (page_height < 1)
        page_height = 10;
    return page_height;
}

static size_t display_width(const char *line, int cols) {
    if (line == NULL || cols <= 0) {
        return 0;
    }

    mbstate_t state;
    memset(&state, 0, sizeof(state));

    size_t width = 0;
    const unsigned char *p = (const unsigned char *)line;
    while (*p != '\0') {
        if (*p == '\x1b') {
            const unsigned char *seq = p + 1;
            if (*seq == '[') {
                seq++;
                while (*seq != '\0' && (*seq < '@' || *seq > '~')) {
                    seq++;
                }
                if (*seq != '\0') {
                    seq++;
                }
                p = seq;
                continue;
            }
            if (*seq == ']') {
                seq++;
                while (*seq != '\0') {
                    if (*seq == '\a') {
                        seq++;
                        break;
                    }
                    if (*seq == '\x1b' && seq[1] == '\\') {
                        seq += 2;
                        break;
                    }
                    seq++;
                }
                p = seq;
                continue;
            }
        }

        if (*p == '\t') {
            size_t next_tab = ((width / 8) + 1) * 8;
            width = next_tab;
            p++;
            continue;
        }
        if (*p == '\r') {
            width = 0;
            p++;
            continue;
        }

        wchar_t wc;
        size_t consumed = mbrtowc(&wc, (const char *)p, MB_CUR_MAX, &state);
        if (consumed == (size_t)-1 || consumed == (size_t)-2) {
            memset(&state, 0, sizeof(state));
            width += 1;
            p++;
            continue;
        }
        if (consumed == 0) {
            break;
        }
        int char_width = wcwidth(wc);
        if (char_width > 0) {
            width += (size_t)char_width;
        }
        p += consumed;
    }

    return width;
}

static size_t line_display_rows(const char *line, int cols) {
    if (cols < 1) {
        cols = 80;
    }
    size_t width = display_width(line, cols);
    if (width == 0) {
        return 1;
    }
    return (width + (size_t)cols - 1) / (size_t)cols;
}

static size_t total_display_rows(const char **lines, size_t line_count, int cols) {
    size_t total = 0;
    for (size_t i = 0; i < line_count; i++) {
        total += line_display_rows(lines[i], cols);
    }
    return total;
}

struct wrapped_rows {
    char **rows;
    size_t count;
    size_t capacity;
};

static int wrapped_rows_push(struct wrapped_rows *wrapped, const char *data, size_t len) {
    if (wrapped->count == wrapped->capacity) {
        size_t new_cap = wrapped->capacity == 0 ? 64 : wrapped->capacity * 2;
        char **next = realloc(wrapped->rows, new_cap * sizeof(*wrapped->rows));
        if (next == NULL) {
            perror("realloc");
            return -1;
        }
        wrapped->rows = next;
        wrapped->capacity = new_cap;
    }
    char *row = malloc(len + 1);
    if (row == NULL) {
        perror("malloc");
        return -1;
    }
    if (len > 0) {
        memcpy(row, data, len);
    }
    row[len] = '\0';
    wrapped->rows[wrapped->count++] = row;
    return 0;
}

static void wrapped_rows_free(struct wrapped_rows *wrapped) {
    if (wrapped == NULL) {
        return;
    }
    for (size_t i = 0; i < wrapped->count; i++) {
        free(wrapped->rows[i]);
    }
    free(wrapped->rows);
    wrapped->rows = NULL;
    wrapped->count = 0;
    wrapped->capacity = 0;
}

static int append_wrapped_line(struct wrapped_rows *wrapped, const char *line, int cols, size_t *line_rows) {
    if (line_rows != NULL) {
        *line_rows = 0;
    }
    if (cols < 1) {
        cols = 80;
    }
    if (line == NULL) {
        return wrapped_rows_push(wrapped, "", 0);
    }

    mbstate_t state;
    memset(&state, 0, sizeof(state));

    size_t row_start_count = wrapped->count;
    size_t row_len = 0;
    size_t row_cap = 0;
    char *row_buf = NULL;
    size_t col = 0;

    const unsigned char *p = (const unsigned char *)line;
    while (*p != '\0') {
        if (*p == '\x1b') {
            const unsigned char *seq = p + 1;
            if (*seq == '[') {
                seq++;
                while (*seq != '\0' && (*seq < '@' || *seq > '~')) {
                    seq++;
                }
                if (*seq != '\0') {
                    seq++;
                }
            } else if (*seq == ']') {
                seq++;
                while (*seq != '\0') {
                    if (*seq == '\a') {
                        seq++;
                        break;
                    }
                    if (*seq == '\x1b' && seq[1] == '\\') {
                        seq += 2;
                        break;
                    }
                    seq++;
                }
            } else {
                seq++;
            }

            size_t seq_len = (size_t)(seq - p);
            if (row_len + seq_len + 1 > row_cap) {
                size_t next_cap = row_cap == 0 ? 128 : row_cap * 2;
                while (row_len + seq_len + 1 > next_cap) {
                    next_cap *= 2;
                }
                char *next = realloc(row_buf, next_cap);
                if (next == NULL) {
                    perror("realloc");
                    free(row_buf);
                    return -1;
                }
                row_buf = next;
                row_cap = next_cap;
            }
            memcpy(row_buf + row_len, p, seq_len);
            row_len += seq_len;
            p = seq;
            continue;
        }

        if (*p == '\t') {
            size_t spaces = 8 - (col % 8);
            if (col > 0 && col + spaces > (size_t)cols) {
                if (wrapped_rows_push(wrapped, row_buf, row_len) != 0) {
                    free(row_buf);
                    return -1;
                }
                row_len = 0;
                col = 0;
                spaces = 8;
            }
            if (row_len + spaces + 1 > row_cap) {
                size_t next_cap = row_cap == 0 ? 128 : row_cap * 2;
                while (row_len + spaces + 1 > next_cap) {
                    next_cap *= 2;
                }
                char *next = realloc(row_buf, next_cap);
                if (next == NULL) {
                    perror("realloc");
                    free(row_buf);
                    return -1;
                }
                row_buf = next;
                row_cap = next_cap;
            }
            for (size_t i = 0; i < spaces; i++) {
                row_buf[row_len++] = ' ';
            }
            col += spaces;
            p++;
            continue;
        }

        if (*p == '\r') {
            if (row_len + 2 > row_cap) {
                size_t next_cap = row_cap == 0 ? 128 : row_cap * 2;
                if (next_cap < row_len + 2) {
                    next_cap = row_len + 2;
                }
                char *next = realloc(row_buf, next_cap);
                if (next == NULL) {
                    perror("realloc");
                    free(row_buf);
                    return -1;
                }
                row_buf = next;
                row_cap = next_cap;
            }
            row_buf[row_len++] = '\r';
            col = 0;
            p++;
            continue;
        }

        wchar_t wc;
        size_t consumed = mbrtowc(&wc, (const char *)p, MB_CUR_MAX, &state);
        size_t char_len = 0;
        int char_width = 1;
        if (consumed == (size_t)-1 || consumed == (size_t)-2) {
            memset(&state, 0, sizeof(state));
            char_len = 1;
            char_width = 1;
        } else if (consumed == 0) {
            break;
        } else {
            char_len = consumed;
            char_width = wcwidth(wc);
            if (char_width <= 0) {
                char_width = 1;
            }
        }

        if (col > 0 && col + (size_t)char_width > (size_t)cols) {
            if (wrapped_rows_push(wrapped, row_buf, row_len) != 0) {
                free(row_buf);
                return -1;
            }
            row_len = 0;
            col = 0;
        }

        if (row_len + char_len + 1 > row_cap) {
            size_t next_cap = row_cap == 0 ? 128 : row_cap * 2;
            while (row_len + char_len + 1 > next_cap) {
                next_cap *= 2;
            }
            char *next = realloc(row_buf, next_cap);
            if (next == NULL) {
                perror("realloc");
                free(row_buf);
                return -1;
            }
            row_buf = next;
            row_cap = next_cap;
        }
        memcpy(row_buf + row_len, p, char_len);
        row_len += char_len;
        col += (size_t)char_width;
        p += char_len;
    }

    if (row_len == 0 && row_start_count == wrapped->count) {
        if (wrapped_rows_push(wrapped, "", 0) != 0) {
            free(row_buf);
            return -1;
        }
    } else if (row_len > 0) {
        if (wrapped_rows_push(wrapped, row_buf, row_len) != 0) {
            free(row_buf);
            return -1;
        }
    }

    free(row_buf);
    if (line_rows != NULL) {
        *line_rows = wrapped->count - row_start_count;
    }
    return 0;
}

/* Pager function accounts for wrapped display rows. */
void pager(const char **lines, size_t line_count) {
    int rows = get_terminal_rows();
    int cols = get_terminal_cols();
    int page_height = pager_page_height(rows);
    size_t *line_rows = malloc(line_count * sizeof(size_t));
    size_t *prefix = malloc((line_count + 1) * sizeof(size_t));
    struct wrapped_rows wrapped = {0};
    if (line_rows == NULL || prefix == NULL) {
        perror("malloc");
        free(line_rows);
        free(prefix);
        return;
    }
    prefix[0] = 0;
    for (size_t i = 0; i < line_count; i++) {
        if (append_wrapped_line(&wrapped, lines[i], cols, &line_rows[i]) != 0) {
            free(line_rows);
            free(prefix);
            wrapped_rows_free(&wrapped);
            return;
        }
        prefix[i + 1] = prefix[i] + line_rows[i];
    }
    size_t total_rows = wrapped.count;
    if (total_rows == 0) {
        free(line_rows);
        free(prefix);
        wrapped_rows_free(&wrapped);
        return;
    }

    size_t row_offset = 0;
    while (1) {
        printf("\033[H\033[J"); // Clear the screen.
        size_t rows_used = 0;
        for (size_t i = row_offset; i < total_rows && rows_used < (size_t)page_height; i++) {
            printf("%s\n", wrapped.rows[i]);
            rows_used++;
        }
        int total_pages = (int)((total_rows + (size_t)page_height - 1) / (size_t)page_height);
        int current_page = (int)(row_offset / (size_t)page_height) + 1;
        printf("\nPage %d/%d - Use Up/Dn to scroll, PgUp/PgDn to jump, 'f' to find, 'q' to quit.",
               current_page, total_pages);
        fflush(stdout);
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int c = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (c == 'q') {
            break;
        } else if (c == '\033') {
            if (getchar() == '[') {
                int code = getchar();
                if (code == 'A') { // Up arrow.
                    if (row_offset > 0) {
                        row_offset--;
                    } else {
                        row_offset = 0;
                    }
                } else if (code == 'B') { // Down arrow.
                    size_t max_start = total_rows > (size_t)page_height
                                           ? total_rows - (size_t)page_height
                                           : 0;
                    if (row_offset < max_start) {
                        row_offset++;
                    }
                } else if (code == '5') { // Page up.
                    if (getchar() == '~') {
                        if (row_offset >= (size_t)page_height) {
                            row_offset -= (size_t)page_height;
                        } else {
                            row_offset = 0;
                        }
                    }
                } else if (code == '6') { // Page down.
                    if (getchar() == '~') {
                        size_t max_start = total_rows > (size_t)page_height
                                               ? total_rows - (size_t)page_height
                                               : 0;
                        if (row_offset + (size_t)page_height < max_start) {
                            row_offset += (size_t)page_height;
                        } else {
                            row_offset = max_start;
                        }
                    }
                }
            }
        } else if (c == 'f') {
            char search[256];
            printf("\nSearch: ");
            fflush(stdout);
            if (fgets(search, sizeof(search), stdin) != NULL) {
                search[strcspn(search, "\n")] = '\0';
                if (strlen(search) > 0) {
                    int selected = search_mode(lines, line_count, search);
                    if (selected != -1) {
                        row_offset = prefix[selected];
                        size_t max_start = total_rows > (size_t)page_height
                                               ? total_rows - (size_t)page_height
                                               : 0;
                        if (row_offset > max_start) {
                            row_offset = max_start;
                        }
                    }
                }
            }
        }
    }
    printf("\n");
    free(line_rows);
    free(prefix);
    wrapped_rows_free(&wrapped);
}

int is_realtime_command(const char *command) {
    for (int i = 0; i < realtime_command_count; i++) {
        if (strcmp(command, realtime_commands[i]) == 0)
            return 1;
    }
    return 0;
}

/*
 * Updated execute_command_with_paging():
 * - For realtime commands (or when the "-nopaging" flag is provided), execute directly.
 * - Otherwise, fork a child process to execute the command and capture its output.
 */
int execute_command_with_paging(CommandStruct *cmd) {
    int nopaging = 0;
    /* Check if the command parameters contain "-nopaging" flag. */
    for (int i = 0; i < cmd->param_count; i++) {
        if (strcmp(cmd->parameters[i], "-nopaging") == 0) {
            nopaging = 1;
            /* Remove the "-nopaging" flag by shifting the remaining parameters. */
            for (int j = i; j < cmd->param_count - 1; j++) {
                cmd->parameters[j] = cmd->parameters[j + 1];
            }
            cmd->param_count--;
            break;
        }
    }
    
    /* Realtime mode is now entered if:
     * - The "-nopaging" flag is provided, or
     * - The command is in the realtime command list loaded from apps/ folder.
     * - The command requires interactive input (e.g., prompts).
     */
    int realtime_mode = nopaging || is_realtime_command(cmd->command);

    if (realtime_mode && log_file == NULL) {
        return execute_command(cmd);
    }
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return execute_command(cmd);
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return execute_command(cmd);
    }
    
    if (pid == 0) {
        // In child process, reset SIGINT to default so that CTRL+C kills the app.
        signal(SIGINT, SIG_DFL);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        if (dup2(pipefd[1], STDERR_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        int exec_ret = execute_command(cmd);
        exit(exec_ret == 0 ? EXIT_SUCCESS : 127);
    }
    
    /* Parent process: Close write end and capture output */
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    
    time_t start_time = time(NULL);
    int timeout_seconds = 5; // Timeout in seconds.
    size_t total_size = 0;
    size_t buffer_size = realtime_mode ? 0 : 4096;
    char *output = NULL;
    if (!realtime_mode) {
        output = malloc(buffer_size);
        if (!output) {
            perror("malloc");
            close(pipefd[0]);
            int status; waitpid(pid, &status, 0);
            return (WIFEXITED(status) && WEXITSTATUS(status) == 127) ? -1 : 0;
        }
    }
    char buffer[4096];
    int child_status = 0;
    int child_exited = 0;
    
    /* Concurrently read from the pipe while monitoring the child process */
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);
        struct timeval tv = {1, 0}; // 1-second timeout for select()
        int sel_ret = select(pipefd[0] + 1, &readfds, NULL, NULL, &tv);
        if (sel_ret > 0 && FD_ISSET(pipefd[0], &readfds)) {
            ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer));
            if (bytes > 0) {
                if (!realtime_mode) {
                    if (total_size + bytes >= buffer_size) {
                        buffer_size *= 2;
                        char *temp = realloc(output, buffer_size);
                        if (!temp) {
                            perror("realloc");
                            free(output);
                            close(pipefd[0]);
                            int status; waitpid(pid, &status, 0);
                            return (WIFEXITED(status) && WEXITSTATUS(status) == 127) ? -1 : 0;
                        }
                        output = temp;
                    }
                    memcpy(output + total_size, buffer, bytes);
                    total_size += bytes;
                } else {
                    if (fwrite(buffer, 1, (size_t)bytes, stdout) < (size_t)bytes) {
                        perror("write");
                    }
                    fflush(stdout);
                }
                log_output(buffer, (size_t)bytes);
            } else if (bytes == 0) {
                // End-of-file reached.
                break;
            }
        }
        /* Check if child has finished */
        pid_t result = waitpid(pid, &child_status, WNOHANG);
        if (result == pid) {
            // Child finished. Continue reading any remaining data.
            child_exited = 1;
        }
        if (time(NULL) - start_time > timeout_seconds) {
            /* Timeout reached; kill child process if still running */
            kill(pid, SIGKILL);
            waitpid(pid, &child_status, 0);
            break;
        }
    }
    close(pipefd[0]);
    if (!child_exited) {
        if (waitpid(pid, &child_status, 0) < 0 && errno != ECHILD) {
            perror("waitpid");
        }
    }

    if (realtime_mode) {
        return (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 127) ? -1 : 0;
    }

    if (total_size == 0) {
        free(output);
        return (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 127) ? -1 : 0;
    }
    output[total_size] = '\0';

    log_output(output, total_size);
    
    /*
     * Manually split output into lines while preserving empty lines.
     */
    size_t line_count = 0;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n')
            line_count++;
    }
    if (total_size > 0 && output[total_size-1] != '\n')
        line_count++;
    
    char **lines = malloc(line_count * sizeof(char *));
    if (!lines) {
        perror("malloc");
        free(output);
        return (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 127) ? -1 : 0;
    }
    size_t current_line = 0;
    char *start = output;
    for (size_t i = 0; i < total_size; i++) {
        if (output[i] == '\n') {
            output[i] = '\0';  // terminate this line
            lines[current_line++] = start;
            start = output + i + 1;
        }
    }
    if (start < output + total_size) {
        lines[current_line++] = start;
    }
    
    /* Determine page height based on terminal size. */
    int rows = get_terminal_rows();
    int cols = get_terminal_cols();
    int page_height = pager_page_height(rows);
    size_t display_rows = total_display_rows((const char **)lines, current_line, cols);
    
    /* If the output fits in one page, print directly; otherwise, page the output. */
    if (display_rows <= (size_t)page_height) {
        for (size_t i = 0; i < current_line; i++) {
            printf("%s\n", lines[i]);
        }
    } else {
        pager((const char **)lines, current_line);
    }
    free(lines);
    free(output);
    return (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 127) ? -1 : 0;
}

static void run_shell_command(const char *shell_command) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
    } else if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execl("/bin/sh", "sh", "-c", shell_command, (char *)NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

static int handle_tofile(CommandStruct *cmd) {
    if (strcmp(cmd->command, "_TOFILE") != 0)
        return 0;

    int start_flag = 0;
    int stop_flag = 0;
    const char *path = NULL;

    for (int i = 0; i < cmd->opt_count; i++) {
        if (strcmp(cmd->options[i], "-file") == 0 && i + 1 < cmd->opt_count) {
            path = cmd->options[i + 1];
            i++;
        } else if (strcmp(cmd->options[i], "--start") == 0) {
            start_flag = 1;
        } else if (strcmp(cmd->options[i], "--stop") == 0) {
            stop_flag = 1;
        }
    }

    if (start_flag && stop_flag) {
        fprintf(stderr, "_TOFILE: cannot use --start and --stop together\n");
        return 1;
    }

    if (start_flag) {
        (void)start_logging(path);
        return 1;
    }

    if (stop_flag) {
        if (log_file != NULL) {
            printf("_TOFILE: logging stopped (%s)\n", log_file_path[0] != '\0' ? log_file_path : "<unknown>");
        } else {
            printf("_TOFILE: logging was not active\n");
        }
        stop_logging();
        return 1;
    }

    fprintf(stderr, "Usage: _TOFILE -file <path> --start | _TOFILE --stop\n");
    return 1;
}

int main(int argc, char *argv[]) {
    char *input;
    CommandStruct cmd;

    init_command_struct(&cmd);

    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "Warning: failed to configure locale; Unicode I/O may be limited.\n");
    }

    /* 
     * Ignore SIGINT in the shell so that CTRL+C does not quit BUDOSTACK.
     * Child processes will reset SIGINT to default.
     */
    signal(SIGINT, SIG_IGN);
    
    /* Store original command-line arguments for later use by the "restart" command */
    g_argc = argc;
    g_argv = argv;
    
    /* Determine the base directory of the executable. */
    char exe_path[PATH_MAX] = {0};
    const char *env_base = getenv("BUDOSTACK_BASE");
    if (env_base && env_base[0] != '\0') {
        const char *source = env_base;
        if (realpath(env_base, exe_path) != NULL) {
            source = exe_path;
        }
        snprintf(base_directory, sizeof(base_directory), "%s", source);
        set_base_path(base_directory);
    } else if (argc > 0) {
        int resolved = 0;
        if (realpath(argv[0], exe_path) != NULL) {
            resolved = 1;
        } else {
            ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
            if (len != -1) {
                exe_path[len] = '\0';
                resolved = 1;
            }
        }
        if (resolved) {
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0'; // Terminate the string at the last '/'
            }
            set_base_path(exe_path);
            snprintf(base_directory, sizeof(base_directory), "%s", exe_path);
            if (setenv("BUDOSTACK_BASE", base_directory, 1) != 0) {
                perror("setenv BUDOSTACK_BASE");
            }
        } else {
            fprintf(stderr, "Warning: unable to resolve executable path; relative commands may fail.\n");
        }
    }
    
    /* Load the realtime command list from the apps/ folder */
    load_realtime_commands();

    /* Clear the screen */
    if (system("clear") != 0)
        perror("system");

    /* Modified: Determine if we need to auto-run a command. */
    char *auto_command = NULL;
    if (argc == 2 && strcmp(argv[1], "-f") != 0) {
        size_t len = strlen("runtask ") + strlen(argv[1]) + strlen(".task") + 1;
        auto_command = malloc(len);
        if (auto_command == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        snprintf(auto_command, len, "runtask %s.task", argv[1]);
    }

    /* Run autoexec before announcing readiness */
    {
        CommandStruct aut;
        init_command_struct(&aut);
        char *autoexec_cmd = strdup("runtask autoexec.task");
        if (!autoexec_cmd) {
            perror("strdup");
        } else {
            parse_input(autoexec_cmd, &aut);
            /* Try in-app command first; if not handled, fall back to /bin/sh */
            if (execute_command_with_paging(&aut) == -1) {
                run_shell_command(autoexec_cmd);
            }
            free_command_struct(&aut);
            free(autoexec_cmd);
        }
    }

    system("clear");

    /* Modified: Enable login only if argument (-f) given or auto_command mode. */
    if ((argc > 1 && strcmp(argv[1], "-f") == 0) || auto_command != NULL) {        
        if (system("clear") != 0)
            perror("system");
        printlogo();
        login();
        printf("========================================================================\n");
    } else {
        /* Do not print startup messages and skip login() */
    }

    printf("\nSYSTEM READY");
    say("system ready");
    printf("\nType 'help' for command list.");
    printf("\nType 'exit' to quit.\n\n");

    /* Execute auto_command if set */
    if (auto_command != NULL) {
        parse_input(auto_command, &cmd);
        if (execute_command_with_paging(&cmd) == -1) {
            run_shell_command(auto_command);
        }
        free_command_struct(&cmd);
        free(auto_command);
    }

    /* Main loop */
    while (1) {
        display_prompt();
        input = read_input();
        if (input == NULL) {
            printf("\n");
            break;
        }
        if (input[0] == '\0') {
            free(input);
            continue;
        }
        input[strcspn(input, "\n")] = '\0';
        if (espeak_enable) { say(input); };
                
        /* NEW: "restart" command handling.
         * When the user types "restart" or "restart -f", the shell first changes its working directory to the base directory,
         * then runs "make" to recompile itself. If "restart -f" is entered, "make clean" is executed before rebuilding.
         * After running make (or make clean failure), a pause is introduced so that build warnings/errors can be read.
         */
        if (strncmp(input, "restart", 7) == 0) {
            int force = 0;
            // Tokenize the input to check for additional parameter "-f"
            char *token = strtok(input, " ");
            token = strtok(NULL, " ");
            if (token && strcmp(token, "-f") == 0) {
                force = 1;
            }
            free(input);
            // Change directory to base_directory so that make is run from the correct location.
            if (chdir(base_directory) != 0) {
                perror("chdir to base_directory failed");
                continue;
            }
            // If force flag is set, run "make clean" before rebuilding.
            if (force) {
                int clean_ret = system("make clean");
                if (clean_ret != 0) {
                    fprintf(stderr, "make clean failed, not restarting.\n");
                    printf("Press ENTER to continue...");
                    fflush(stdout);
                    while(getchar() != '\n');
                    continue;
                }
            }
            int ret = system("make");
            printf("Press ENTER to continue...");
            fflush(stdout);
            while(getchar() != '\n');
            if (ret != 0) {
                fprintf(stderr, "Make failed, not restarting.\n");
                continue;
            } else {
                execv(g_argv[0], g_argv);
                perror("execv failed");
                exit(EXIT_FAILURE);
            }
            continue;
        }

        if (strcmp(input, "mute") == 0) {
            // set say() off / on
            espeak_enable = !espeak_enable;
            if (espeak_enable) {
                printf("Voice assist enabled\n");
            }
            else {
                printf("Voice assist disabled\n");
            }
            free(input);
            continue;
        }
        
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        /* Built-in "cd" command handling */
        if (strncmp(input, "cd", 2) == 0) {
            parse_input(input, &cmd);
            if (cmd.param_count > 0) {
                if (chdir(cmd.parameters[0]) != 0)
                    perror("cd");
            } else {
                fprintf(stderr, "cd: missing operand\n");
            }
            free(input);
            free_command_struct(&cmd);
            continue;
        }
        /* Built-in "run" command handling */
        if (strncmp(input, "run", 3) == 0 && (input[3] == ' ' || input[3] == '\0')) {
            if (input[3] == '\0') {
                fprintf(stderr, "run: missing operand\n");
                free(input);
                continue;
            }
            /* Skip the "run " prefix and trim leading spaces */
            char *shell_command = input + 4;
            while (*shell_command == ' ')
                shell_command++;
            run_shell_command(shell_command);
            free(input);
            continue;
        }
        /* Default processing for other commands */
        parse_input(input, &cmd);
        if (handle_tofile(&cmd)) {
            free(input);
            free_command_struct(&cmd);
            continue;
        }
        if (execute_command_with_paging(&cmd) == -1) {
            run_shell_command(input);
        }
        free(input);
        free_command_struct(&cmd);
    }
    
    /* Free the realtime command list resources before exiting */
    free_realtime_commands();

    stop_logging();

    printf("Exiting terminal...\n");
    return 0;
}
