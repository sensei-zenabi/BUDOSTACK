#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>

static void snapshot(const char *directory, const char *name)
{
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, name));
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        uint32_t rgb = rgb_mask[i] ? rgb_framebuffer[i] : palette[framebuffer[i]];
        fputc((rgb >> 16) & 255, file);
        fputc((rgb >> 8) & 255, file);
        fputc(rgb & 255, file);
    }
    assert(fclose(file) == 0);
}

static void test_editor(const char *directory)
{
    editor_window.open = 1;
    editor_reset_document();
    active_window = ui_draw_owner = APP_EDITOR;
    editor_word_wrap = editor_show_row_numbers = 1;
    for (int width = 478; width <= 638; width += 80) {
        editor_window.x = 0;
        editor_window.w = width;
        for (int writer = 0; writer <= 1; ++writer) {
            editor_writer_mode = writer;
            for (int ruler = 0; ruler <= 1; ++ruler) {
                editor_writer_ruler = ruler;
                draw_editor_window();
                int status_y = editor_window.y + editor_window.h - EDITOR_STATUS_H;
                int body_y = status_y - 20;
                assert(editor_text_x() >= editor_window.x + 8);
                if (writer) {
                    int cols = editor_visible_cols();
                    if (cols > EDITOR_WRITER_PAGE_COLS) cols = EDITOR_WRITER_PAGE_COLS;
                    assert(editor_text_x() + cols * 6 <= editor_vscroll.x - 4);
                }
                assert(editor_vscroll.y + editor_vscroll.length <= status_y - 2);
                for (int x = 2; x < width - 2; ++x) {
                    /* No paper edge crosses the bottom of the writing area. */
                    assert(framebuffer[(status_y - 1) * SCREEN_WIDTH + x] == FILE_COLOR);
                    assert(framebuffer[status_y * SCREEN_WIDTH + x] == FILE_DARK);
                    assert(framebuffer[(status_y + EDITOR_STATUS_H - 3) * SCREEN_WIDTH + x] == FILE_DARK);
                    if (x < editor_vscroll.x || x >= editor_vscroll.x + BUDO_SCROLL_WIDTH)
                        assert(framebuffer[body_y * SCREEN_WIDTH + x] == FILE_COLOR);
                }
            }
        }
    }
    editor_window.x = 60;
    editor_window.w = 520;
    strcpy(editor_lines[0], "Writer layout: continuous page and uniform status bar.");
    draw_desktop(0);
    snapshot(directory, "writer-layout.ppm");
    editor_writer_mode = 0;
    draw_desktop(0);
    snapshot(directory, "editor-layout.ppm");
    editor_window.open = 0;
}

static void test_desktop(const char *directory)
{
    explorer_window.open = editor_window.open = terminal_window.open = 1;
    explorer_window.minimized = editor_window.minimized = terminal_window.minimized = 1;
    ui_draw_owner = UI_OVERLAY_OWNER;
    memset(framebuffer, DESKTOP_COLOR, sizeof(framebuffer));
    desktop_running_draw();
    for (int x = 4; x < 126; ++x)
        assert(framebuffer[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH + x] != DESKTOP_COLOR);
    assert(!desktop_minimized_click(8, SCREEN_HEIGHT - 21));
    assert(desktop_minimized_click(8, SCREEN_HEIGHT - 1));
    assert(!explorer_window.minimized && active_window == APP_EXPLORER);
    assert(desktop_minimized_click(8, SCREEN_HEIGHT - 1));
    assert(!editor_window.minimized && active_window == APP_EDITOR);
    explorer_window.open = editor_window.open = terminal_window.open = 0;
    shortcut_count = 1;
    strcpy(desktop_shortcuts[0].name, "Folder");
    desktop_shortcuts[0].type = TYPE_FOLDER;
    shortcut_slots[0] = 3;
    for (int artwork = 0; artwork < 3; ++artwork) {
        folder_pcx_icon_loaded = artwork == 1;
        memset(folder_pcx_icon, PCX_TRANSPARENT, sizeof(folder_pcx_icon));
        shortcut_details[0].icon_loaded = artwork == 2;
        memset(shortcut_details[0].pixels, PCX_TRANSPARENT, sizeof(shortcut_details[0].pixels));
        draw_desktop(0);
        int x, y, white = 0;
        desktop_slot_position(shortcut_slots[0], &x, &y);
        for (int row = y + DESKTOP_ICON_H + 2; row < y + DESKTOP_ICON_H + 9; ++row) {
            for (int col = x - 20; col < x + 52; ++col) {
                unsigned char c = framebuffer[row * SCREEN_WIDTH + col];
                assert(c != TEXT_COLOR);
                white += c == DESKTOP_TEXT_COLOR;
            }
        }
        assert(white > 0);
    }
    explorer_window.open = 1;
    explorer_window.minimized = 1;
    draw_desktop(0);
    snapshot(directory, "desktop-layout.ppm");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    set_classic_gui_palette();
    test_editor(argv[1]);
    test_desktop(argv[1]);
    puts("PASS: Writer/editor surface and status geometry, bottom-edge restore tabs, white shortcut labels for all artwork paths");
    return 0;
}
