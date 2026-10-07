#ifndef BUDOWIN_FILE_OPERATIONS_H
#define BUDOWIN_FILE_OPERATIONS_H
#include <sys/socket.h>
#include <limits.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#define BW_JOB_COPY 1
#define BW_JOB_MOVE 2
#define BW_JOB_TRASH 3
#define BW_JOB_PURGE 4
#define BW_JOB_RESTORE 5
#define BW_JOB_PROGRESS 1
#define BW_JOB_CONFLICT 2
#define BW_JOB_RESULT 3
#define BW_JOB_DONE 4
#define BW_JOB_CANCEL 0
#define BW_JOB_REPLACE 1
#define BW_JOB_SKIP 2
#define BW_JOB_KEEP 3

typedef struct BwJobMessage {
    int kind, completed, total, failed, skipped, cancelled, index, success;
    unsigned long long bytes;
    char name[MAX_NAME], detail[256];
} BwJobMessage;
static pid_t file_job_pid;
static int file_job_socket = -1, file_job_child_socket = -1;
static int file_job_kind, file_job_visible, file_job_choice = 1;
static int file_job_cancelled, file_job_finished;
static char file_job_directory[MAX_PATH];
static BwJobMessage file_job_message;
static char file_job_errors[64][256];
static char fs_result_detail[160];
static int file_job_error_count, file_job_error_row;
static unsigned char *file_job_moved;
static int file_job_entry_count;
static unsigned long long file_job_last_progress, file_job_bytes;
static int file_job_active(void) { return file_job_pid > 0; }

static int fs_cancelled(void)
{
    if (file_job_child_socket < 0) return 0;
    int command;
    ssize_t size = recv(file_job_child_socket, &command, sizeof(command), MSG_DONTWAIT);
    if (!size) file_job_cancelled = 1;
    if (size == sizeof(command) && command == BW_JOB_CANCEL) file_job_cancelled = 1;
    if (file_job_cancelled) errno = ECANCELED;
    return file_job_cancelled;
}

static void fs_progress(const char *path)
{
    if (file_job_child_socket < 0) return;
    unsigned long long now = bwa_get_time_ms();
    if (now - file_job_last_progress < 80) return;
    file_job_last_progress = now;
    file_job_message.kind = BW_JOB_PROGRESS;
    file_job_message.bytes = file_job_bytes;
    const char *name = strrchr(path, '/');
    snprintf(file_job_message.name, sizeof(file_job_message.name), "%s", name ? name + 1 : path);
    (void)send(file_job_child_socket, &file_job_message, sizeof(file_job_message), MSG_DONTWAIT | MSG_NOSIGNAL);
}

/* Publishing without replacement must be atomic, including directory copies.
 * Linux provides renameat2; other POSIX hosts can publish files via linkat and
 * safely reject unsupported directory publishing instead of overwriting. */
static int fs_rename_exclusive(const char *source, const char *destination)
{
#if defined(__linux__) && defined(SYS_renameat2)
    if (syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination, 1u) == 0) return 1;
    if (errno != ENOSYS && errno != EINVAL) return 0;
#endif
    struct stat info;
    if (lstat(source, &info) != 0) return 0;
    if (S_ISDIR(info.st_mode)) { errno = ENOTSUP; return 0; }
    if (linkat(AT_FDCWD, source, AT_FDCWD, destination, 0) != 0) return 0;
    return unlink(source) == 0;
}

static void fs_discard_staging(const char *path)
{
    struct stat info;
    if (lstat(path, &info) != 0) return;
    if (!S_ISDIR(info.st_mode)) { (void)unlink(path); return; }
    /* Only newly created, user-owned staging trees enter this function. */
    if (info.st_uid == getuid()) (void)chmod(path, (info.st_mode & 0777) | 0700);
    DIR *directory = opendir(path);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[MAX_PATH];
        if (join_path(child, sizeof(child), path, entry->d_name)) fs_discard_staging(child);
    }
    closedir(directory);
    (void)rmdir(path);
}

/* Exclusive creation and rollback ensure cancellation cannot damage an
 * existing destination. Links are copied as links, never followed. */
static int fs_copy_tree(const char *source, const char *destination)
{
    if (fs_cancelled()) return 0;
    struct stat info;
    if (lstat(source, &info) != 0) return 0;
    fs_progress(source);
    if (S_ISLNK(info.st_mode)) {
        char target[MAX_PATH];
        ssize_t length = readlink(source, target, sizeof(target) - 1);
        if (length < 0) return 0;
        target[length] = 0;
        return symlink(target, destination) == 0;
    }
    if (S_ISREG(info.st_mode)) {
        int input = open(source, O_RDONLY | O_NOFOLLOW);
        if (input < 0) return 0;
        int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, info.st_mode & 0777);
        if (output < 0) { int error = errno; close(input); errno = error; return 0; }
        unsigned char buffer[65536];
        int ok = 1;
        for (;;) {
            if (fs_cancelled()) { ok = 0; break; }
            ssize_t count = read(input, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) { ok = 0; break; }
            if (!count) break;
            ssize_t offset = 0;
            while (offset < count) {
                ssize_t written = write(output, buffer + offset, (size_t)(count - offset));
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) { ok = 0; break; }
                offset += written;
            }
            if (!ok) break;
            file_job_bytes += (unsigned long long)count;
            fs_progress(source);
        }
        struct timespec times[2] = {info.st_atim, info.st_mtim};
        if (ok && (fchmod(output, info.st_mode & 0777) != 0 || futimens(output, times) != 0 || fsync(output) != 0)) ok = 0;
        int error = errno;
        close(input);
        if (close(output) != 0 && ok) { ok = 0; error = errno; }
        if (!ok) unlink(destination);
        errno = error;
        return ok;
    }
    if (!S_ISDIR(info.st_mode)) { errno = ENOTSUP; return 0; }
    if (mkdir(destination, 0700) != 0) return 0;
    DIR *directory = opendir(source);
    int ok = directory != NULL;
    struct dirent *entry;
    while (ok) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { if (errno) ok = 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char from[MAX_PATH], to[MAX_PATH];
        if (!join_path(from, sizeof(from), source, entry->d_name) ||
            !join_path(to, sizeof(to), destination, entry->d_name)) { errno = ENAMETOOLONG; ok = 0; break; }
        ok = fs_copy_tree(from, to);
    }
    int error = errno;
    if (directory && closedir(directory) != 0 && ok) { error = errno; ok = 0; }
    struct timespec times[2] = {info.st_atim, info.st_mtim};
    if (ok && (chmod(destination, info.st_mode & 0777) != 0 || utimensat(AT_FDCWD, destination, times, 0) != 0)) { error = errno; ok = 0; }
    if (!ok) fs_discard_staging(destination);
    errno = error;
    return ok;
}

static int fs_remove_tree(const char *path)
{
    if (fs_cancelled()) return 0;
    struct stat info;
    if (lstat(path, &info) != 0) return 0;
    fs_progress(path);
    if (!S_ISDIR(info.st_mode)) return unlink(path) == 0;
    DIR *directory = opendir(path);
    if (!directory) return 0;
    int ok = 1;
    struct dirent *entry;
    while (ok) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { if (errno) ok = 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[MAX_PATH];
        if (!join_path(child, sizeof(child), path, entry->d_name)) { errno = ENAMETOOLONG; ok = 0; break; }
        ok = fs_remove_tree(child);
    }
    int error = errno;
    if (closedir(directory) != 0 && ok) { error = errno; ok = 0; }
    if (ok) ok = rmdir(path) == 0;
    else errno = error;
    return ok;
}

static int fs_directory(const char *path)
{
    struct stat info;
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return 0;
    if (lstat(path, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077)) {
        errno = EACCES; return 0;
    }
    return 1;
}

static int fs_trash_init(void)
{
    const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME");
    char base[MAX_PATH], root[MAX_PATH];
    if (data && data[0] == '/') snprintf(base, sizeof(base), "%s", data);
    else if (home && home[0] == '/') {
        char local[MAX_PATH];
        if (!join_path(local, sizeof(local), home, ".local")) return 0;
        if (mkdir(local, 0700) != 0 && errno != EEXIST) return 0;
        if (!join_path(base, sizeof(base), local, "share")) return 0;
    } else { errno = ENOENT; return 0; }
    if (mkdir(base, 0700) != 0 && errno != EEXIST) return 0;
    if (!join_path(root, sizeof(root), base, "Trash") || !fs_directory(root) ||
        !join_path(file_job_trash_files, sizeof(file_job_trash_files), root, "files") ||
        !join_path(file_job_trash_info, sizeof(file_job_trash_info), root, "info")) return 0;
    return fs_directory(file_job_trash_files) && fs_directory(file_job_trash_info);
}

static int fs_is_trash(void) { return file_job_trash_files[0] && !strcmp(current_path, file_job_trash_files); }

static int fs_info_path(char *path, size_t size, const char *file)
{
    const char *name = strrchr(file, '/');
    char info[MAX_NAME + 16];
    int count = snprintf(info, sizeof(info), "%s.trashinfo", name ? name + 1 : file);
    return count > 0 && (size_t)count < sizeof(info) && join_path(path, size, file_job_trash_info, info);
}

static int fs_percent_write(FILE *file, const char *path)
{
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (isalnum(*p) || strchr("/-_.~", *p)) { if (fputc(*p, file) == EOF) return 0; }
        else if (fprintf(file, "%%%02X", *p) != 3) return 0;
    }
    return 1;
}

static int fs_original_path(const char *file, char *path, size_t capacity)
{
    char metadata[MAX_PATH], line[MAX_PATH * 3 + 16];
    if (!fs_info_path(metadata, sizeof(metadata), file)) return 0;
    FILE *input = fopen(metadata, "r");
    if (!input) return 0;
    int ok = 0;
    while (fgets(line, sizeof(line), input)) {
        if (strncmp(line, "Path=", 5)) continue;
        size_t used = 0;
        char *p = line + 5;
        while (*p && *p != '\n' && *p != '\r' && used + 1 < capacity) {
            if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                char hex[3] = {p[1], p[2], 0};
                path[used++] = (char)strtoul(hex, NULL, 16);
                p += 3;
            } else path[used++] = *p++;
        }
        path[used] = 0;
        ok = path[0] == '/' && (!*p || *p == '\n' || *p == '\r');
        break;
    }
    fclose(input);
    if (!ok) errno = EINVAL;
    return ok;
}

static int fs_unique_destination(const char *directory, const char *name, char *path, size_t capacity)
{
    char candidate[MAX_NAME];
    for (unsigned int i = 1; i < 1000000; ++i) {
        const char *dot = strrchr(name, '.');
        if (!dot || dot == name) dot = name + strlen(name);
        size_t base = (size_t)(dot - name);
        if (base > 190) base = 190;
        snprintf(candidate, sizeof(candidate), "%.*s (copy %u)%.32s", (int)base, name, i, dot);
        if (!join_path(path, capacity, directory, candidate)) { errno = ENAMETOOLONG; return 0; }
        struct stat info;
        if (lstat(path, &info) != 0 && errno == ENOENT) return 1;
    }
    errno = EEXIST; return 0;
}

static int fs_trash_item_as(const char *source, const char *original)
{
    if (!fs_trash_init()) return 0;
    if (explorer_path_is_same_or_child(source, file_job_trash_files) || explorer_path_is_same_or_child(file_job_trash_files, source)) { errno = EINVAL; return 0; }
    char destination[MAX_PATH], metadata[MAX_PATH], token[MAX_NAME];
    const char *name = strrchr(source, '/');
    snprintf(token, sizeof(token), "%.190s-%lld-%ld", name ? name + 1 : source, (long long)time(NULL), (long)getpid());
    if (!join_path(destination, sizeof(destination), file_job_trash_files, token)) return 0;
    struct stat info;
    if (lstat(destination, &info) == 0 && !fs_unique_destination(file_job_trash_files, token, destination, sizeof(destination))) return 0;
    if (!fs_info_path(metadata, sizeof(metadata), destination)) return 0;
    int fd = open(metadata, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    FILE *output = fdopen(fd, "w");
    if (!output) { close(fd); unlink(metadata); return 0; }
    char date[32];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local);
    int ok = fputs("[Trash Info]\nPath=", output) != EOF && fs_percent_write(output, original) &&
             fprintf(output, "\nDeletionDate=%s\n", date) > 0 && fflush(output) == 0 && fsync(fd) == 0;
    if (fclose(output) != 0) ok = 0;
    if (!ok) { unlink(metadata); return 0; }
    if (fs_rename_exclusive(source, destination)) return 1;
    int error = errno;
    if (error == EXDEV) {
        /* Keep a complete recoverable copy before touching the source. */
        if (fs_copy_tree(source, destination)) {
            if (fs_remove_tree(source)) return 1;
            /* Source may now be partial; retain the complete Trash copy. */
            snprintf(fs_result_detail, sizeof(fs_result_detail), "Removal incomplete; complete backup retained in Trash");
            return 0;
        }
        error = errno;
    }
    unlink(metadata);
    errno = error;
    return 0;
}

static int fs_conflict(const char *name)
{
    file_job_message.kind = BW_JOB_CONFLICT;
    snprintf(file_job_message.name, sizeof(file_job_message.name), "%s", name);
    if (send(file_job_child_socket, &file_job_message, sizeof(file_job_message), MSG_NOSIGNAL) != sizeof(file_job_message)) return BW_JOB_CANCEL;
    int answer = 0;
    ssize_t size;
    do { size = recv(file_job_child_socket, &answer, sizeof(answer), 0); } while (size < 0 && errno == EINTR);
    return size == sizeof(answer) ? answer : BW_JOB_CANCEL;
}

static int fs_transfer(const char *source, const char *destination, int move, int replace)
{
    if (explorer_path_is_same_or_child(destination, source)) { errno = EINVAL; return 0; }
    char stage[MAX_PATH], backup[MAX_PATH];
    int length = snprintf(stage, sizeof(stage), "%s.budowin-%ld", destination, (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(stage)) { errno = ENAMETOOLONG; return 0; }
    if (!fs_copy_tree(source, stage)) return 0;
    if (fs_cancelled()) { fs_discard_staging(stage); return 0; }
    backup[0] = 0;
    if (replace) {
        length = snprintf(backup, sizeof(backup), "%s.budowin-backup-%ld", destination, (long)getpid());
        if (length < 0 || (size_t)length >= sizeof(backup)) { fs_discard_staging(stage); errno = ENAMETOOLONG; return 0; }
        struct stat info;
        if (lstat(backup, &info) == 0 || !fs_rename_exclusive(destination, backup)) {
            int error = errno; fs_discard_staging(stage); errno = error; return 0;
        }
    } else {
        struct stat info;
        if (lstat(destination, &info) == 0) { fs_discard_staging(stage); errno = EEXIST; return 0; }
    }
    if (!fs_rename_exclusive(stage, destination)) {
        int error = errno;
        if (backup[0]) (void)fs_rename_exclusive(backup, destination);
        fs_discard_staging(stage);
        errno = error; return 0;
    }
    if (backup[0]) {
        /* Replacement is recoverable too; failure to trash the old file leaves
         * the backup intact and reports failure instead of discarding it. */
        if (!fs_trash_item_as(backup, destination)) return 0;
    }
    if (move && !fs_remove_tree(source)) return 0;
    return 1;
}

static void file_job_worker(const ExplorerClipboardItem *entries, int count, const char *directory, int operation)
{
    file_job_cancelled = 0; file_job_last_progress = 0; file_job_bytes = 0;
    memset(&file_job_message, 0, sizeof(file_job_message));
    file_job_message.total = count;
    for (int i = 0; i < count && !fs_cancelled(); ++i) {
        const ExplorerClipboardItem *entry = &entries[i];
        int ok = 0, skipped = 0;
        fs_result_detail[0] = 0;
        char destination[MAX_PATH];
        fs_progress(entry->path);
        if (operation == BW_JOB_TRASH) ok = fs_trash_item_as(entry->path, entry->path);
        else if (operation == BW_JOB_PURGE) {
            ok = fs_remove_tree(entry->path);
            if (ok && explorer_path_is_same_or_child(entry->path, file_job_trash_files)) {
                char metadata[MAX_PATH];
                if (fs_info_path(metadata, sizeof(metadata), entry->path)) unlink(metadata);
            }
        } else {
            int restore = operation == BW_JOB_RESTORE;
            ok = restore ? fs_original_path(entry->path, destination, sizeof(destination)) :
                 join_path(destination, sizeof(destination), directory, entry->name);
            if (ok) {
                struct stat info;
                int exists = lstat(destination, &info) == 0;
                int action = exists ? fs_conflict(entry->name) : BW_JOB_KEEP;
                if (action == BW_JOB_CANCEL) { file_job_cancelled = 1; break; }
                if (action == BW_JOB_SKIP) { skipped = 1; ok = 1; }
                else {
                    if (exists && action == BW_JOB_KEEP) {
                        char parent[MAX_PATH];
                        snprintf(parent, sizeof(parent), "%s", destination);
                        char *slash = strrchr(parent, '/');
                        if (slash) *slash = 0;
                        ok = fs_unique_destination(parent, entry->name, destination, sizeof(destination));
                    }
                    if (ok) ok = fs_transfer(entry->path, destination, operation != BW_JOB_COPY, exists && action == BW_JOB_REPLACE);
                    if (ok && restore) {
                        char metadata[MAX_PATH];
                        if (fs_info_path(metadata, sizeof(metadata), entry->path)) unlink(metadata);
                    }
                }
            }
        }
        int error = errno;
        ++file_job_message.completed;
        if (skipped) ++file_job_message.skipped;
        else if (!ok) ++file_job_message.failed;
        file_job_message.kind = BW_JOB_RESULT;
        file_job_message.index = i; file_job_message.success = ok && !skipped;
        snprintf(file_job_message.name, sizeof(file_job_message.name), "%s", entry->name);
        const char *reason = ok ? (skipped ? "skipped" : "completed") : fs_result_detail[0] ? fs_result_detail : error == ECANCELED ? "cancelled; completed items kept" : strerror(error);
        const char *hint = ok || fs_result_detail[0] || error == ECANCELED ? "" :
                           error == EACCES || error == EPERM ? "; check folder write permissions" :
                           error == ENOSPC ? "; free space, then retry" :
                           error == ENOENT ? "; refresh and check both folders" : "; check source and destination";
        snprintf(file_job_message.detail, sizeof(file_job_message.detail), "%.100s: %.100s%.50s", entry->name, reason, hint);
        file_job_message.bytes = file_job_bytes;
        if (send(file_job_child_socket, &file_job_message, sizeof(file_job_message), MSG_NOSIGNAL) < 0) break;
    }
    file_job_message.kind = BW_JOB_DONE;
    file_job_message.cancelled = file_job_cancelled;
    (void)send(file_job_child_socket, &file_job_message, sizeof(file_job_message), MSG_NOSIGNAL);
    close(file_job_child_socket);
    _exit(0);
}

static int file_job_start(const ExplorerClipboardItem *entries, int count, const char *directory, int operation)
{
    if (file_job_active() || count <= 0) return 0;
    if ((operation == BW_JOB_TRASH || operation == BW_JOB_RESTORE) && !fs_trash_init()) {
        snprintf(explorer_error, sizeof(explorer_error), "Trash unavailable: %.100s; original files kept", strerror(errno));
        explorer_status = explorer_error; return 0;
    }
    free(file_job_moved); file_job_moved = NULL; file_job_entry_count = count;
    if (operation == BW_JOB_MOVE) {
        file_job_moved = calloc((size_t)count, 1);
        if (!file_job_moved) return 0;
    }
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) { perror("File operation"); return 0; }
    file_job_pid = fork();
    if (file_job_pid < 0) { close(sockets[0]); close(sockets[1]); file_job_pid = 0; perror("File operation"); return 0; }
    if (!file_job_pid) {
        close(sockets[0]); file_job_child_socket = sockets[1];
        file_job_worker(entries, count, directory, operation);
    }
    close(sockets[1]); file_job_socket = sockets[0];
    (void)fcntl(file_job_socket, F_SETFD, FD_CLOEXEC);
    memset(&file_job_message, 0, sizeof(file_job_message));
    file_job_message.total = count;
    file_job_kind = operation; file_job_visible = 1; file_job_finished = 0;
    file_job_conflict = file_job_cancelled = file_job_error_count = file_job_error_row = 0;
    snprintf(file_job_directory, sizeof(file_job_directory), "%s", current_path);
    return 1;
}

static int file_job_selection(int operation)
{
    if (explorer_shortcut_folder[0]) return explorer_delete_selection();
    ExplorerClipboardItem *entries = calloc((size_t)item_count + 1, sizeof(*entries));
    if (!entries) { explorer_status = "Not enough memory; no files changed."; return 0; }
    int count = 0;
    for (int i = 0; i < item_count; ++i) {
        if (!explorer_selection[i]) continue;
        snprintf(entries[count].path, sizeof(entries[count].path), "%s", items[i].path);
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", items[i].name);
        entries[count++].type = items[i].type;
    }
    int result = file_job_start(entries, count, current_path, operation);
    free(entries);
    return result;
}

static void file_job_send(int command)
{
    if (file_job_socket >= 0) (void)send(file_job_socket, &command, sizeof(command), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (!command) file_job_cancelled = 1;
    file_job_conflict = 0;
}

static int file_job_poll(void)
{
    if (!file_job_active()) return 0;
    if (file_job_socket < 0) {
        int status;
        pid_t reaped = waitpid(file_job_pid, &status, WNOHANG);
        if (reaped == file_job_pid || reaped < 0) file_job_pid = 0;
        return 0;
    }
    BwJobMessage message;
    int changed = 0, finished = 0;
    for (;;) {
        ssize_t count = recv(file_job_socket, &message, sizeof(message), MSG_DONTWAIT);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (count <= 0) { finished = 1; break; }
        if (count != sizeof(message)) continue;
        changed = 1;
        file_job_message = message;
        if (message.kind == BW_JOB_CONFLICT) { file_job_conflict = 1; file_job_choice = 1; }
        if (message.kind == BW_JOB_RESULT && file_job_moved && message.index >= 0 && message.index < file_job_entry_count)
            file_job_moved[message.index] = (unsigned char)message.success;
        if (message.kind == BW_JOB_RESULT && message.failed > file_job_error_count && file_job_error_count < 64)
            snprintf(file_job_errors[file_job_error_count++], 256, "%s", message.detail);
        if (message.kind == BW_JOB_DONE) { finished = 1; file_job_finished = 1; break; }
    }
    if (finished) {
        close(file_job_socket); file_job_socket = -1;
        int status;
        /* DONE precedes exit; defer reaping to the next poll without blocking. */
        pid_t reaped = waitpid(file_job_pid, &status, WNOHANG);
        if (reaped == file_job_pid || reaped < 0) file_job_pid = 0;
        if (file_job_finished) {
            if (!strcmp(current_path, file_job_directory)) (void)load_directory(current_path);
            snprintf(explorer_error, sizeof(explorer_error), "%s: %d completed, %d skipped, %d failed%s",
                     file_job_message.cancelled ? "Cancelled" : "Finished",
                     file_job_message.completed - file_job_message.skipped - file_job_message.failed,
                     file_job_message.skipped, file_job_message.failed,
                     file_job_message.cancelled ? "; completed items kept" : "");
            explorer_status = explorer_error;
            if (file_job_kind == BW_JOB_MOVE && file_job_moved && explorer_clipboard_count == file_job_entry_count) {
                int keep = 0;
                for (int i = 0; i < explorer_clipboard_count; ++i)
                    if (!file_job_moved[i]) explorer_clipboard[keep++] = explorer_clipboard[i];
                explorer_clipboard_count = keep;
                if (!keep) explorer_clipboard_mode = EXPLORER_CLIP_NONE;
            }
            free(file_job_moved); file_job_moved = NULL;
        } else { file_job_finished = 1; file_job_message.failed++; explorer_status = "File worker disconnected; check source and destination."; }
        changed = 1;
    }
    return changed;
}

static void file_job_shutdown(void)
{
    if (!file_job_active()) { free(file_job_moved); file_job_moved = NULL; return; }
    file_job_send(BW_JOB_CANCEL);
    /* Keep the desktop alive until cancellation has cleaned staging files. */
    while (file_job_active()) {
        (void)file_job_poll();
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
    }
}
#endif
