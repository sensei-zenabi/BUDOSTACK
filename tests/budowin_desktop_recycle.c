#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void add(const char *path, int type, const char *parent, int slot)
{
    int i = shortcut_count++;
    assert(copy_text(desktop_shortcuts[i].path, MAX_PATH, path));
    assert(copy_text(shortcut_details[i].parent, MAX_PATH, parent));
    desktop_shortcuts[i].type = type;
    shortcut_slots[i] = slot;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(join_path(state_directory, sizeof(state_directory), argv[1], "desktop-recycle-state"));
    assert(mkdir(state_directory, 0700) == 0);
    char folder[MAX_PATH], child[MAX_PATH], target[MAX_PATH], physical[MAX_PATH], state[MAX_PATH], bin[MAX_PATH];
    assert(join_path(folder, sizeof(folder), state_directory, "Folder"));
    assert(join_path(child, sizeof(child), state_directory, "VirtualChild"));
    assert(join_path(target, sizeof(target), state_directory, "target.txt"));
    assert(join_path(physical, sizeof(physical), folder, "contents.txt"));
    assert(mkdir(folder, 0700) == 0 && mkdir(child, 0700) == 0);
    FILE *file = fopen(target, "wb");
    assert(file && fputs("target preserved", file) >= 0 && fclose(file) == 0);
    file = fopen(physical, "wb");
    assert(file && fputs("physical content", file) >= 0 && fclose(file) == 0);
    add(folder, TYPE_FOLDER, "", 2);
    add(child, TYPE_FOLDER, folder, 0);
    add(target, TYPE_FILE, child, 0);
    add("/unrelated", TYPE_FILE, "", 3);
    assert(shortcuts_save());
    shortcut_selection[0] = 1;
    /* A failed save rolls back folder payloads and all virtual membership. */
    assert(copy_text(state, MAX_PATH, bw_state_file("shortcuts.state")));
    assert(unlink(state) == 0 && mkdir(state, 0700) == 0);
    assert(!shortcuts_delete_selected());
    assert(shortcut_count == 4 && access(folder, F_OK) == 0 && access(child, F_OK) == 0);
    assert(!strcmp(shortcut_details[2].parent, child));
    assert(rmdir(state) == 0 && shortcuts_save());
    /* A failed recycle operation does not remove or promote any shortcuts. */
    assert(copy_text(bin, MAX_PATH, bw_state_file("RecycleBin")));
    assert(rmdir(bin) == 0);
    file = fopen(bin, "wb");
    assert(file && fclose(file) == 0);
    assert(!shortcuts_delete_selected() && shortcut_count == 4);
    assert(unlink(bin) == 0);
    assert(shortcuts_delete_selected());
    assert(shortcut_count == 1 && !strcmp(desktop_shortcuts[0].path, "/unrelated"));
    assert(access(folder, F_OK) != 0 && access(child, F_OK) != 0 && access(target, F_OK) == 0);
    shortcut_count = 0;
    shortcuts_load();
    assert(shortcut_count == 1);
    recycle_bin = 1;
    assert(recycle_load() && item_count == 1 && directory_items[0].type == TYPE_FOLDER);
    char payload[MAX_PATH];
    assert(join_path(payload, MAX_PATH, directory_items[0].path, "contents.txt"));
    assert(access(payload, F_OK) == 0);
    /* Restoring must also be atomic when shortcut persistence fails. */
    assert(unlink(state) == 0 && mkdir(state, 0700) == 0);
    explorer_selection[0] = 1;
    assert(!recycle_restore() && shortcut_count == 1 && item_count == 1);
    assert(access(folder, F_OK) != 0 && access(child, F_OK) != 0);
    assert(rmdir(state) == 0 && shortcuts_save());
    /* An occupied original desktop slot is reassigned on restore. */
    shortcut_slots[0] = 2;
    explorer_selection[0] = 1;
    assert(recycle_restore() && item_count == 0 && shortcut_count == 4);
    assert(shortcut_slots[1] != 2);
    assert(!strcmp(desktop_shortcuts[1].path, folder) && !strcmp(shortcut_details[2].parent, folder));
    assert(!strcmp(shortcut_details[3].parent, child));
    assert(access(physical, F_OK) == 0 && access(child, F_OK) == 0 && access(target, F_OK) == 0);
    shortcut_count = 0;
    shortcuts_load();
    assert(shortcut_count == 4 && !strcmp(shortcut_details[3].parent, child));
    /* Selecting both an ancestor and descendant creates one recycle entry. */
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[1] = shortcut_selection[2] = 1;
    assert(shortcuts_delete_selected() && shortcut_count == 1);
    assert(recycle_load() && item_count == 1);
    assert(recycle_empty() && item_count == 0 && access(target, F_OK) == 0);
    /* Physical nesting and the Explorer deletion route use the same workflow. */
    assert(mkdir(folder, 0700) == 0);
    assert(join_path(child, MAX_PATH, folder, "PhysicalChild"));
    assert(mkdir(child, 0700) == 0);
    add(folder, TYPE_FOLDER, "", 4);
    add(child, TYPE_FOLDER, folder, 0);
    add(target, TYPE_FILE, child, 0);
    assert(shortcuts_save());
    recycle_bin = 0;
    assert(load_directory(folder) && item_count == 1);
    explorer_selection[0] = 1;
    assert(explorer_delete_selection() && shortcut_count == 2 && item_count == 0);
    assert(access(child, F_OK) != 0 && access(target, F_OK) == 0);
    recycle_bin = 1;
    assert(recycle_load() && item_count == 1);
    explorer_selection[0] = 1;
    assert(recycle_restore() && shortcut_count == 4 && access(child, F_OK) == 0);
    memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
    shortcut_selection[1] = 1;
    assert(shortcuts_delete_selected() && shortcut_count == 1);
    assert(recycle_load() && item_count == 1);
    explorer_selection[0] = 1;
    assert(recycle_restore() && shortcut_count == 4 && access(child, F_OK) == 0);
    puts("PASS: desktop folders recycle and restore nested icons, preserve targets and roll back failures");
    return 0;
}
