#define _XOPEN_SOURCE 700
#define BUDOSTACK_LIST_NO_MAIN

#include "../utilities/list.c"
#include "../lib/terminal_layout.h"

#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CTRL_KEY(k) ((k) & 0x1f)
#define EXPLORER_MIN_ROWS 12
#define EXPLORER_MIN_COLS 60
#define EXPLORER_STATUS_SIZE 256

enum ExplorerKey {
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE
};

typedef struct {
    char *name;
    char path[PATH_MAX];
    mode_t mode;
    off_t size;
    time_t mtime;
    int is_dir;
} ExplorerEntry;

typedef struct {
    struct termios original_termios;
    ExplorerEntry *entries;
    size_t entry_count;
    size_t selected;
    size_t scroll;
    char cwd[PATH_MAX];
    char status[EXPLORER_STATUS_SIZE];
    int rows;
    int cols;
    int show_hidden;
    int running;
    char edit_path[PATH_MAX];
} ExplorerState;

static ExplorerState E;

static void explorer_set_status(const char *fmt, ...);

static void explorer_die(const char *message)
{
    if (write(STDOUT_FILENO, "\x1b[?25h\x1b[0m\x1b[2J\x1b[H", strlen("\x1b[?25h\x1b[0m\x1b[2J\x1b[H")) < 0) {
        perror("write");
    }
    perror(message);
    exit(EXIT_FAILURE);
}

static void explorer_disable_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1) {
        perror("tcsetattr");
    }
    if (write(STDOUT_FILENO, "\x1b[?25h\x1b[0m", strlen("\x1b[?25h\x1b[0m")) < 0) {
        perror("write");
    }
}

static void explorer_enable_raw_mode(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1) {
        explorer_die("tcgetattr");
    }
    atexit(explorer_disable_raw_mode);

    raw = E.original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        explorer_die("tcsetattr");
    }
}

static int explorer_read_key(void)
{
    char c;
    ssize_t nread;

    do {
        nread = read(STDIN_FILENO, &c, 1);
    } while (nread == 0);

    if (nread == -1) {
        explorer_die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                        case '7':
                            return KEY_HOME;
                        case '3':
                            return KEY_DELETE;
                        case '4':
                        case '8':
                            return KEY_END;
                        case '5':
                            return KEY_PAGE_UP;
                        case '6':
                            return KEY_PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return KEY_ARROW_UP;
                    case 'B':
                        return KEY_ARROW_DOWN;
                    case 'C':
                        return KEY_ARROW_RIGHT;
                    case 'D':
                        return KEY_ARROW_LEFT;
                    case 'H':
                        return KEY_HOME;
                    case 'F':
                        return KEY_END;
                }
            }
        }
        return '\x1b';
    }

    return c;
}

static int explorer_get_window_size(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        *rows = budostack_get_target_rows();
        *cols = budostack_get_target_cols();
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }

    budostack_clamp_terminal_size(rows, cols);
    if (*rows < EXPLORER_MIN_ROWS) {
        *rows = EXPLORER_MIN_ROWS;
    }
    if (*cols < EXPLORER_MIN_COLS) {
        *cols = EXPLORER_MIN_COLS;
    }

    return 0;
}

static void explorer_set_status(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(E.status, sizeof(E.status), fmt, ap);
    E.status[sizeof(E.status) - 1] = '\0';
    va_end(ap);
}

static void explorer_free_entries(void)
{
    size_t i;

    for (i = 0; i < E.entry_count; i++) {
        free(E.entries[i].name);
    }
    free(E.entries);
    E.entries = NULL;
    E.entry_count = 0;
}

static int explorer_entry_compare(const void *left, const void *right)
{
    const ExplorerEntry *a = left;
    const ExplorerEntry *b = right;
    int folded;

    if (strcmp(a->name, "..") == 0) {
        return -1;
    }
    if (strcmp(b->name, "..") == 0) {
        return 1;
    }
    if (a->is_dir != b->is_dir) {
        return b->is_dir - a->is_dir;
    }

    folded = strcasecmp(a->name, b->name);
    if (folded != 0) {
        return folded;
    }
    return strcmp(a->name, b->name);
}

static int explorer_add_entry(const char *dir_path, const char *name)
{
    ExplorerEntry *new_entries;
    ExplorerEntry *entry;
    struct stat st;
    size_t new_count;
    int written;

    new_count = E.entry_count + 1;
    new_entries = realloc(E.entries, new_count * sizeof(*E.entries));
    if (new_entries == NULL) {
        explorer_set_status("Out of memory while reading directory");
        return -1;
    }
    E.entries = new_entries;
    entry = &E.entries[E.entry_count];
    memset(entry, 0, sizeof(*entry));

    entry->name = strdup(name);
    if (entry->name == NULL) {
        explorer_set_status("Out of memory while reading directory");
        return -1;
    }

    written = snprintf(entry->path, sizeof(entry->path), "%s/%s", dir_path, name);
    if (written < 0 || (size_t)written >= sizeof(entry->path)) {
        free(entry->name);
        entry->name = NULL;
        explorer_set_status("Path is too long: %s/%s", dir_path, name);
        return -1;
    }

    if (lstat(entry->path, &st) == -1) {
        free(entry->name);
        entry->name = NULL;
        explorer_set_status("Cannot stat %s: %s", entry->path, strerror(errno));
        return -1;
    }

    entry->mode = st.st_mode;
    entry->size = st.st_size;
    entry->mtime = st.st_mtime;
    entry->is_dir = S_ISDIR(st.st_mode);
    E.entry_count = new_count;
    return 0;
}

static int explorer_load_directory(const char *dir_path)
{
    DIR *dir;
    struct dirent *entry;
    char previous[PATH_MAX];
    char target[PATH_MAX];

    snprintf(previous, sizeof(previous), "%s", E.cwd);
    snprintf(target, sizeof(target), "%s", dir_path);
    target[sizeof(target) - 1] = '\0';
    explorer_free_entries();

    if (chdir(target) == -1) {
        explorer_set_status("Cannot enter %s: %s", target, strerror(errno));
        return -1;
    }
    if (getcwd(E.cwd, sizeof(E.cwd)) == NULL) {
        explorer_set_status("Cannot read current directory: %s", strerror(errno));
        if (chdir(previous) == -1) {
            perror("chdir");
        }
        snprintf(E.cwd, sizeof(E.cwd), "%s", previous);
        return -1;
    }

    dir = opendir(E.cwd);
    if (dir == NULL) {
        explorer_set_status("Cannot open %s: %s", E.cwd, strerror(errno));
        if (chdir(previous) == -1) {
            perror("chdir");
        }
        snprintf(E.cwd, sizeof(E.cwd), "%s", previous);
        return -1;
    }

    if (strcmp(E.cwd, "/") != 0) {
        if (explorer_add_entry(E.cwd, "..") == -1) {
            closedir(dir);
            return -1;
        }
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!E.show_hidden && entry->d_name[0] == '.') {
            continue;
        }
        if (explorer_add_entry(E.cwd, entry->d_name) == -1) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    qsort(E.entries, E.entry_count, sizeof(*E.entries), explorer_entry_compare);
    E.selected = 0;
    E.scroll = 0;
    explorer_set_status("%zu entries. Enter opens, e edits via ./apps/edit, d deletes, r renames, n creates a folder.", E.entry_count);
    return 0;
}

static ExplorerEntry *explorer_selected_entry(void)
{
    if (E.entry_count == 0 || E.selected >= E.entry_count) {
        return NULL;
    }
    return &E.entries[E.selected];
}

static void explorer_scroll_to_cursor(void)
{
    size_t visible_rows;

    visible_rows = (size_t)(E.rows - 5);
    if (E.selected < E.scroll) {
        E.scroll = E.selected;
    }
    if (E.selected >= E.scroll + visible_rows) {
        E.scroll = E.selected - visible_rows + 1;
    }
}

static void explorer_draw_repeated(char ch, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        putchar(ch);
    }
}

static void explorer_draw_truncated(const char *text, int width)
{
    int len;

    if (width <= 0) {
        return;
    }
    len = (int)strlen(text);
    if (len <= width) {
        printf("%s", text);
        explorer_draw_repeated(' ', width - len);
        return;
    }
    if (width <= 3) {
        explorer_draw_repeated('.', width);
        return;
    }
    printf("%.*s...", width - 3, text);
}

static void explorer_draw_bar_text(const char *left, const char *right, int row)
{
    int left_width;
    int right_width;
    int space;

    left_width = (int)strlen(left);
    right_width = (int)strlen(right);
    space = E.cols - left_width - right_width;
    if (space < 1) {
        space = 1;
    }

    printf("\x1b[%d;1H\x1b[7m", row);
    explorer_draw_truncated(left, E.cols - right_width - space);
    explorer_draw_repeated(' ', space);
    printf("%s", right);
    printf("\x1b[0m");
}

static void explorer_draw_entry(const ExplorerEntry *entry, int row, int selected, int name_width)
{
    char display_name[PATH_MAX + 4];
    char perms[11];
    char size_value_raw[32];
    char unit_raw[8];
    char size_text[48];
    char time_text[20];
    struct tm *tm_info;

    mode_to_string(entry->mode, perms);
    if (entry->is_dir && strcmp(entry->name, "..") != 0) {
        snprintf(display_name, sizeof(display_name), "[%s]", entry->name);
    } else {
        snprintf(display_name, sizeof(display_name), "%s", entry->name);
    }

    if (entry->is_dir) {
        snprintf(size_text, sizeof(size_text), "<DIR>");
    } else {
        format_size(entry->size, size_value_raw, sizeof(size_value_raw), unit_raw, sizeof(unit_raw));
        snprintf(size_text, sizeof(size_text), "%s %s", size_value_raw, unit_raw);
    }

    tm_info = localtime(&entry->mtime);
    if (tm_info != NULL) {
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M", tm_info);
    } else {
        snprintf(time_text, sizeof(time_text), "unknown");
    }

    printf("\x1b[%d;2H", row);
    if (selected) {
        printf("\x1b[44;37m");
    }
    explorer_draw_truncated(display_name, name_width);
    printf("  %-10s %10s  %-16s", perms, size_text, time_text);
    if (selected) {
        printf("\x1b[0m");
    }
}

static void explorer_draw_screen(void)
{
    size_t i;
    size_t visible_rows;
    int row;
    int name_width;
    char title[PATH_MAX + 64];
    char right[64];

    explorer_get_window_size(&E.rows, &E.cols);
    explorer_scroll_to_cursor();
    visible_rows = (size_t)(E.rows - 5);
    name_width = E.cols - 46;
    if (name_width < 16) {
        name_width = 16;
    }

    printf("\x1b[?25l\x1b[2J\x1b[H");
    snprintf(title, sizeof(title), " BUDOSTACK Explorer  %s ", E.cwd);
    snprintf(right, sizeof(right), " %zu/%zu ", E.entry_count == 0 ? 0 : E.selected + 1, E.entry_count);
    explorer_draw_bar_text(title, right, 1);

    printf("\x1b[2;1H+");
    explorer_draw_repeated('-', E.cols - 2);
    printf("+");
    printf("\x1b[3;2H%-*s  %-10s %10s  %-16s", name_width, "Name", "Mode", "Size", "Modified");

    for (i = 0; i < visible_rows; i++) {
        row = (int)i + 4;
        printf("\x1b[%d;1H|", row);
        explorer_draw_repeated(' ', E.cols - 2);
        printf("|");
        if (E.scroll + i < E.entry_count) {
            explorer_draw_entry(&E.entries[E.scroll + i], row, E.scroll + i == E.selected, name_width);
        }
    }

    row = E.rows - 1;
    printf("\x1b[%d;1H+", row);
    explorer_draw_repeated('-', E.cols - 2);
    printf("+");
    explorer_draw_bar_text(E.status, E.show_hidden ? " hidden:on " : " hidden:off ", E.rows - 2);
    explorer_draw_bar_text(" ↑↓ Move  Enter/Open  ← Parent  e Edit  r Rename  d Delete  n Mkdir ", " h Hidden  q Quit ", E.rows);
    fflush(stdout);
}

static int explorer_prompt(const char *prompt, char *buffer, size_t buffer_size)
{
    size_t len;
    int c;

    if (buffer_size == 0) {
        return -1;
    }
    len = strlen(buffer);
    for (;;) {
        explorer_set_status("%s%s", prompt, buffer);
        explorer_draw_screen();
        c = explorer_read_key();
        if (c == '\r' || c == '\n') {
            buffer[buffer_size - 1] = '\0';
            return len > 0 ? 0 : -1;
        }
        if (c == '\x1b' || c == CTRL_KEY('q')) {
            return -1;
        }
        if (c == 127 || c == CTRL_KEY('h') || c == KEY_DELETE) {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
            }
            continue;
        }
        if (c >= 0 && c <= UCHAR_MAX && isprint((unsigned char)c) && len + 1 < buffer_size) {
            buffer[len] = (char)c;
            len++;
            buffer[len] = '\0';
        }
    }
}

static void explorer_open_selected(void)
{
    ExplorerEntry *entry;

    entry = explorer_selected_entry();
    if (entry == NULL) {
        explorer_set_status("No entry selected");
        return;
    }
    if (entry->is_dir) {
        explorer_load_directory(entry->path);
    } else {
        explorer_set_status("File: %s. Press e to edit, d to delete, r to rename.", entry->name);
    }
}

static void explorer_go_parent(void)
{
    if (strcmp(E.cwd, "/") == 0) {
        explorer_set_status("Already at filesystem root");
        return;
    }
    explorer_load_directory("..");
}

static void explorer_delete_selected(void)
{
    ExplorerEntry *entry;
    char prompt[PATH_MAX + 64];
    char answer[8];
    char deleted_name[PATH_MAX];

    entry = explorer_selected_entry();
    if (entry == NULL || strcmp(entry->name, "..") == 0) {
        explorer_set_status("Choose a normal file or empty directory to delete");
        return;
    }

    snprintf(prompt, sizeof(prompt), "Delete %s? Type y: ", entry->name);
    answer[0] = '\0';
    if (explorer_prompt(prompt, answer, sizeof(answer)) == -1 || strcmp(answer, "y") != 0) {
        explorer_set_status("Delete canceled");
        return;
    }

    snprintf(deleted_name, sizeof(deleted_name), "%s", entry->name);

    if (entry->is_dir) {
        if (rmdir(entry->path) == -1) {
            explorer_set_status("Cannot delete directory %s: %s", entry->name, strerror(errno));
            return;
        }
    } else if (unlink(entry->path) == -1) {
        explorer_set_status("Cannot delete %s: %s", entry->name, strerror(errno));
        return;
    }

    explorer_load_directory(E.cwd);
    explorer_set_status("Deleted %s", deleted_name);
}

static void explorer_rename_selected(void)
{
    ExplorerEntry *entry;
    char new_name[PATH_MAX];
    char new_path[PATH_MAX];
    int written;

    entry = explorer_selected_entry();
    if (entry == NULL || strcmp(entry->name, "..") == 0) {
        explorer_set_status("Choose a normal file or directory to rename");
        return;
    }

    snprintf(new_name, sizeof(new_name), "%s", entry->name);
    if (explorer_prompt("Rename to: ", new_name, sizeof(new_name)) == -1) {
        explorer_set_status("Rename canceled");
        return;
    }

    written = snprintf(new_path, sizeof(new_path), "%s/%s", E.cwd, new_name);
    if (written < 0 || (size_t)written >= sizeof(new_path)) {
        explorer_set_status("New path is too long");
        return;
    }
    if (rename(entry->path, new_path) == -1) {
        explorer_set_status("Cannot rename %s: %s", entry->name, strerror(errno));
        return;
    }
    explorer_load_directory(E.cwd);
    explorer_set_status("Renamed to %s", new_name);
}

static void explorer_make_directory(void)
{
    char name[PATH_MAX];
    char path[PATH_MAX];
    int written;

    name[0] = '\0';
    if (explorer_prompt("New directory name: ", name, sizeof(name)) == -1) {
        explorer_set_status("Mkdir canceled");
        return;
    }

    written = snprintf(path, sizeof(path), "%s/%s", E.cwd, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        explorer_set_status("Directory path is too long");
        return;
    }
    if (mkdir(path, 0777) == -1) {
        explorer_set_status("Cannot create %s: %s", name, strerror(errno));
        return;
    }
    explorer_load_directory(E.cwd);
    explorer_set_status("Created directory %s", name);
}

static void explorer_edit_selected(void)
{
    ExplorerEntry *entry;
    pid_t pid;
    int status;

    entry = explorer_selected_entry();
    if (entry == NULL || entry->is_dir) {
        explorer_set_status("Choose a file to edit");
        return;
    }

    explorer_disable_raw_mode();
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
    pid = fork();
    if (pid == -1) {
        explorer_enable_raw_mode();
        explorer_set_status("Cannot start editor: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        execl(E.edit_path, "edit", entry->path, (char *)NULL);
        execlp("edit", "edit", entry->path, (char *)NULL);
        perror("edit");
        _exit(EXIT_FAILURE);
    }
    if (waitpid(pid, &status, 0) == -1) {
        explorer_set_status("Editor wait failed: %s", strerror(errno));
    } else {
        explorer_set_status("Editor exited with status %d", status);
    }
    explorer_enable_raw_mode();
    explorer_load_directory(E.cwd);
}

static void explorer_process_key(int key)
{
    size_t visible_rows;

    visible_rows = (size_t)(E.rows - 5);
    switch (key) {
        case CTRL_KEY('q'):
        case 'q':
            E.running = 0;
            break;
        case KEY_ARROW_UP:
        case 'k':
            if (E.selected > 0) {
                E.selected--;
            }
            break;
        case KEY_ARROW_DOWN:
        case 'j':
            if (E.selected + 1 < E.entry_count) {
                E.selected++;
            }
            break;
        case KEY_PAGE_UP:
            if (E.selected > visible_rows) {
                E.selected -= visible_rows;
            } else {
                E.selected = 0;
            }
            break;
        case KEY_PAGE_DOWN:
            if (E.entry_count > 0) {
                E.selected += visible_rows;
                if (E.selected >= E.entry_count) {
                    E.selected = E.entry_count - 1;
                }
            }
            break;
        case KEY_HOME:
            E.selected = 0;
            break;
        case KEY_END:
            if (E.entry_count > 0) {
                E.selected = E.entry_count - 1;
            }
            break;
        case KEY_ARROW_RIGHT:
        case '\r':
        case '\n':
            explorer_open_selected();
            break;
        case KEY_ARROW_LEFT:
            explorer_go_parent();
            break;
        case 'h':
            E.show_hidden = !E.show_hidden;
            explorer_load_directory(E.cwd);
            break;
        case 'd':
        case KEY_DELETE:
            explorer_delete_selected();
            break;
        case 'r':
            explorer_rename_selected();
            break;
        case 'n':
            explorer_make_directory();
            break;
        case 'e':
            explorer_edit_selected();
            break;
        default:
            explorer_set_status("Shortcut: arrows move, Enter opens, e edits, r renames, d deletes, n creates, h toggles hidden, q quits.");
            break;
    }
}

static void explorer_handle_signal(int signo)
{
    (void)signo;
    E.running = 0;
}

int main(int argc, char **argv)
{
    const char *start_dir;

    setlocale(LC_CTYPE, "");
    memset(&E, 0, sizeof(E));
    E.running = 1;
    start_dir = argc > 1 ? argv[1] : ".";

    signal(SIGTERM, explorer_handle_signal);
    signal(SIGINT, explorer_handle_signal);
    explorer_enable_raw_mode();

    if (getcwd(E.cwd, sizeof(E.cwd)) == NULL) {
        explorer_die("getcwd");
    }
    if (snprintf(E.edit_path, sizeof(E.edit_path), "%s/apps/edit", E.cwd) < 0) {
        explorer_die("snprintf");
    }
    E.edit_path[sizeof(E.edit_path) - 1] = '\0';
    if (explorer_load_directory(start_dir) == -1) {
        explorer_die("explorer_load_directory");
    }

    while (E.running) {
        explorer_draw_screen();
        explorer_process_key(explorer_read_key());
    }

    explorer_free_entries();
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    return EXIT_SUCCESS;
}
