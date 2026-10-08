#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static unsigned char before_scroll[SCREEN_SIZE];

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
    int x = explorer_window.x + 8 + explorer_folder_indent(index) - explorer_folder_left + (glyph ? 3 : 16);
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
    assert(!strcmp(directory_items[0].name, "contents.txt"));
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
    /* Divider dragging resizes both panes, captures outside the window and clamps
     * the content page while preserving the directory and selections. */
    assert(load_directory(beta));
    explorer_select_item(0, 0);
    int divider_x = explorer_window.x + 8 + explorer_folder_width();
    int divider_y = explorer_folder_y() + 10;
    assert(desktop_scroll_pointer(divider_x + 3, divider_y, BUDO_POINTER_DOWN, &page));
    assert(explorer_divider_dragging && ui_cursor_kind(0, 0) == EXPLORER_DIVIDER_CURSOR);
    assert(desktop_scroll_pointer(divider_x + 83, divider_y, BUDO_POINTER_MOVE, &page));
    assert(explorer_folder_width() == 240 && explorer_client_x() == explorer_window.x + 256);
    assert(!strcmp(current_path, beta) && explorer_selection[0] && !explorer_select.dragging);
    assert(desktop_scroll_pointer(SCREEN_WIDTH + 200, divider_y, BUDO_POINTER_MOVE, &page));
    assert(explorer_client_w() == GRID_X_STEP);
    assert(desktop_scroll_pointer(-100, divider_y, BUDO_POINTER_MOVE, &page));
    assert(explorer_folder_width() == 96 && explorer_client_w() > GRID_X_STEP);
    assert(desktop_scroll_pointer(explorer_window.x + 11, divider_y, BUDO_POINTER_MOVE, &page));
    assert(explorer_folder_width() == 96); /* Requested width zero remains clamped. */
    assert(desktop_scroll_pointer(0, 0, BUDO_POINTER_UP, &page) && !explorer_divider_dragging);
    assert(!desktop_scroll_pointer(0, 0, BUDO_POINTER_MOVE, &page));
    explorer_folder_preferred_width = 240;
    explorer_window.w = WINDOW_MIN_W;
    assert(explorer_folder_width() <= WINDOW_MIN_W - 40 - GRID_X_STEP);
    explorer_window.w = WINDOW_DEFAULT_W;
    assert(explorer_folder_width() == 240);
    explorer_folder_preferred_width = 0;
    /* A long folder label supplies horizontal overflow independently of the
     * content page and vertical tree scroll; hit testing follows its offset. */
    assert(join_path(path, sizeof(path), root,
        "A-very-long-folder-name-that-needs-horizontal-scrolling-to-see-the-end"));
    assert(mkdir(path, 0700) == 0);
    assert(load_directory(root));
    explorer_folder_left = 0;
    explorer_folder_top = 0;
    desktop_scroll_configure(APP_EXPLORER, page);
    assert(budo_scroll_limit(&explorer_folder_hscroll) > 0);
    int tree_top = explorer_folder_top;
    assert(desktop_scroll_pointer(explorer_folder_hscroll.x + explorer_folder_hscroll.length - 3,
        explorer_folder_hscroll.y + 3, BUDO_POINTER_DOWN, &page));
    assert(explorer_folder_left > 0 && explorer_folder_top == tree_top && page == 0);
    desktop_scroll_pointer(0, 0, BUDO_POINTER_UP, &page);
    int thumb_start, thumb_size;
    desktop_scroll_configure(APP_EXPLORER, page);
    budo_scroll_thumb(&explorer_folder_hscroll, &thumb_start, &thumb_size);
    assert(desktop_scroll_pointer(explorer_folder_hscroll.x + thumb_start + 1,
        explorer_folder_hscroll.y + 3, BUDO_POINTER_DOWN, &page));
    assert(explorer_folder_hscroll.dragging);
    assert(desktop_scroll_pointer(SCREEN_WIDTH + 100, explorer_folder_hscroll.y + 3,
        BUDO_POINTER_MOVE, &page));
    assert(explorer_folder_left == budo_scroll_limit(&explorer_folder_hscroll));
    assert(explorer_folder_top == tree_top && page == 0);
    desktop_scroll_pointer(0, 0, BUDO_POINTER_UP, &page);
    assert(!explorer_folder_hscroll.dragging);
    render(argv[1], "folders-scrolled.ppm");
    memcpy(before_scroll, framebuffer, sizeof(before_scroll));
    --explorer_folder_left;
    explorer_folder_draw();
    int left = explorer_window.x + 8;
    int right = left + explorer_folder_width() - BUDO_SCROLL_WIDTH;
    int row_top = explorer_folder_y();
    int row_bottom = row_top + explorer_folder_rows() * 12;
    for (int sy = 0; sy < SCREEN_HEIGHT; ++sy)
        for (int sx = 0; sx < SCREEN_WIDTH; ++sx)
            if (sx < left || sx >= right || sy < row_top || sy >= row_bottom)
                assert(framebuffer[sy * SCREEN_WIDTH + sx] == before_scroll[sy * SCREEN_WIDTH + sx]);
    explorer_folder_left = explorer_folder_indent(folder_row(alpha));
    explorer_folder_last_click[0] = 0;
    click_folder(alpha, 0, &page);
    assert(!strcmp(current_path, alpha));
    int expanded = explorer_folder_expanded(alpha) >= 0;
    click_folder(alpha, 1, &page);
    assert((explorer_folder_expanded(alpha) >= 0) != expanded);
    explorer_folder_left = 10000;
    explorer_folder_preferred_width = explorer_window.w - 40 - GRID_X_STEP;
    desktop_scroll_configure(APP_EXPLORER, page);
    assert(explorer_folder_left == budo_scroll_limit(&explorer_folder_hscroll));
    render(argv[1], "folders-wide.ppm");
    explorer_folder_preferred_width = 0;
    explorer_folder_left = 0;
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
    puts("PASS: folder tree navigation, draggable divider, horizontal/vertical scrolling, modes and refresh");
    return 0;
}
