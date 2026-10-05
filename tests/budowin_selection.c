#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static uint32_t before[SCREEN_SIZE];
static int opened;

static int open_test_app(void)
{
    ++opened;
    return 1;
}

static void capture(void)
{
    for (int i = 0; i < SCREEN_SIZE; ++i)
        before[i] = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
}

static void assert_icon_changes(int x, int y)
{
    int changed = 0;
    for (int sy = 0; sy < SCREEN_HEIGHT; ++sy) {
        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            int offset = sy * SCREEN_WIDTH + sx;
            uint32_t after = rgb_mask[offset] ? rgb_framebuffer[offset] : palette[framebuffer[offset]];
            if (before[offset] == after) continue;
            ++changed;
            assert(sx >= x && sx < x + 32 && sy >= y && sy < y + 32);
        }
    }
    assert(changed > 0);
}

static void click_desktop(int id, unsigned int modifiers, int context, int *page)
{
    int x, y;
    desktop_slot_position(desktop_item_slot(id), &x, &y);
    keys[224] = (modifiers & KEYMOD_CTRL) != 0;
    keys[225] = (modifiers & KEYMOD_SHIFT) != 0;
    if (!context) desktop_last_click = -1;
    assert(desktop_selection_pointer(x + 4, y + 4, context ? 2 : 1, page));
    keys[224] = keys[225] = 0;
}

static void assert_matches_explorer(const int *order, int count)
{
    for (int i = 0; i < count; ++i)
        assert(desktop_select.selected[order[i]] == explorer_selection[i]);
    assert(budo_selection_position(order, count, desktop_select.focus) == explorer_selected_item);
    assert(budo_selection_position(order, count, desktop_select.anchor) == explorer_anchor);
}

int main(void)
{
    set_classic_gui_palette();
    assert(bwa_host_api.abi_minor == 12);
    keys[224] = keys[225] = 1;
    assert(budo_selection_modifiers(&bwa_host_api) == (KEYMOD_CTRL | KEYMOD_SHIFT));
    BwaHostApi old_host = bwa_host_api;
    old_host.abi_minor = 10;
    old_host.get_keyboard_modifiers = NULL;
    assert(budo_selection_modifiers(&old_host) == 0);
    keys[224] = keys[225] = 0;
    unsigned char custom_marks[300], custom_snapshot[300];
    BudoSelection custom;
    int custom_order[] = {4, 280, 12};
    budo_selection_init(&custom, custom_marks, custom_snapshot, 300);
    budo_selection_click(&custom, custom_order, 3, 280, 0, 0);
    budo_selection_click(&custom, custom_order, 3, 12, KEYMOD_SHIFT, 0);
    assert(custom_marks[280] && custom_marks[12] && !custom_marks[4]);
    assert(custom.anchor == 280 && custom.focus == 12);
    assert(desktop_select.focus == -1 && explorer_selected_item == -1);

    /* Both fallback artwork and PCX use an exact 32x32 highlight. */
    for (int pcx = 0; pcx < 2; ++pcx) {
        folder_pcx_icon_loaded = pcx;
        if (pcx) {
            memset(folder_pcx_icon, PCX_TRANSPARENT, sizeof(folder_pcx_icon));
            folder_pcx_icon[10 * 32 + 10] = FILE_COLOR;
        }
        memset(framebuffer, WINDOW_FACE_COLOR, sizeof(framebuffer));
        memset(rgb_mask, 0, sizeof(rgb_mask));
        draw_labeled_icon(100, 100, "long filename", TYPE_FOLDER, 0, "/");
        capture();
        draw_labeled_icon(100, 100, "long filename", TYPE_FOLDER, 1, "/");
        assert_icon_changes(100, 100);
    }
    folder_pcx_icon_loaded = 0;
    active_window = APP_NONE;
    explorer_window.open = editor_window.open = terminal_window.open = 0;
    explorer_desktop_slot = 0;
    editor_desktop_slot = 1;
    bwa_external_app_count = 1;
    BwaLoadedApp *app = &bwa_external_apps[0];
    app->definition.app_id = "test";
    app->definition.name = "Test App";
    app->definition.runtime_id = APP_BWA_BASE;
    app->definition.callbacks.open = open_test_app;
    app->desktop_slot = 2;
    shortcut_count = 2;
    for (int i = 0; i < 2; ++i) {
        snprintf(desktop_shortcuts[i].name, MAX_NAME, "document-%d.txt", i);
        snprintf(desktop_shortcuts[i].path, MAX_PATH, "/document-%d.txt", i);
        desktop_shortcuts[i].type = TYPE_FILE;
        shortcut_slots[i] = 3 + i;
    }
    int order[DESKTOP_SELECTION_MAX];
    int count = desktop_order(order);
    assert(count == 5);
    for (int i = 0; i < count; ++i) {
        int x, y;
        budo_selection_clear(&desktop_select);
        draw_desktop(0);
        capture();
        desktop_select.selected[order[i]] = 1;
        desktop_select.focus = order[i];
        draw_desktop(0);
        desktop_slot_position(desktop_item_slot(order[i]), &x, &y);
        assert_icon_changes(x, y);
    }
    item_count = 5;
    only_executables = 0;
    for (int i = 0; i < item_count; ++i) items[i].type = TYPE_FOLDER;
    explorer_clear_selection();
    budo_selection_clear(&desktop_select);
    int page = 0;
    int targets[] = {0, 3, 1, 4, 2, 3, 4};
    unsigned int modifiers[] = {0, KEYMOD_CTRL, KEYMOD_SHIFT, KEYMOD_CTRL | KEYMOD_SHIFT,
                                0, KEYMOD_CTRL, KEYMOD_CTRL};
    for (int i = 0; i < 7; ++i) {
        click_desktop(order[targets[i]], modifiers[i], 0, &page);
        explorer_select_item(targets[i], modifiers[i]);
        assert_matches_explorer(order, count);
    }
    /* Context clicks preserve a selected group and replace it on an unselected item. */
    click_desktop(order[2], 0, 1, &page);
    int visible[MAX_ITEMS];
    int visible_count = explorer_order(visible);
    budo_selection_click(&explorer_select, visible, visible_count, 2, 0, 1);
    assert_matches_explorer(order, count);
    context_menu = 0;
    click_desktop(order[0], 0, 1, &page);
    budo_selection_click(&explorer_select, visible, visible_count, 0, 0, 1);
    assert_matches_explorer(order, count);
    context_menu = 0;
    assert(desktop_selection_key(1, &page));
    explorer_select_all_visible();
    assert_matches_explorer(order, count);
    int scans[] = {71, 77, 77, 75, 79, 71};
    for (int i = 0; i < 6; ++i) {
        keys[225] = i > 0 && i < 4;
        keys[224] = i == 2;
        enqueue(scans[i]);
        assert(desktop_selection_key(0, &page));
        explorer_navigate(scans[i], &page);
        assert_matches_explorer(order, count);
    }
    keys[224] = keys[225] = 0;
    keys[224] = 1;
    assert(desktop_selection_key(' ', &page));
    budo_selection_space(&explorer_select, visible, visible_count, KEYMOD_CTRL);
    assert_matches_explorer(order, count);
    keys[224] = 0;
    assert(desktop_selection_key(27, &page));
    explorer_clear_selection();
    assert_matches_explorer(order, count);
    assert(!desktop_selection_key(27, &page));

    /* Desktop marquee includes launchers and shortcuts and restores its snapshot
     * when shrinking; Ctrl toggles and Shift adds using the same engine. */
    int end_x, end_y;
    desktop_slot_position(3, &end_x, &end_y);
    assert(desktop_selection_pointer(5, 5, 1, &page));
    assert(desktop_select.dragging);
    desktop_selection_drag_update(end_x + 30, end_y + 31);
    for (int i = 0; i < count; ++i) assert(desktop_select.selected[order[i]] == (i < 4));
    desktop_selection_drag_update(60, 80);
    for (int i = 0; i < count; ++i) assert(desktop_select.selected[order[i]] == (i == 0));
    desktop_select.dragging = 0;
    budo_selection_drag_begin(&desktop_select, 5, 5, KEYMOD_SHIFT);
    desktop_selection_drag_update(end_x + 30, end_y + 31);
    assert(desktop_select.selected[order[3]] && desktop_select.selected[order[0]]);
    desktop_select.dragging = 0;
    budo_selection_drag_begin(&desktop_select, 5, 5, KEYMOD_CTRL);
    desktop_selection_drag_update(60, 80);
    assert(!desktop_select.selected[order[0]] && desktop_select.selected[order[3]]);
    desktop_select.dragging = 0;
    /* A covered desktop icon cannot be selected through another window. */
    editor_window.open = 1;
    editor_window.x = 20;
    editor_window.y = 30;
    active_window = APP_EDITOR;
    int focus = desktop_select.focus;
    assert(!desktop_selection_pointer(38, 52, 1, &page));
    assert(desktop_select.focus == focus);
    editor_window.open = 0;
    active_window = APP_NONE;
    click_desktop(2, 0, 0, &page);
    assert(opened == 0 && !app->open);
    int x, y;
    desktop_slot_position(2, &x, &y);
    assert(desktop_selection_pointer(x + 4, y + 4, 1, &page));
    assert(opened == 1 && app->open && active_window == APP_BWA_BASE);
    puts("PASS: shared selection, desktop/explorer parity, marquee and exact 32x32 highlights");
    return 0;
}
