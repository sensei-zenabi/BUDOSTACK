#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static int folder_row(const char *path)
{
    for (int i = 0; i < explorer_folder_count; ++i)
        if (!strcmp(explorer_folders[i].path, path)) return i;
    return -1;
}

static void click_folder(const char *path, int glyph, int *page)
{
    int index = folder_row(path);
    assert(index >= 0);
    explorer_folder_top = index;
    int x = explorer_window.x + 8 + explorer_folder_indent(index) + (glyph ? 3 : 16);
    assert(explorer_folder_pointer(x, explorer_folder_y() + 4, page));
}

static void render(const char *directory, const char *name)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, name));
    draw_desktop(0);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n640 480\n255\n");
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t color = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
        fputc((color >> 16) & 255, file);
        fputc((color >> 8) & 255, file);
        fputc(color & 255, file);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char root[MAX_PATH], alpha[MAX_PATH], beta[MAX_PATH], child[MAX_PATH], path[MAX_PATH];
    assert(join_path(root, sizeof(root), argv[1], "folder-browser-files"));
    assert(mkdir(root, 0700) == 0);
    assert(join_path(alpha, sizeof(alpha), root, "Alpha"));
    assert(join_path(beta, sizeof(beta), root, "Beta"));
    assert(join_path(child, sizeof(child), alpha, "Nested"));
    assert(mkdir(alpha, 0700) == 0 && mkdir(beta, 0700) == 0 && mkdir(child, 0700) == 0);
    assert(join_path(path, sizeof(path), beta, "contents.txt"));
    FILE *file = fopen(path, "wb");
    assert(file && fputs("content", file) >= 0 && fclose(file) == 0);
    assert(join_path(path, sizeof(path), root, "not-a-folder.txt"));
    file = fopen(path, "wb");
    assert(file && fclose(file) == 0);
    for (int i = 0; i < 35; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Folder-%02d", i);
        assert(join_path(path, sizeof(path), root, name));
        assert(mkdir(path, 0700) == 0);
    }
    set_classic_gui_palette();
    assert(load_directory(child));
    assert(folder_row(child) == explorer_folder_focus);
    assert(explorer_folder_expanded(alpha) >= 0 && explorer_folder_expanded(root) >= 0);
    assert(folder_row(alpha) < folder_row(beta));
    assert(join_path(path, sizeof(path), root, "not-a-folder.txt"));
    assert(folder_row(path) == -1);
    int page = 7;
    click_folder(beta, 0, &page);
    assert(!strcmp(current_path, beta) && page == 0 && item_count == 1);
    assert(!strcmp(items[0].name, "contents.txt"));
    assert(explorer_folder_keyboard && !explorer_folders[folder_row(beta)].expandable);
    /* Expanding another branch preserves the current contents and file selection. */
    explorer_select_item(0, 0);
    click_folder(alpha, 1, &page);
    assert(!strcmp(current_path, beta) && explorer_selection[0]);
    assert(folder_row(child) == -1);
    click_folder(alpha, 1, &page);
    assert(folder_row(child) >= 0 && !strcmp(current_path, beta));
    click_folder(alpha, 0, &page);
    assert(explorer_folder_key(0, 77, &page));
    assert(!strcmp(current_path, child));
    assert(explorer_folder_key(0, 75, &page));
    assert(!strcmp(current_path, alpha));
    assert(explorer_folder_key(0, 75, &page));
    assert(folder_row(child) == -1 && !strcmp(current_path, alpha));
    assert(explorer_folder_key(0, 77, &page));
    assert(folder_row(child) >= 0 && !strcmp(current_path, alpha));
    /* Collapsing an ancestor of the selected folder selects that ancestor. */
    assert(load_directory(child));
    click_folder(root, 1, &page);
    assert(!strcmp(current_path, root) && folder_row(alpha) == -1);
    assert(explorer_folder_key(0, 77, &page));
    assert(folder_row(alpha) >= 0);
    /* Double-click toggles the branch after selecting it. */
    explorer_folder_last_click[0] = 0;
    click_folder(alpha, 0, &page);
    click_folder(alpha, 0, &page);
    assert(!strcmp(current_path, alpha) && folder_row(child) == -1);
    assert(explorer_folder_key(9, 0, &page) && !explorer_folder_keyboard);
    assert(!explorer_folder_key(3, 0, &page));
    assert(explorer_folder_key(9, 0, &page) && explorer_folder_keyboard);
    assert(explorer_folder_key(3, 0, &page));
    assert(explorer_folder_key(0, 79, &page));
    assert(explorer_folder_focus >= explorer_folder_top &&
           explorer_folder_focus < explorer_folder_top + explorer_folder_rows());
    /* Each scrollbar changes only its own pane. */
    explorer_window.open = 1;
    active_window = APP_EXPLORER;
    page = 0;
    explorer_folder_top = 0;
    desktop_scroll_configure(APP_EXPLORER, page);
    assert(desktop_scroll_pointer(explorer_folder_scroll.x + 3,
        explorer_folder_scroll.y + explorer_folder_scroll.length - 3, BUDO_POINTER_DOWN, &page));
    assert(explorer_folder_top > 0 && page == 0);
    desktop_scroll_pointer(0, 0, BUDO_POINTER_UP, &page);
    assert(point_to_slot(explorer_window.x + 10, explorer_folder_y() + 4) == -1);
    /* Filters and view modes do not change folder rows or their geometry. */
    assert(load_directory(root));
    int count = explorer_folder_count, top = explorer_folder_top;
    int y = explorer_folder_y(), rows = explorer_folder_rows();
    only_executables = 1;
    assert(load_directory(root) && explorer_folder_count == count);
    only_executables = 0;
    explorer_list_view = 0;
    render(argv[1], "folders-icons.ppm");
    explorer_list_view = 1;
    assert(explorer_folder_y() == y && explorer_folder_rows() == rows && explorer_folder_top == top);
    render(argv[1], "folders-details.ppm");
    explorer_window.w = WINDOW_MIN_W;
    explorer_window.h = WINDOW_MIN_H;
    render(argv[1], "folders-small.ppm");
    assert(explorer_client_w() > 0 && explorer_cols() > 0 && explorer_folder_rows() > 0);
    assert(join_path(path, sizeof(path), root, "missing"));
    assert(!load_directory(path) && !strcmp(current_path, root));
    assert(explorer_status && strstr(explorer_status, "Cannot open folder"));
    /* Refresh exposes creation, rename and deletion in expanded branches. */
    assert(mkdir(path, 0700) == 0);
    assert(load_directory(root) && folder_row(path) >= 0);
    assert(join_path(child, sizeof(child), root, "renamed"));
    assert(rename(path, child) == 0);
    assert(load_directory(root) && folder_row(path) == -1 && folder_row(child) >= 0);
    assert(rmdir(child) == 0);
    assert(load_directory(root) && folder_row(child) == -1);
    /* A symlink to an ancestor navigates to the canonical path, without a loop. */
    assert(join_path(path, sizeof(path), root, "loop"));
    assert(symlink(root, path) == 0);
    assert(load_directory(path) && !strcmp(current_path, root));
    puts("PASS: folder tree navigation, focus, scrolling, modes, refresh and failed loads");
    return 0;
}
