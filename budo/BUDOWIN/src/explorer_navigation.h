/* Host Explorer state. Included after the shared drawing/file helpers. */
#define EXPLORER_HISTORY_MAX 64
typedef struct ExplorerLocation {
    char path[MAX_PATH];
    char focused[MAX_PATH];
    int page;
} ExplorerLocation;
static ExplorerLocation explorer_history[EXPLORER_HISTORY_MAX];
static int explorer_history_count, explorer_history_index = -1;
static int explorer_history_replaying;
static int *explorer_page_pointer;
static char explorer_location[MAX_PATH], explorer_query[MAX_NAME];
static int explorer_field, explorer_field_cursor, explorer_field_selected;
static int explorer_sort_column, explorer_sort_reverse;
static int explorer_name_column = 220, explorer_column_drag;
static int explorer_size_column = 78, explorer_date_column = 102;
static int explorer_column_start_x, explorer_column_start_width;

static int explorer_compare_metadata(const DesktopItem *a, const DesktopItem *b)
{
    int result = 0;
    if (explorer_sort_column == 1) result = (a->size > b->size) - (a->size < b->size);
    else if (explorer_sort_column == 2) result = (a->modified > b->modified) - (a->modified < b->modified);
    else if (explorer_sort_column == 3) result = (a->mode > b->mode) - (a->mode < b->mode);
    if (!result) result = compare_names_ci(a->name, b->name);
    return explorer_sort_reverse ? -result : result;
}

static int explorer_matches(const DesktopItem *item)
{
    if (!explorer_query[0]) return 1;
    char name[MAX_NAME];
    bw_text_decode(name, item->name);
    size_t length = strlen(explorer_query);
    for (const char *p = name; *p; ++p) {
        size_t i = 0;
        while (i < length && p[i] && tolower((unsigned char)p[i]) ==
               tolower((unsigned char)explorer_query[i])) ++i;
        if (i == length) return 1;
    }
    return 0;
}

static void explorer_history_remember(void)
{
    if (explorer_history_index < 0) return;
    ExplorerLocation *entry = &explorer_history[explorer_history_index];
    entry->page = explorer_page_pointer ? *explorer_page_pointer : 0;
    entry->focused[0] = 0;
    if (explorer_selected_item >= 0 && explorer_selected_item < item_count)
        snprintf(entry->focused, sizeof(entry->focused), "%s", items[explorer_selected_item].path);
}

/* Build a complete snapshot before committing; unreadable directories leave
 * the old path, selection and contents intact. One readdir pass, no cap. */
static int explorer_load_directory(const char *path)
{
    char resolved[MAX_PATH];
    if (!realpath(path, resolved)) {
        snprintf(explorer_error, sizeof(explorer_error), "Cannot open folder: %.110s", strerror(errno));
        explorer_status = explorer_error;
        return 0;
    }
    DIR *directory = opendir(resolved);
    if (!directory) {
        snprintf(explorer_error, sizeof(explorer_error), "Cannot open folder: %.110s", strerror(errno));
        explorer_status = explorer_error;
        return 0;
    }
    size_t count = 0, capacity = MAX_ITEMS;
    DesktopItem *next = malloc(capacity * sizeof(*next));
    unsigned char *marks = NULL, *snapshot = NULL;
    int ok = next != NULL;
    struct dirent *entry;
    while (ok) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { if (errno) ok = 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (count == capacity) {
            if (capacity > (size_t)INT_MAX / 2) { errno = ENOMEM; ok = 0; break; }
            size_t larger = capacity * 2;
            DesktopItem *grown = realloc(next, larger * sizeof(*next));
            if (!grown) { ok = 0; break; }
            next = grown;
            capacity = larger;
        }
        DesktopItem *item = &next[count];
        memset(item, 0, sizeof(*item));
        struct stat info, target;
        if (!copy_text(item->name, sizeof(item->name), entry->d_name) ||
            !join_path(item->path, sizeof(item->path), resolved, entry->d_name)) {
            errno = ENAMETOOLONG; ok = 0; break;
        }
        if (lstat(item->path, &info) != 0) continue;
        target = info;
        if (S_ISLNK(info.st_mode)) (void)stat(item->path, &target);
        item->type = S_ISDIR(target.st_mode) ? TYPE_FOLDER : TYPE_FILE;
        item->size = info.st_size;
        item->modified = info.st_mtime;
        item->mode = info.st_mode;
        if (file_job_trash_files[0] && !strcmp(resolved, file_job_trash_files)) {
            char original[MAX_PATH];
            if (fs_original_path(item->path, original, sizeof(original))) {
                const char *name = strrchr(original, '/');
                if (name) snprintf(item->name, sizeof(item->name), "%s", name + 1);
            }
        }
        ++count;
    }
    int saved_errno = errno;
    if (closedir(directory) != 0) { saved_errno = errno; ok = 0; }
    /* Virtual desktop folders contain references in addition to real entries. */
    char virtual_folder[MAX_PATH] = "";
    for (int i = 0; i < shortcut_count; ++i)
        if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, resolved))
            snprintf(virtual_folder, sizeof(virtual_folder), "%s", resolved);
    if (ok && virtual_folder[0]) {
        DesktopItem *grown = realloc(next, (capacity + DESKTOP_SHORTCUT_MAX) * sizeof(*next));
        if (!grown) { ok = 0; saved_errno = ENOMEM; }
        else {
            next = grown;
            capacity += DESKTOP_SHORTCUT_MAX;
            for (int i = 0; i < shortcut_count; ++i) {
                if (strcmp(shortcut_details[i].parent, resolved)) continue;
                int duplicate = 0;
                for (size_t j = 0; j < count; ++j)
                    if (!strcmp(next[j].path, desktop_shortcuts[i].path)) duplicate = 1;
                if (!duplicate) next[count++] = desktop_shortcuts[i];
            }
        }
    }
    if (ok) {
        marks = calloc(capacity, 1);
        snapshot = calloc(capacity, 1);
        if (!marks || !snapshot) { ok = 0; saved_errno = ENOMEM; }
    }
    if (!ok) {
        free(next); free(marks); free(snapshot);
        snprintf(explorer_error, sizeof(explorer_error), "Folder unchanged: %.110s", strerror(saved_errno ? saved_errno : ENOMEM));
        explorer_status = explorer_error;
        return 0;
    }
    qsort(next, count, sizeof(*next), compare_items);
    int same = !strcmp(resolved, current_path), focus = -1;
    if (same) {
        for (size_t i = 0; i < count; ++i)
            for (int j = 0; j < item_count; ++j)
                if ((explorer_selection[j] || j == explorer_selected_item) && !strcmp(next[i].path, items[j].path)) {
                    marks[i] = explorer_selection[j];
                    if (j == explorer_selected_item) focus = (int)i;
                    break;
                }
    }
    if (!same && !explorer_history_replaying) explorer_history_remember();
    if (items != initial_items) free(items);
    if (explorer_selection != initial_selection) free(explorer_selection);
    if (explorer_drag_snapshot != initial_snapshot) free(explorer_drag_snapshot);
    items = next; explorer_selection = marks; explorer_drag_snapshot = snapshot;
    item_count = (int)count; item_capacity = (int)capacity;
    explorer_select.selected = marks; explorer_select.snapshot = snapshot;
    explorer_select.capacity = (int)capacity;
    explorer_select.focus = focus; explorer_select.anchor = focus; explorer_select.dragging = 0;
    snprintf(current_path, sizeof(current_path), "%s", resolved);
    bw_text_decode(explorer_location, resolved);
    snprintf(explorer_shortcut_folder, sizeof(explorer_shortcut_folder), "%s", virtual_folder);
    explorer_up_selected = 0;
    explorer_status = NULL;
    if (!same) { explorer_query[0] = 0; explorer_field = 0; }
    if (!explorer_history_replaying && (!same || explorer_history_index < 0)) {
        if (explorer_history_index == EXPLORER_HISTORY_MAX - 1) {
            memmove(explorer_history, explorer_history + 1, (EXPLORER_HISTORY_MAX - 1) * sizeof(*explorer_history));
            --explorer_history_index;
        }
        ++explorer_history_index;
        explorer_history_count = explorer_history_index + 1;
        ExplorerLocation *location = &explorer_history[explorer_history_index];
        memset(location, 0, sizeof(*location));
        snprintf(location->path, sizeof(location->path), "%s", resolved);
    }
    return 1;
}

static int explorer_history_go(int delta, int *page)
{
    int destination = explorer_history_index + delta;
    if (destination < 0 || destination >= explorer_history_count) return 0;
    explorer_history_remember();
    ExplorerLocation *entry = &explorer_history[destination];
    explorer_history_replaying = 1;
    int ok = explorer_load_directory(entry->path);
    explorer_history_replaying = 0;
    if (ok) {
        explorer_history_index = destination;
        *page = entry->page;
        for (int i = 0; i < item_count; ++i)
            if (!strcmp(items[i].path, entry->focused)) {
                explorer_select.focus = explorer_select.anchor = i;
                explorer_selection[i] = 1;
                break;
            }
    }
    return ok;
}
