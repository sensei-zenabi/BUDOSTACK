/* Desktop membership is virtual; recycle its records without moving shortcut targets. */
typedef struct DesktopRecycleRecord {
    DesktopItem item;
    int slot;
    char parent[MAX_PATH];
    char icon[MAX_PATH];
} DesktopRecycleRecord;

static void desktop_recycle_descendants(unsigned char *selected)
{
    for (int pass = 0; pass < shortcut_count; ++pass) {
        int changed = 0;
        for (int i = 0; i < shortcut_count; ++i) {
            if (selected[i]) continue;
            for (int j = 0; j < shortcut_count; ++j) {
                if (selected[j] && desktop_shortcuts[j].type == TYPE_FOLDER &&
                    !strcmp(shortcut_details[i].parent, desktop_shortcuts[j].path)) {
                    selected[i] = 1;
                    changed = 1;
                    break;
                }
            }
        }
        if (!changed) break;
    }
}

static DesktopRecycleRecord *desktop_recycle_read(const char *wrapper, int *count)
{
    char path[MAX_PATH], header[64];
    *count = 0;
    if (!join_path(path, sizeof(path), wrapper, "shortcuts")) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    DesktopRecycleRecord *records = calloc(DESKTOP_SHORTCUT_MAX, sizeof(*records));
    int ok = records && fgets(header, sizeof(header), file) && !strcmp(header, "BUDOWIN recycled shortcuts 1\n") &&
        fscanf(file, "%d", count) == 1 && fgetc(file) == '\n' && *count > 0 && *count <= DESKTOP_SHORTCUT_MAX;
    for (int i = 0; ok && i < *count; ++i) {
        DesktopRecycleRecord *record = &records[i];
        ok = fscanf(file, "%d %d", &record->slot, &record->item.type) == 2 && fgetc(file) == '\n' &&
            fread(record->item.path, 1, MAX_PATH, file) == MAX_PATH &&
            fread(record->parent, 1, MAX_PATH, file) == MAX_PATH &&
            fread(record->icon, 1, MAX_PATH, file) == MAX_PATH &&
            memchr(record->item.path, 0, MAX_PATH) && record->item.path[0] == '/' &&
            memchr(record->parent, 0, MAX_PATH) && memchr(record->icon, 0, MAX_PATH) &&
            (record->item.type == TYPE_FOLDER || record->item.type == TYPE_FILE) &&
            record->slot >= 0 && record->slot < desktop_slot_count();
        if (ok) {
            const char *name = strrchr(record->item.path, '/');
            ok = name && name[1] && copy_text(record->item.name, MAX_NAME, name + 1);
        }
    }
    if (fclose(file) != 0) ok = 0;
    if (!ok) { free(records); errno = EINVAL; return NULL; }
    return records;
}

static int desktop_recycle_store(const char *wrapper, int root)
{
    unsigned char selected[DESKTOP_SHORTCUT_MAX] = {0};
    selected[root] = 1;
    desktop_recycle_descendants(selected);
    int count = 0;
    for (int i = 0; i < shortcut_count; ++i) count += selected[i] != 0;
    char path[MAX_PATH];
    if (!join_path(path, sizeof(path), wrapper, "shortcuts")) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) { perror(path); return 0; }
    int ok = fprintf(file, "BUDOWIN recycled shortcuts 1\n%d\n", count) > 0;
    /* The root is first so physical descendants can remain in its payload. */
    for (int pass = -1; pass < shortcut_count && ok; ++pass) {
        int i = pass < 0 ? root : pass;
        if (!selected[i] || (pass >= 0 && i == root)) continue;
        ok = fprintf(file, "%d %d\n", shortcut_slots[i], desktop_shortcuts[i].type) > 0 &&
            fwrite(desktop_shortcuts[i].path, 1, MAX_PATH, file) == MAX_PATH &&
            fwrite(shortcut_details[i].parent, 1, MAX_PATH, file) == MAX_PATH &&
            fwrite(shortcut_details[i].icon, 1, MAX_PATH, file) == MAX_PATH;
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int desktop_recycle_folders(const char *wrapper, int restore)
{
    int count;
    DesktopRecycleRecord *records = desktop_recycle_read(wrapper, &count);
    if (!records) return 0;
    unsigned char moved[DESKTOP_SHORTCUT_MAX] = {0};
    int ok = 1;
    for (int i = 1; i < count; ++i) {
        if (records[i].item.type != TYPE_FOLDER) continue;
        int contained = 0;
        for (int j = 0; j < count; ++j) {
            if (i == j || records[j].item.type != TYPE_FOLDER) continue;
            size_t n = strlen(records[j].item.path);
            if (!strncmp(records[i].item.path, records[j].item.path, n) && records[i].item.path[n] == '/') contained = 1;
        }
        if (contained) continue;
        char name[32], payload[MAX_PATH];
        snprintf(name, sizeof(name), "folder-%d", i);
        if (!join_path(payload, sizeof(payload), wrapper, name) ||
            !recycle_move(restore ? payload : records[i].item.path, restore ? records[i].item.path : payload)) {
            ok = 0;
            break;
        }
        moved[i] = 1;
    }
    if (!ok) {
        int saved = errno;
        for (int i = count - 1; i > 0; --i) {
            if (!moved[i]) continue;
            char name[32], payload[MAX_PATH];
            snprintf(name, sizeof(name), "folder-%d", i);
            if (!join_path(payload, sizeof(payload), wrapper, name) ||
                !recycle_move(restore ? records[i].item.path : payload, restore ? payload : records[i].item.path))
                perror("Rollback recycled desktop folder");
        }
        errno = saved;
    }
    free(records);
    return ok;
}

static int desktop_recycle_restore(const char *wrapper)
{
    int count;
    DesktopRecycleRecord *records = desktop_recycle_read(wrapper, &count);
    if (!records) return errno == ENOENT;
    int old_count = shortcut_count, ok = count <= DESKTOP_SHORTCUT_MAX - old_count;
    if (!ok) errno = ENOSPC;
    for (int i = 0; ok && i < count; ++i) {
        for (int j = 0; j < old_count; ++j) {
            if (!strcmp(records[i].item.path, desktop_shortcuts[j].path)) { errno = EEXIST; ok = 0; break; }
        }
        if (!ok) break;
        int index = shortcut_count++;
        desktop_shortcuts[index] = records[i].item;
        memset(&shortcut_details[index], 0, sizeof(shortcut_details[index]));
        copy_text(shortcut_details[index].parent, MAX_PATH, records[i].parent);
        copy_text(shortcut_details[index].icon, MAX_PATH, records[i].icon);
        shortcut_slots[index] = records[i].slot;
        int occupied = !records[i].parent[0] &&
            (records[i].slot == explorer_desktop_slot || records[i].slot == editor_desktop_slot);
        if (!records[i].parent[0])
            for (int j = 0; j < bwa_external_app_count; ++j)
                if (bwa_external_apps[j].desktop_slot == records[i].slot) occupied = 1;
        for (int j = 0; j < index; ++j)
            if (shortcut_slots[j] == records[i].slot && !strcmp(shortcut_details[j].parent, records[i].parent)) occupied = 1;
        if (occupied) shortcut_slots[index] = desktop_free_slot(records[i].parent, index);
        if (shortcut_slots[index] < 0) {
            errno = ENOSPC;
            ok = 0;
        }
    }
    int folders_restored = ok && desktop_recycle_folders(wrapper, 1);
    if (folders_restored) ok = shortcuts_save();
    else ok = 0;
    if (!ok) {
        int saved = errno;
        shortcut_count = old_count;
        if (folders_restored && !desktop_recycle_folders(wrapper, 0)) perror("Rollback desktop folder restore");
        errno = saved;
    } else {
        for (int i = old_count; i < shortcut_count; ++i) (void)shortcut_icon_load(i);
    }
    free(records);
    return ok;
}
