/* Each entry has a private wrapper with its original absolute path and payload.
 * Metadata is committed before moving the payload. Symlinks are never followed
 * during copy, deletion or collision checks. */
static int explorer_delete_path(const char *path, int type);

static int recycle_root(char *path)
{
    if (!copy_text(path, MAX_PATH, bw_state_file("RecycleBin"))) return 0;
    struct stat info;
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return 0;
    if (lstat(path, &info) != 0 || !S_ISDIR(info.st_mode)) { errno = ENOTDIR; return 0; }
    return 1;
}

static int recycle_copy(const char *source, const char *dest)
{
    struct stat info;
    if (lstat(source, &info) != 0) return 0;
    if (S_ISLNK(info.st_mode)) {
        char target[MAX_PATH];
        ssize_t n = readlink(source, target, sizeof(target) - 1);
        if (n < 0 || (size_t)n >= sizeof(target) - 1) return 0;
        target[n] = 0;
        return symlink(target, dest) == 0;
    }
    if (S_ISDIR(info.st_mode)) {
        if (mkdir(dest, 0700) != 0) return 0;
        DIR *dir = opendir(source);
        if (!dir) return 0;
        int ok = 1;
        struct dirent *entry;
        for (;;) {
            errno = 0;
            entry = readdir(dir);
            if (!entry) { if (errno) ok = 0; break; }
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char from[MAX_PATH], to[MAX_PATH];
            if (!join_path(from, sizeof(from), source, entry->d_name) ||
                !join_path(to, sizeof(to), dest, entry->d_name) || !recycle_copy(from, to)) { ok = 0; break; }
        }
        if (closedir(dir) != 0) ok = 0;
        if (ok && chmod(dest, info.st_mode & 0777) != 0) ok = 0;
        return ok;
    }
    if (!S_ISREG(info.st_mode)) { errno = ENOTSUP; return 0; }
    int in = open(source, O_RDONLY | O_NOFOLLOW);
    if (in < 0) return 0;
    int out = open(dest, O_WRONLY | O_CREAT | O_EXCL, info.st_mode & 0777);
    int ok = out >= 0;
    char buffer[16384];
    while (ok) {
        ssize_t n = read(in, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = 0; break; }
        if (!n) break;
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t written = write(out, buffer + offset, (size_t)(n - offset));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = 0; break; }
            offset += written;
        }
    }
    if (ok && fsync(out) != 0) ok = 0;
    if (out >= 0 && close(out) != 0) ok = 0;
    if (close(in) != 0) ok = 0;
    return ok;
}

static int recycle_move(const char *source, const char *dest)
{
    struct stat info;
    if (lstat(dest, &info) == 0) { errno = EEXIST; return 0; }
    if (errno != ENOENT) return 0;
    if (rename(source, dest) == 0) return 1;
    if (errno != EXDEV) return 0;
    if (!recycle_copy(source, dest)) {
        int saved = errno;
        (void)explorer_delete_path(dest, TYPE_FILE);
        errno = saved;
        return 0;
    }
    /* A failed source cleanup leaves the complete recovery copy available. */
    if (!explorer_delete_path(source, TYPE_FILE)) {
        fprintf(stderr, "Recycle Bin: recovery copy retained at %s\n", dest);
    }
    return 1;
}

static int recycle_put(const char *source)
{
    char root[MAX_PATH], absolute[MAX_PATH], parent[MAX_PATH];
    char wrapper[MAX_PATH], metadata[MAX_PATH], payload[MAX_PATH];
    /* Resolve the parent only: realpath(source) would follow the final symlink. */
    if (!copy_text(parent, sizeof(parent), source)) return 0;
    char *slash = strrchr(parent, '/');
    const char *name = strrchr(source, '/');
    name = name ? name + 1 : source;
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) { errno = EINVAL; return 0; }
    if (slash) { if (slash == parent) slash[1] = 0; else *slash = 0; }
    else copy_text(parent, sizeof(parent), ".");
    char resolved[MAX_PATH];
    if (!realpath(parent, resolved) || !join_path(absolute, sizeof(absolute), resolved, name) || !recycle_root(root)) return 0;
    size_t n = strlen(root);
    size_t source_len = strlen(absolute);
    if (!strncmp(root, absolute, source_len) && root[source_len] == '/') { errno = EINVAL; return 0; }
    if (!strcmp(absolute, root) || (!strncmp(absolute, root, n) && absolute[n] == '/')) { errno = EINVAL; return 0; }
    if (!join_path(wrapper, sizeof(wrapper), root, "entry-XXXXXX") || !mkdtemp(wrapper)) return 0;
    int ok = join_path(metadata, sizeof(metadata), wrapper, "origin") && join_path(payload, sizeof(payload), wrapper, "payload");
    FILE *file = ok ? fopen(metadata, "wb") : NULL;
    if (!file) ok = 0;
    else {
        size_t length = strlen(absolute) + 1;
        ok = fwrite(absolute, 1, length, file) == length;
        if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = 0;
        if (fclose(file) != 0) ok = 0;
    }
    if (ok) ok = recycle_move(absolute, payload);
    if (!ok) {
        int saved = errno;
        (void)explorer_delete_path(wrapper, TYPE_FOLDER);
        errno = saved;
        perror(source);
    }
    return ok;
}

static int recycle_origin(const char *wrapper, char *origin)
{
    char metadata[MAX_PATH];
    if (!join_path(metadata, sizeof(metadata), wrapper, "origin")) return 0;
    int fd = open(metadata, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 0;
    ssize_t n = read(fd, origin, MAX_PATH);
    int ok = n > 1 && n < MAX_PATH && origin[0] == '/' && origin[n - 1] == 0 &&
             strlen(origin) == (size_t)n - 1;
    if (close(fd) != 0) ok = 0;
    return ok;
}

static int recycle_load(void)
{
    char root[MAX_PATH];
    if (!recycle_root(root)) { perror("Recycle Bin"); return 0; }
    DIR *dir = opendir(root);
    if (!dir) { perror(root); return 0; }
    copy_text(current_path, sizeof(current_path), root);
    item_count = 0;
    explorer_status = NULL;
    explorer_shortcut_folder[0] = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && item_count < MAX_ITEMS) {
        if (strncmp(entry->d_name, "entry-", 6)) continue;
        char wrapper[MAX_PATH], origin[MAX_PATH], payload[MAX_PATH];
        struct stat info;
        if (!join_path(wrapper, sizeof(wrapper), root, entry->d_name) ||
            lstat(wrapper, &info) != 0 || !S_ISDIR(info.st_mode) ||
            !recycle_origin(wrapper, origin) ||
            !join_path(payload, sizeof(payload), wrapper, "payload") || lstat(payload, &info) != 0) continue;
        const char *name = strrchr(origin, '/');
        if (!name || !name[1]) continue;
        DesktopItem *item = &directory_items[item_count++];
        copy_text(item->name, sizeof(item->name), name + 1);
        copy_text(item->path, sizeof(item->path), payload);
        item->type = S_ISDIR(info.st_mode) ? TYPE_FOLDER : TYPE_FILE;
    }
    if (closedir(dir) != 0) perror(root);
    qsort(directory_items, (size_t)item_count, sizeof(directory_items[0]), compare_items);
    explorer_up_selected = 0;
    budo_selection_clear(&explorer_select);
    return 1;
}

static int recycle_restore(void)
{
    if (!recycle_bin) return 0;
    int restored = 0, failed = 0;
    for (int i = 0; i < item_count; ++i) {
        if (!explorer_selection[i]) continue;
        char wrapper[MAX_PATH], origin[MAX_PATH];
        copy_text(wrapper, sizeof(wrapper), directory_items[i].path);
        char *slash = strrchr(wrapper, '/');
        if (!slash) { failed = 1; continue; }
        *slash = 0;
        if (!recycle_origin(wrapper, origin) || !recycle_move(directory_items[i].path, origin)) {
            snprintf(explorer_error, sizeof(explorer_error), "Restore failed: %.80s: %.40s", directory_items[i].name, strerror(errno));
            failed = 1;
            continue;
        }
        ++restored;
        if (!explorer_delete_path(wrapper, TYPE_FOLDER)) failed = 1;
    }
    (void)recycle_load();
    explorer_status = failed ? explorer_error : restored ? "Selected entries restored." : "Select entries to restore.";
    return restored > 0;
}

static int recycle_empty(void)
{
    if (!recycle_bin) return 0;
    char root[MAX_PATH];
    if (!recycle_root(root)) return 0;
    DIR *dir = opendir(root);
    if (!dir) { perror(root); return 0; }
    int ok = 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "entry-", 6)) continue;
        char wrapper[MAX_PATH];
        if (!join_path(wrapper, sizeof(wrapper), root, entry->d_name) || !explorer_delete_path(wrapper, TYPE_FOLDER)) ok = 0;
    }
    if (closedir(dir) != 0) ok = 0;
    (void)recycle_load();
    explorer_status = ok ? "Recycle Bin emptied." : "Some entries could not be deleted.";
    return ok;
}
