#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static int selected_count(void)
{
    int count = 0;
    for (int i = 0; i < item_count; ++i) count += explorer_selection[i] != 0;
    return count;
}

static void menu_choose(int item, int *page)
{
    assert(desktop_selection_pointer(context_x + 4, context_y + 3 + item * 16, 1, page));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(join_path(state_directory, sizeof(state_directory), argv[1], "shortcut-state"));
    assert(mkdir(state_directory, 0700) == 0);
    char directory[MAX_PATH], path[MAX_PATH], target[MAX_PATH], executable[MAX_PATH];
    assert(join_path(directory, sizeof(directory), argv[1], "shortcut-files"));
    assert(mkdir(directory, 0700) == 0);
    assert(join_path(path, sizeof(path), directory, "folder"));
    assert(mkdir(path, 0700) == 0);
    for (int i = 0; i < 60; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "file-%02d.txt", i);
        assert(join_path(path, sizeof(path), directory, name));
        FILE *file = fopen(path, "wb");
        assert(file);
        assert(fclose(file) == 0);
    }
    assert(join_path(executable, sizeof(executable), directory, "run-me"));
    FILE *file = fopen(executable, "wb");
    assert(file);
    assert(fclose(file) == 0);
    assert(chmod(executable, 0700) == 0);
    assert(load_directory(directory));
    int page = 0;
    explorer_navigate(71, &page);
    int first = explorer_selected_item;
    assert(selected_count() == 1);
    keys[225] = 1;
    explorer_navigate(77, &page);
    explorer_navigate(77, &page);
    assert(selected_count() == 3 && explorer_anchor == first);
    explorer_navigate(75, &page);
    assert(selected_count() == 2 && explorer_anchor == first);
    keys[225] = 0;
    keys[224] = 1;
    explorer_navigate(77, &page);
    assert(selected_count() == 2 && !explorer_selection[explorer_selected_item]);
    keys[225] = 1;
    explorer_navigate(77, &page);
    assert(selected_count() == 4);
    keys[225] = keys[224] = 0;
    explorer_navigate(79, &page);
    assert(selected_count() == 1 && page > 0);
    explorer_navigate(71, &page);
    assert(page == 0);
    explorer_select_all_visible();
    assert(selected_count() == item_count);
    only_executables = 1;
    explorer_select_all_visible();
    assert(selected_count() == 2); /* folder and executable */
    only_executables = 0;
    explorer_clear_selection();
    int x, y;
    slot_position(1, &x, &y);
    budo_selection_drag_begin(&explorer_select, x - 21, y - 3, 0);
    explorer_rubber_update(x + GRID_X_STEP + 30, y + 39, 0);
    assert(selected_count() == 2);
    explorer_select.dragging = 0;
    budo_selection_drag_begin(&explorer_select, x - 21, y - 3, KEYMOD_CTRL);
    explorer_rubber_update(x + 30, y + 39, 0);
    assert(selected_count() == 1);
    explorer_select.dragging = 0;

    explorer_select_item(first, 0);
    assert(selected_count() == 1);
    explorer_select_item(visible_item_at(3), KEYMOD_CTRL);
    assert(selected_count() == 2);
    explorer_select_item(visible_item_at(3), KEYMOD_CTRL);
    assert(selected_count() == 1 && explorer_selected_item == visible_item_at(3));
    explorer_select_item(first, 0);
    explorer_select_item(visible_item_at(5), KEYMOD_SHIFT);
    assert(selected_count() == 6 && explorer_anchor == first);
    explorer_select_item(visible_item_at(2), KEYMOD_SHIFT);
    assert(selected_count() == 3 && explorer_anchor == first);

    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    explorer_selection[first] = 1;
    int before = selected_count();
    assert(desktop_selection_pointer(x, y + 2, 2, &page));
    assert(context_menu == 1 && selected_count() == before);
    editor_window.open = 1;
    editor_window.x = explorer_window.x;
    editor_window.y = explorer_window.y;
    assert(desktop_point_owner(x, y) == APP_EXPLORER);
    active_window = APP_EDITOR;
    assert(desktop_point_owner(x, y) == APP_EDITOR);
    active_window = APP_EXPLORER;
    editor_window.open = 0;

    slot_position(4, &x, &y);
    assert(desktop_selection_pointer(x, y + 2, 2, &page));
    assert(selected_count() == 1);
    assert(copy_text(target, sizeof(target), directory_items[explorer_selected_item].path));
    menu_choose(6, &page);
    assert(shortcut_count == 1);
    assert(!strcmp(desktop_shortcuts[0].path, target));
    assert(!shortcuts_create()); /* no duplicate */
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, executable)) explorer_selection[i] = 1;
    assert(shortcuts_create() && shortcut_count == 2);
    shortcut_count = 0;
    shortcuts_load();
    assert(shortcut_count == 2);
    assert(load_directory(argv[1])); /* executable must not depend on Explorer directory */
    shortcut_open(1, page);
    /* Endpoint creation is restricted here; concurrent launch is covered by the PTY test. */
    assert(terminal_window.open && active_window == APP_TERMINAL);
    assert(!strcmp(terminal_cwd, directory) && !launch_command[0]);
    close_terminal();
    shortcut_launch_requested = 0;
    launch_command[0] = '\0';
    explorer_window.open = 0;
    desktop_slot_position(shortcut_slots[0], &x, &y);
    assert(desktop_selection_pointer(x, y, 1, &page));
    assert(shortcut_selection[0]);
    int second_x, second_y;
    desktop_slot_position(shortcut_slots[1], &second_x, &second_y);
    keys[224] = 1;
    assert(desktop_selection_pointer(second_x, second_y, 1, &page));
    assert(shortcut_selection[0] && shortcut_selection[1]);
    assert(desktop_selection_pointer(second_x, second_y, 1, &page));
    assert(shortcut_selection[0] && !shortcut_selection[1]);
    keys[224] = 0;

    assert(desktop_selection_pointer(x, y, 2, &page));
    assert(context_menu == 2);
    menu_choose(1, &page);
    assert(shortcut_count == 1 && access(target, F_OK) == 0);
    shortcut_count = 0;
    shortcuts_load();
    assert(shortcut_count == 1 && !strcmp(desktop_shortcuts[0].path, executable));
    desktop_slot_position(shortcut_slots[0], &x, &y);
    desktop_last_click = -1;
    assert(desktop_selection_pointer(x, y, 1, &page));
    assert(desktop_selection_pointer(x, y, 1, &page));
    assert(terminal_window.open && active_window == APP_TERMINAL && !launch_command[0]);
    close_terminal();
    shortcut_launch_requested = 0;
    launch_command[0] = '\0';
    assert(load_directory(directory));
    explorer_clear_selection();
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, target)) explorer_select_item(i, 0);
    assert(shortcuts_create() && shortcut_count == 2);
    memset(shortcut_selection, 1, DESKTOP_SHORTCUT_MAX);
    desktop_slot_position(shortcut_slots[0], &x, &y);
    assert(desktop_selection_pointer(x, y, 2, &page));
    menu_choose(1, &page);
    assert(shortcut_count == 0 && access(target, F_OK) == 0 && access(executable, F_OK) == 0);
    shortcuts_load();
    assert(shortcut_count == 0);
    explorer_clear_selection();
    for (int i = 0; i < item_count; ++i)
        if (!strcmp(directory_items[i].path, executable)) explorer_select_item(i, 0);
    assert(shortcuts_create() && shortcut_count == 1);
    /* Failed persistence must leave the visible shortcut intact. */
    assert(copy_text(path, sizeof(path), bw_state_file("shortcuts.state")));
    assert(unlink(path) == 0 && mkdir(path, 0700) == 0);
    desktop_slot_position(shortcut_slots[0], &x, &y);
    assert(desktop_selection_pointer(x, y, 2, &page));
    menu_choose(1, &page);
    assert(shortcut_count == 1 && access(executable, F_OK) == 0);
    assert(rmdir(path) == 0);
    file = fopen(path, "wb");
    assert(file);
    assert(fputs("BUDOWIN shortcuts 1\n65\n", file) >= 0);
    assert(fclose(file) == 0);
    shortcuts_load();
    assert(shortcut_count == 0);
    puts("BUDOWIN shortcuts and selection tests passed");
    return 0;
}
