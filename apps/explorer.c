#define _XOPEN_SOURCE 700
#define BUDOSTACK_LIST_NO_MAIN

#include "../utilities/list.c"
#include "../lib/terminal_layout.h"

#include <ctype.h>
#include <fcntl.h>
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
#define EXPLORER_MIN_ROWS BUDOSTACK_TARGET_ROWS
#define EXPLORER_MIN_COLS BUDOSTACK_TARGET_COLS
#define EXPLORER_RESERVED_ROWS 7
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

enum ExplorerClipboardMode {
    CLIPBOARD_EMPTY = 0,
    CLIPBOARD_COPY,
    CLIPBOARD_MOVE
};

typedef struct {
    char *name;
    char path[PATH_MAX];
    mode_t mode;
    off_t size;
    time_t mtime;
    int is_dir;
    int marked;
} ExplorerEntry;

typedef struct {
    char name[PATH_MAX];
    size_t selected;
    size_t screen_row;
    int has_name;
} ExplorerCursorAnchor;

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
    int selection_mode;
    char **clipboard_paths;
    size_t clipboard_count;
    int clipboard_mode;
} ExplorerState;

static ExplorerState E;

int budostack_terminal_layout_enabled(void)
{
    return 0;
}

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
        char seq[8];
        size_t len = 0;

        if (read(STDIN_FILENO, &seq[len], 1) != 1) {
            return '\x1b';
        }
        len++;
        if (seq[0] != '[' && seq[0] != 'O') {
            return '\x1b';
        }

        while (len < sizeof(seq) && read(STDIN_FILENO, &seq[len], 1) == 1) {
            if ((seq[len] >= 'A' && seq[len] <= 'Z') || seq[len] == '~') {
                len++;
                break;
            }
            len++;
        }

        if (seq[0] == '[' && len >= 2) {
            char final = seq[len - 1];

            switch (final) {
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
                case '~':
                    if (len >= 3) {
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
                    break;
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

static ExplorerEntry *explorer_selected_entry(void);
static size_t explorer_visible_rows(void);
static void explorer_scroll_to_cursor(void);

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

static void explorer_capture_cursor_anchor(ExplorerCursorAnchor *anchor)
{
    ExplorerEntry *entry;

    if (anchor == NULL) {
        return;
    }

    memset(anchor, 0, sizeof(*anchor));
    anchor->selected = E.selected;
    if (E.selected >= E.scroll) {
        anchor->screen_row = E.selected - E.scroll;
    }

    entry = explorer_selected_entry();
    if (entry != NULL) {
        snprintf(anchor->name, sizeof(anchor->name), "%s", entry->name);
        anchor->name[sizeof(anchor->name) - 1] = '\0';
        anchor->has_name = 1;
    }
}

static void explorer_restore_cursor_anchor(const ExplorerCursorAnchor *anchor, const char *preferred_name)
{
    size_t selected = 0;
    size_t screen_row = 0;
    size_t i;

    if (E.entry_count == 0) {
        E.selected = 0;
        E.scroll = 0;
        return;
    }

    if (anchor != NULL) {
        selected = anchor->selected;
        screen_row = anchor->screen_row;
    }
    if (selected >= E.entry_count) {
        selected = E.entry_count - 1;
    }

    if (preferred_name != NULL && preferred_name[0] != '\0') {
        for (i = 0; i < E.entry_count; i++) {
            if (strcmp(E.entries[i].name, preferred_name) == 0) {
                selected = i;
                break;
            }
        }
    } else if (anchor != NULL && anchor->has_name) {
        for (i = 0; i < E.entry_count; i++) {
            if (strcmp(E.entries[i].name, anchor->name) == 0) {
                selected = i;
                break;
            }
        }
    }

    E.selected = selected;
    if (E.selected > screen_row) {
        E.scroll = E.selected - screen_row;
    } else {
        E.scroll = 0;
    }
    explorer_scroll_to_cursor();
}

static int explorer_load_directory_at(const char *dir_path, const ExplorerCursorAnchor *anchor, const char *preferred_name)
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
    explorer_restore_cursor_anchor(anchor, preferred_name);
    explorer_set_status("%zu entries", E.entry_count);
    return 0;
}

static int explorer_load_directory(const char *dir_path)
{
    return explorer_load_directory_at(dir_path, NULL, NULL);
}

static ExplorerEntry *explorer_selected_entry(void)
{
    if (E.entry_count == 0 || E.selected >= E.entry_count) {
        return NULL;
    }
    return &E.entries[E.selected];
}


static int explorer_entry_is_actionable(const ExplorerEntry *entry)
{
    return entry != NULL && strcmp(entry->name, "..") != 0;
}

static size_t explorer_marked_count(void)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < E.entry_count; i++) {
        if (E.entries[i].marked && explorer_entry_is_actionable(&E.entries[i])) {
            count++;
        }
    }
    return count;
}

static void explorer_clear_marks(void)
{
    size_t i;

    for (i = 0; i < E.entry_count; i++) {
        E.entries[i].marked = 0;
    }
}

static void explorer_toggle_mark_selected(void)
{
    ExplorerEntry *entry = explorer_selected_entry();

    if (!explorer_entry_is_actionable(entry)) {
        explorer_set_status("Cannot mark parent directory entry");
        return;
    }
    entry->marked = !entry->marked;
    explorer_set_status("%s %s", entry->marked ? "Marked" : "Unmarked", entry->name);
}

static void explorer_mark_all(void)
{
    size_t i;
    size_t actionable = 0;
    size_t marked = 0;

    for (i = 0; i < E.entry_count; i++) {
        if (explorer_entry_is_actionable(&E.entries[i])) {
            actionable++;
            if (E.entries[i].marked) {
                marked++;
            }
        }
    }

    if (actionable > 0 && marked == actionable) {
        explorer_clear_marks();
        explorer_set_status("Cleared all marks");
        return;
    }

    for (i = 0; i < E.entry_count; i++) {
        if (explorer_entry_is_actionable(&E.entries[i])) {
            E.entries[i].marked = 1;
        }
    }
    explorer_set_status("Marked %zu entries", actionable);
}

static void explorer_free_clipboard(void)
{
    size_t i;

    for (i = 0; i < E.clipboard_count; i++) {
        free(E.clipboard_paths[i]);
    }
    free(E.clipboard_paths);
    E.clipboard_paths = NULL;
    E.clipboard_count = 0;
    E.clipboard_mode = CLIPBOARD_EMPTY;
}

static const char *explorer_basename(const char *path)
{
    const char *slash = strrchr(path, '/');

    if (slash == NULL) {
        return path;
    }
    return slash + 1;
}

static int explorer_join_path(char *output, size_t output_size, const char *dir_path, const char *name)
{
    int written;

    written = snprintf(output, output_size, "%s/%s", dir_path, name);
    if (written < 0 || (size_t)written >= output_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int explorer_copy_file(const char *src, const char *dst, mode_t mode)
{
    int in_fd;
    int out_fd;
    char buffer[16384];
    ssize_t nread;

    in_fd = open(src, O_RDONLY);
    if (in_fd == -1) {
        return -1;
    }
    out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (out_fd == -1) {
        int saved_errno = errno;
        close(in_fd);
        errno = saved_errno;
        return -1;
    }

    while ((nread = read(in_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;

        while (offset < nread) {
            ssize_t nwritten = write(out_fd, buffer + offset, (size_t)(nread - offset));
            if (nwritten == -1) {
                int saved_errno = errno;
                close(in_fd);
                close(out_fd);
                unlink(dst);
                errno = saved_errno;
                return -1;
            }
            offset += nwritten;
        }
    }

    if (nread == -1) {
        int saved_errno = errno;
        close(in_fd);
        close(out_fd);
        unlink(dst);
        errno = saved_errno;
        return -1;
    }
    if (close(in_fd) == -1) {
        int saved_errno = errno;
        close(out_fd);
        errno = saved_errno;
        return -1;
    }
    if (close(out_fd) == -1) {
        return -1;
    }
    return 0;
}

static int explorer_copy_symlink(const char *src, const char *dst)
{
    char target[PATH_MAX];
    ssize_t len;

    len = readlink(src, target, sizeof(target) - 1);
    if (len == -1) {
        return -1;
    }
    target[len] = '\0';
    return symlink(target, dst);
}

static int explorer_copy_recursive(const char *src, const char *dst)
{
    struct stat st;
    struct stat dst_st;
    DIR *dir;
    struct dirent *entry;

    if (lstat(src, &st) == -1) {
        return -1;
    }
    if (lstat(dst, &dst_st) == 0) {
        errno = EEXIST;
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        return explorer_copy_symlink(src, dst);
    }
    if (!S_ISDIR(st.st_mode)) {
        return explorer_copy_file(src, dst, st.st_mode);
    }

    if (mkdir(dst, st.st_mode & 0777) == -1) {
        return -1;
    }
    dir = opendir(src);
    if (dir == NULL) {
        int saved_errno = errno;
        rmdir(dst);
        errno = saved_errno;
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_src[PATH_MAX];
        char child_dst[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (explorer_join_path(child_src, sizeof(child_src), src, entry->d_name) == -1 ||
            explorer_join_path(child_dst, sizeof(child_dst), dst, entry->d_name) == -1 ||
            explorer_copy_recursive(child_src, child_dst) == -1) {
            int saved_errno = errno;
            closedir(dir);
            errno = saved_errno;
            return -1;
        }
    }

    if (closedir(dir) == -1) {
        return -1;
    }
    return 0;
}

static int explorer_delete_tree(const char *path)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;

    if (lstat(path, &st) == -1) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return unlink(path);
    }

    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (explorer_join_path(child, sizeof(child), path, entry->d_name) == -1 || explorer_delete_tree(child) == -1) {
            int saved_errno = errno;
            closedir(dir);
            errno = saved_errno;
            return -1;
        }
    }
    if (closedir(dir) == -1) {
        return -1;
    }
    return rmdir(path);
}

static int explorer_path_contains_destination(const char *src, const char *dst)
{
    size_t src_len = strlen(src);

    return strncmp(src, dst, src_len) == 0 && (dst[src_len] == '/' || dst[src_len] == '\0');
}

static int explorer_move_path(const char *src, const char *dst)
{
    if (explorer_path_contains_destination(src, dst)) {
        errno = EINVAL;
        return -1;
    }
    if (rename(src, dst) == 0) {
        return 0;
    }
    if (errno != EXDEV) {
        return -1;
    }
    if (explorer_copy_recursive(src, dst) == -1) {
        return -1;
    }
    return explorer_delete_tree(src);
}

static int explorer_collect_marked_or_current(char ***paths, size_t *count)
{
    size_t marked = explorer_marked_count();
    size_t capacity = marked > 0 ? marked : 1;
    size_t used = 0;
    char **items;
    size_t i;

    *paths = NULL;
    *count = 0;
    items = calloc(capacity, sizeof(*items));
    if (items == NULL) {
        return -1;
    }

    if (marked > 0) {
        for (i = 0; i < E.entry_count; i++) {
            if (E.entries[i].marked && explorer_entry_is_actionable(&E.entries[i])) {
                items[used] = strdup(E.entries[i].path);
                if (items[used] == NULL) {
                    size_t j;
                    for (j = 0; j < used; j++) {
                        free(items[j]);
                    }
                    free(items);
                    return -1;
                }
                used++;
            }
        }
    } else {
        ExplorerEntry *entry = explorer_selected_entry();

        if (!explorer_entry_is_actionable(entry)) {
            free(items);
            return 0;
        }
        items[0] = strdup(entry->path);
        if (items[0] == NULL) {
            free(items);
            return -1;
        }
        used = 1;
    }

    *paths = items;
    *count = used;
    return 0;
}

static void explorer_set_clipboard(int mode)
{
    char **paths;
    size_t count;

    if (explorer_collect_marked_or_current(&paths, &count) == -1) {
        explorer_set_status("Out of memory while preparing clipboard");
        return;
    }
    if (count == 0) {
        free(paths);
        explorer_set_status("Select a file or folder before copy/move");
        return;
    }

    explorer_free_clipboard();
    E.clipboard_paths = paths;
    E.clipboard_count = count;
    E.clipboard_mode = mode;
    explorer_set_status("%s %zu item%s",
        mode == CLIPBOARD_COPY ? "Copied" : "Prepared move for",
        count,
        count == 1 ? "" : "s");
}

static void explorer_paste_clipboard(void)
{
    ExplorerCursorAnchor anchor;
    size_t i;
    size_t pasted = 0;
    size_t failed = 0;
    char first_error[EXPLORER_STATUS_SIZE] = "";

    explorer_capture_cursor_anchor(&anchor);

    if (E.clipboard_count == 0 || E.clipboard_mode == CLIPBOARD_EMPTY) {
        explorer_set_status("Clipboard is empty");
        return;
    }

    for (i = 0; i < E.clipboard_count; i++) {
        char destination[PATH_MAX];
        const char *name = explorer_basename(E.clipboard_paths[i]);

        if (explorer_join_path(destination, sizeof(destination), E.cwd, name) == -1) {
            failed++;
            if (first_error[0] == '\0') {
                snprintf(first_error, sizeof(first_error), "%s: %s", name, strerror(errno));
            }
            continue;
        }

        if ((E.clipboard_mode == CLIPBOARD_COPY && explorer_copy_recursive(E.clipboard_paths[i], destination) == 0) ||
            (E.clipboard_mode == CLIPBOARD_MOVE && explorer_move_path(E.clipboard_paths[i], destination) == 0)) {
            pasted++;
        } else {
            failed++;
            if (first_error[0] == '\0') {
                snprintf(first_error, sizeof(first_error), "%s: %s", name, strerror(errno));
            }
        }
    }

    if (E.clipboard_mode == CLIPBOARD_MOVE && failed == 0) {
        explorer_free_clipboard();
    }
    explorer_load_directory_at(E.cwd, &anchor, NULL);
    explorer_clear_marks();
    if (failed == 0) {
        explorer_set_status("Pasted %zu item%s", pasted, pasted == 1 ? "" : "s");
    } else {
        explorer_set_status("Pasted %zu item%s, %zu failed (%s)", pasted, pasted == 1 ? "" : "s", failed, first_error);
    }
}

static void explorer_move_cursor(int direction, int mark)
{
    ExplorerEntry *entry;

    if (E.entry_count == 0) {
        return;
    }
    if (mark) {
        entry = explorer_selected_entry();
        if (explorer_entry_is_actionable(entry)) {
            entry->marked = 1;
        }
    }
    if (direction < 0 && E.selected > 0) {
        E.selected--;
    } else if (direction > 0 && E.selected + 1 < E.entry_count) {
        E.selected++;
    }
    if (mark) {
        entry = explorer_selected_entry();
        if (explorer_entry_is_actionable(entry)) {
            entry->marked = 1;
        }
        explorer_set_status("Marked %zu entries", explorer_marked_count());
    }
}

static size_t explorer_visible_rows(void)
{
    int rows = E.rows - EXPLORER_RESERVED_ROWS;

    if (rows < 1) {
        return 1;
    }
    return (size_t)rows;
}

static void explorer_scroll_to_cursor(void)
{
    size_t visible_rows;
    size_t max_scroll;

    visible_rows = explorer_visible_rows();
    if (E.entry_count == 0) {
        E.selected = 0;
        E.scroll = 0;
        return;
    }
    if (E.selected >= E.entry_count) {
        E.selected = E.entry_count - 1;
    }

    if (E.entry_count > visible_rows) {
        max_scroll = E.entry_count - visible_rows;
    } else {
        max_scroll = 0;
    }
    if (E.scroll > max_scroll) {
        E.scroll = max_scroll;
    }
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
    int right_width;
    int right_limit;
    int left_width;
    int space;

    if (E.cols <= 0) {
        return;
    }

    right_width = (int)strlen(right);
    right_limit = E.cols / 3;
    if (right_limit < 12) {
        right_limit = 12;
    }
    if (right_width > right_limit) {
        right_width = right_limit;
    }
    if (right_width >= E.cols) {
        right_width = E.cols - 1;
    }

    space = right_width > 0 ? 1 : 0;
    left_width = E.cols - right_width - space;
    if (left_width < 0) {
        left_width = 0;
    }

    printf("\x1b[%d;1H\x1b[7m", row);
    explorer_draw_truncated(left, left_width);
    explorer_draw_repeated(' ', space);
    if (right_width > 0) {
        explorer_draw_truncated(right, right_width);
    }
    printf("\x1b[0m");
}

static void explorer_draw_plain_bar(const char *text, int row)
{
    printf("\x1b[%d;1H\x1b[7m", row);
    explorer_draw_truncated(text, E.cols);
    printf("\x1b[0m");
}

static void explorer_copy_segment(char *line, size_t line_size, int start, int width, const char *text)
{
    int i;

    if (start < 0 || width <= 0 || (size_t)start >= line_size) {
        return;
    }
    for (i = 0; i < width && text[i] != '\0' && (size_t)(start + i) + 1 < line_size; i++) {
        line[start + i] = text[i];
    }
}

static void explorer_draw_top_bar(const char *left, const char *right)
{
    char line[1024];
    char center[32];
    time_t now;
    struct tm *tm_info;
    int width;
    int center_len;
    int center_start;
    int right_len;
    int right_start;
    int left_width;

    width = E.cols;
    if (width <= 0) {
        return;
    }
    if (width >= (int)sizeof(line)) {
        width = (int)sizeof(line) - 1;
    }
    memset(line, ' ', (size_t)width);
    line[width] = '\0';

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info != NULL) {
        strftime(center, sizeof(center), "%Y-%m-%d %H:%M", tm_info);
    } else {
        snprintf(center, sizeof(center), "date unavailable");
    }

    center_len = (int)strlen(center);
    center_start = (width - center_len) / 2;
    if (center_start < 0) {
        center_start = 0;
    }

    right_len = (int)strlen(right);
    right_start = width - right_len;
    if (right_start < center_start + center_len + 1) {
        right_start = width;
    }

    left_width = center_start - 1;
    if (left_width < 0) {
        left_width = 0;
    }
    explorer_copy_segment(line, sizeof(line), 0, left_width, left);
    explorer_copy_segment(line, sizeof(line), center_start, center_len, center);
    if (right_start < width) {
        explorer_copy_segment(line, sizeof(line), right_start, right_len, right);
    }

    printf("\x1b[1;1H\x1b[7m%.*s\x1b[0m", width, line);
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
    printf("%c ", entry->marked ? '*' : ' ');
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
    char right[128];
    char state[128];
    const char *nav_help;
    const char *action_help;
    size_t marked;

    explorer_get_window_size(&E.rows, &E.cols);
    explorer_scroll_to_cursor();
    visible_rows = explorer_visible_rows();
    marked = explorer_marked_count();
    name_width = E.cols - 45;
    if (name_width < 8) {
        name_width = 8;
    }

    printf("\x1b[?25l\x1b[2J\x1b[H");
    snprintf(title, sizeof(title), " BUDOSTACK Explorer  %s ", E.cwd);
    snprintf(right, sizeof(right), " %zu/%zu  *%zu  clip:%zu ", E.entry_count == 0 ? 0 : E.selected + 1, E.entry_count, marked, E.clipboard_count);
    explorer_draw_top_bar(title, right);

    printf("\x1b[2;1H+");
    explorer_draw_repeated('-', E.cols - 2);
    printf("+");
    printf("\x1b[3;2H  %-*s  %-10s %10s  %-16s", name_width, "Name", "Mode", "Size", "Modified");

    for (i = 0; i < visible_rows; i++) {
        row = (int)i + 4;
        printf("\x1b[%d;1H|", row);
        explorer_draw_repeated(' ', E.cols - 2);
        printf("|");
        if (E.scroll + i < E.entry_count) {
            explorer_draw_entry(&E.entries[E.scroll + i], row, E.scroll + i == E.selected, name_width);
        }
    }

    row = E.rows - 3;
    printf("\x1b[%d;1H+", row);
    explorer_draw_repeated('-', E.cols - 2);
    printf("+");

    snprintf(state, sizeof(state), "hidden:%s  select:%s  clipboard:%zu",
        E.show_hidden ? "on" : "off",
        E.selection_mode ? "on" : "off",
        E.clipboard_count);
    explorer_draw_bar_text(E.status, state, E.rows - 2);

    if (E.cols >= 112) {
        nav_help = "Move: Up/Down or j/k  Page: PgUp/PgDn  Ends: Home/End  Open: Enter  Parent: Left  Mark: Space  All: Ctrl+A  Select: Ctrl+T";
        action_help = "Copy: C  Move: M  Paste: P  New dir: N  Delete: D/Del  Rename: R  Edit: E  Hidden: H  Quit: Q";
    } else {
        nav_help = "Up/Down Move  Enter Open  Left Parent  Space Mark  Ctrl+A All  Ctrl+T Select";
        action_help = "C Copy  M Move  P Paste  N NewDir  D Del  R Rename  E Edit  H Hidden  Q Quit";
    }
    explorer_draw_plain_bar(nav_help, E.rows - 1);
    explorer_draw_plain_bar(action_help, E.rows);
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
    ExplorerCursorAnchor anchor;
    ExplorerEntry *entry;

    entry = explorer_selected_entry();
    if (entry == NULL) {
        explorer_set_status("No entry selected");
        return;
    }
    if (entry->is_dir) {
        explorer_capture_cursor_anchor(&anchor);
        anchor.has_name = 0;
        explorer_load_directory_at(entry->path, &anchor, NULL);
    } else {
        explorer_set_status("File: %s", entry->name);
    }
}

static void explorer_go_parent(void)
{
    ExplorerCursorAnchor anchor;
    char child_name[PATH_MAX];

    if (strcmp(E.cwd, "/") == 0) {
        explorer_set_status("Already at filesystem root");
        return;
    }

    snprintf(child_name, sizeof(child_name), "%s", explorer_basename(E.cwd));
    child_name[sizeof(child_name) - 1] = '\0';
    explorer_capture_cursor_anchor(&anchor);
    explorer_load_directory_at("..", &anchor, child_name);
}

static void explorer_delete_selected(void)
{
    ExplorerCursorAnchor anchor;
    ExplorerEntry *entry;
    char prompt[PATH_MAX + 64];
    char answer[8];
    char deleted_name[PATH_MAX];

    entry = explorer_selected_entry();
    if (entry == NULL || strcmp(entry->name, "..") == 0) {
        explorer_set_status("Choose a normal file or empty directory to delete");
        return;
    }

    explorer_capture_cursor_anchor(&anchor);
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

    explorer_load_directory_at(E.cwd, &anchor, NULL);
    explorer_set_status("Deleted %s", deleted_name);
}

static void explorer_rename_selected(void)
{
    ExplorerCursorAnchor anchor;
    ExplorerEntry *entry;
    char new_name[PATH_MAX];
    char new_path[PATH_MAX];
    int written;

    entry = explorer_selected_entry();
    if (entry == NULL || strcmp(entry->name, "..") == 0) {
        explorer_set_status("Choose a normal file or directory to rename");
        return;
    }

    explorer_capture_cursor_anchor(&anchor);
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
    explorer_load_directory_at(E.cwd, &anchor, new_name);
    explorer_set_status("Renamed to %s", new_name);
}

static void explorer_make_directory(void)
{
    ExplorerCursorAnchor anchor;
    char name[PATH_MAX];
    char path[PATH_MAX];
    int written;

    explorer_capture_cursor_anchor(&anchor);
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
    explorer_load_directory_at(E.cwd, &anchor, NULL);
    explorer_set_status("Created directory %s", name);
}

static void explorer_edit_selected(void)
{
    ExplorerCursorAnchor anchor;
    ExplorerEntry *entry;
    pid_t pid;
    int status;

    entry = explorer_selected_entry();
    explorer_capture_cursor_anchor(&anchor);
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
    explorer_load_directory_at(E.cwd, &anchor, NULL);
}

static void explorer_process_key(int key)
{
    size_t visible_rows;

    visible_rows = explorer_visible_rows();
    switch (key) {
        case CTRL_KEY('q'):
        case 'q':
            E.running = 0;
            break;
        case KEY_ARROW_UP:
        case 'k':
            explorer_move_cursor(-1, E.selection_mode);
            break;
        case KEY_ARROW_DOWN:
        case 'j':
            explorer_move_cursor(1, E.selection_mode);
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
            if (E.selection_mode) {
                explorer_toggle_mark_selected();
            } else {
                explorer_open_selected();
            }
            break;
        case KEY_ARROW_LEFT:
            explorer_go_parent();
            break;
        case CTRL_KEY('a'):
            explorer_mark_all();
            break;
        case CTRL_KEY('t'):
            E.selection_mode = !E.selection_mode;
            explorer_set_status("Selection mode %s", E.selection_mode ? "on" : "off");
            break;
        case ' ':
            explorer_toggle_mark_selected();
            break;
        case 'c':
        case 'C':
            explorer_set_clipboard(CLIPBOARD_COPY);
            break;
        case 'm':
        case 'M':
            explorer_set_clipboard(CLIPBOARD_MOVE);
            break;
        case 'p':
        case 'P':
            explorer_paste_clipboard();
            break;
        case 'h': {
            ExplorerCursorAnchor anchor;

            explorer_capture_cursor_anchor(&anchor);
            E.show_hidden = !E.show_hidden;
            explorer_load_directory_at(E.cwd, &anchor, NULL);
            break;
        }
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
            explorer_set_status("Unknown key");
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
    explorer_free_clipboard();
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    return EXIT_SUCCESS;
}
