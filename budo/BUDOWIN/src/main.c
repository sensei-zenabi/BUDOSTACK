#define _POSIX_C_SOURCE 200809L
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "platform.h"
#include "text_encoding.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include "bwa.h"
#include "ui_style.h"
#include "../sdk/ui.h"
#include "../sdk/palette.h"
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include "../../../lib/budo_gfx.h"

#define VGA_MEMORY 0xA0000UL
#define VESA_MODE 0x0101
#define VESA_BANK_SIZE 65536UL
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define SCREEN_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)

/*
 * Original early-1990s GUI palette inspired by the era's common
 * 16-colour desktop conventions.  No Microsoft artwork or resources
 * are used; all chrome and icons are drawn by BUDOWIN.
 */
static const unsigned char ui_color_defaults[BUDO_SYS_COLOR_COUNT] = {0, 1, 2, 3, 4, 5, 6, 4, 7, 5, 4, 2, 7, 6, 4, 1, 5, 4, 5, 4, 1, 2, 1, 6, 4};
static unsigned char ui_color_indices[BUDO_SYS_COLOR_COUNT] = {0, 1, 2, 3, 4, 5, 6, 4, 7, 5, 4, 2, 7, 6, 4, 1, 5, 4, 5, 4, 1, 2, 1, 6, 4};

#define DESKTOP_COLOR (ui_color_indices[BUDO_SYS_COLOR_DESKTOP])
#define TEXT_COLOR (ui_color_indices[BUDO_SYS_COLOR_TEXT])
#define FILE_DARK (ui_color_indices[BUDO_SYS_COLOR_SHADOW])
#define FOLDER_DARK (ui_color_indices[BUDO_SYS_COLOR_SHADOW])
#define FILE_COLOR (ui_color_indices[BUDO_SYS_COLOR_SURFACE])
#define FOLDER_COLOR (ui_color_indices[BUDO_SYS_COLOR_FOLDER_ICON])
#define BACK_COLOR (ui_color_indices[BUDO_SYS_COLOR_TITLE_ACTIVE])
#define CURSOR_COLOR (ui_color_indices[BUDO_SYS_COLOR_TEXT])
#define MOUSE_CURSOR_OUTLINE_COLOR (ui_color_indices[BUDO_SYS_COLOR_CURSOR_OUTLINE])
#define MOUSE_CURSOR_FILL_COLOR (ui_color_indices[BUDO_SYS_COLOR_CURSOR_FILL])

#define WINDOW_FACE_COLOR (ui_color_indices[BUDO_SYS_COLOR_SURFACE])
#define WINDOW_CHROME_COLOR (ui_color_indices[BUDO_SYS_COLOR_FACE])
#define WINDOW_HIGHLIGHT_COLOR (ui_color_indices[BUDO_SYS_COLOR_HIGHLIGHT])
#define WINDOW_SHADOW_COLOR (ui_color_indices[BUDO_SYS_COLOR_SHADOW])
#define WINDOW_FRAME_COLOR (ui_color_indices[BUDO_SYS_COLOR_FRAME])
#define TITLE_COLOR (ui_color_indices[BUDO_SYS_COLOR_TITLE_ACTIVE])
#define TITLE_TEXT_COLOR (ui_color_indices[BUDO_SYS_COLOR_TITLE_TEXT])
#define TITLE_INACTIVE_COLOR (ui_color_indices[BUDO_SYS_COLOR_TITLE_INACTIVE])
#define CONTROL_HOVER_COLOR (ui_color_indices[BUDO_SYS_COLOR_HOVER])
#define CONTROL_PRESSED_COLOR (ui_color_indices[BUDO_SYS_COLOR_PRESSED])
#define TEXT_CELL_WIDTH 6
#define TEXT_CELL_HEIGHT 9
#define CONTEXT_CURSOR_HOTSPOT 6
#define EXPLORER_DIVIDER_CURSOR 5
#define DESKTOP_TEXT_COLOR (ui_color_indices[BUDO_SYS_COLOR_DESKTOP_TEXT])
#define TERMINAL_BG_COLOR (ui_color_indices[BUDO_SYS_COLOR_TERMINAL_BG])
#define TERMINAL_TEXT_COLOR (ui_color_indices[BUDO_SYS_COLOR_TERMINAL_TEXT])

#define MAX_ITEMS 256
#define MAX_NAME 256
#define MAX_PATH 4096

#define GRID_X_STEP 76
#define GRID_Y_STEP 42
#define ICON_W 24
#define ICON_H 16
#define DESKTOP_ICON_W 32
#define DESKTOP_ICON_H 32
#define MOUSE_CURSOR_W 32
#define MOUSE_CURSOR_H 32
#define PCX_TRANSPARENT 255U

#define WINDOW_TITLE_H 18
#define WINDOW_BORDER 4
#define WINDOW_MIN_W 360
#define WINDOW_MIN_H 150
#define WINDOW_DEFAULT_X 60
#define WINDOW_DEFAULT_Y 60
#define WINDOW_DEFAULT_W 520
#define WINDOW_DEFAULT_H 340

#define DESKTOP_GRID_COLS 8
#define DESKTOP_GRID_ROWS 8
#define DESKTOP_GRID_X_STEP 76
#define DESKTOP_GRID_Y_STEP 52
#define DESKTOP_GRID_X 34
#define DESKTOP_GRID_Y 48
#define DESKTOP_SHORTCUT_MAX (DESKTOP_GRID_COLS * DESKTOP_GRID_ROWS)
#define DESKTOP_SHORTCUT_BASE (2 + BWA_MAX_EXTERNAL)
#define DESKTOP_SELECTION_MAX (DESKTOP_SHORTCUT_BASE + DESKTOP_SHORTCUT_MAX)
#define APP_SHORTCUT_BASE 1000

#define TYPE_FOLDER 1
#define TYPE_FILE 2

#define APP_NONE 0
#define APP_EXPLORER (explorer_state->runtime_id)
#define APP_EDITOR (editor_state->runtime_id)
#define APP_TERMINAL (terminal_state->runtime_id)
#define APP_BWA_BASE 100
#define BWA_MAX_EXTERNAL 16
#define BWA_MAX_INSTANCES 64

#define EDITOR_MAX_LINES 512
#define EDITOR_MAX_COLS 2048
#define EDITOR_MENU_H 14
#define EDITOR_STATUS_H 14
#define EDITOR_RULER_H 20
#define EDITOR_WRITER_TABS 8
#define EDITOR_WRITER_PAGE_COLS 77
#define EDITOR_WRITER_PAGE_W (EDITOR_WRITER_PAGE_COLS * 6)
#define EDITOR_WRITER_MIN_W (EDITOR_WRITER_PAGE_W + 32)
#define EDITOR_MENU_NONE 0
#define EDITOR_MENU_FILE 1
#define EDITOR_MENU_EDIT 2
#define EDITOR_MENU_SEARCH 3
#define EDITOR_MENU_DOCUMENT 4
#define EDITOR_DIALOG_NONE 0
#define EDITOR_DIALOG_OPEN 1
#define EDITOR_DIALOG_SAVE_AS 2
#define EDITOR_DIALOG_FIND 3
#define EDITOR_DIALOG_REPLACE_FIND 4
#define EDITOR_DIALOG_REPLACE_WITH 5
#define EDITOR_DIALOG_REPLACE_ALL_FIND 6
#define EDITOR_DIALOG_REPLACE_ALL_WITH 7

#define EDITOR_FILE_DIALOG_NONE 0
#define EDITOR_FILE_DIALOG_OPEN 1
#define EDITOR_FILE_DIALOG_SAVE_AS 2
#define EDITOR_FILE_DIALOG_EXPORT 3

#define TERMINAL_MAX_LINES 256
#define TERMINAL_LINE_LEN 128
#define TERMINAL_HISTORY_MAX 32
#define TERMINAL_INPUT_LEN 120
#define TERMINAL_COMPLETION_MAX 64
#define TERMINAL_CAPTURE_FILE bw_state_file("terminal.tmp")
#define TERMINAL_STATE_FILE bw_state_file("terminal.state")

#define LAUNCH_FILE bw_state_file("launch.sh")
#define STATE_FILE bw_state_file("session.state")
#define DESKTOP_STATE_FILE bw_state_file("desktop.state")
#define EDITOR_STATE_FILE bw_state_file("editor.state")
#define ASSOCIATION_FILE bw_state_file("associations.txt")
#define FILE_ASSOC_MAX 48
#define FILE_EXT_LEN 8

#define EXPLORER_CLIP_NONE 0
#define EXPLORER_CLIP_COPY 1
#define EXPLORER_CLIP_CUT  2

#define KEYMOD_SHIFT 0x03
#define KEYMOD_CTRL  0x04
#define KEYMOD_ALT   0x08

typedef struct DesktopItem {
    char name[MAX_NAME];
    char path[MAX_PATH];
    int type;
} DesktopItem;

typedef struct FileAssociation {
    char extension[FILE_EXT_LEN + 1];
    char app_id[BWA_ID_LEN];
} FileAssociation;

typedef struct ExplorerClipboardItem {
    char name[MAX_NAME];
    char path[MAX_PATH];
    int type;
} ExplorerClipboardItem;

typedef struct AppWindow {
    int x;
    int y;
    int w;
    int h;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
    int open;
    int minimized;
    int maximized;
    int dragging;
    int resizing;
    int drag_dx;
    int drag_dy;
    int resize_mouse_x;
    int resize_mouse_y;
    int resize_w;
    int resize_h;
    int resize_x, resize_y;
} AppWindow;

#define WINDOW_FRAME_NONE      0
#define WINDOW_FRAME_CLIENT    1
#define WINDOW_FRAME_MINIMIZE  2
#define WINDOW_FRAME_MAXIMIZE  3
#define WINDOW_FRAME_CLOSE     4
#define WINDOW_FRAME_RESIZE    5
#define WINDOW_FRAME_DRAG      6

typedef struct BwaLoadedApp {
    void *handle;
    BwaAppDefinition definition;
    char path[MAX_PATH];
    int desktop_slot;
    int open;
    int pcx_icon_loaded;
    unsigned char pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
    int managed_window;
    AppWindow window;
    char window_title[BWA_NAME_LEN + 32];
    unsigned int window_buttons;
    int window_min_w;
    int window_min_h;
} BwaLoadedApp;

#include "builtin_state.h"
static int terminal_instance_slot;
static int builtin_new_instance(int kind);
static int builtin_create_instance(int kind, int start_terminal);
static void builtin_select(int runtime_id);
static AppWindow *builtin_window(int runtime_id);

static AppWindow *builtin_window(int runtime_id)
{
    if (runtime_id <= 0 || runtime_id >= APP_BWA_BASE) return NULL;
    int slot = (runtime_id - 1) / 3;
    if (slot >= BUILTIN_INSTANCE_MAX) return NULL;
    switch ((runtime_id - 1) % 3) {
        case 0: return explorer_instances[slot] ? &explorer_instances[slot]->v_explorer_window : NULL;
        case 1: return editor_instances[slot] ? &editor_instances[slot]->v_editor_window : NULL;
        default: return terminal_instances[slot] ? &terminal_instances[slot]->v_terminal_window : NULL;
    }
}

static void builtin_select(int runtime_id)
{
    if (!builtin_window(runtime_id)) return;
    int slot = (runtime_id - 1) / 3;
    switch ((runtime_id - 1) % 3) {
        case 0: explorer_state = explorer_instances[slot]; break;
        case 1: editor_state = editor_instances[slot]; break;
        default:
            terminal_state = terminal_instances[slot];
            terminal_instance_slot = slot;
            break;
    }
}

static unsigned char framebuffer[SCREEN_SIZE];
static unsigned char desktop_background[SCREEN_SIZE];
/* Keep PCX RGB separate from the GUI palette. */
static uint32_t desktop_background_rgb[SCREEN_SIZE];
/* A set mask selects exact RGB; otherwise the pixel uses the GUI palette. */
static unsigned char rgb_mask[SCREEN_SIZE];
static uint32_t rgb_framebuffer[SCREEN_SIZE];

/* Each icon keeps its own exact PCX colors, independent of GUI indices. */
typedef struct PcxIconColors {
    const unsigned char *indices;
    uint32_t rgb[DESKTOP_ICON_W * DESKTOP_ICON_H];
} PcxIconColors;
static PcxIconColors pcx_icon_colors[BWA_MAX_EXTERNAL + 9];
static int pcx_icon_color_count;
static int desktop_background_loaded = 0;
static unsigned char explorer_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char editor_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char folder_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char file_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char exec_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char text_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char code_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char back_pcx_icon[DESKTOP_ICON_W * DESKTOP_ICON_H];
static unsigned char cursor_pcx_icon[MOUSE_CURSOR_W * MOUSE_CURSOR_H];
static int explorer_pcx_icon_loaded = 0;
static int editor_pcx_icon_loaded = 0;
static int folder_pcx_icon_loaded = 0;
static int file_pcx_icon_loaded = 0;
static int exec_pcx_icon_loaded = 0;
static int text_pcx_icon_loaded = 0;
static int code_pcx_icon_loaded = 0;
static int back_pcx_icon_loaded = 0;
static int cursor_pcx_icon_loaded = 0;
static FileAssociation file_associations[FILE_ASSOC_MAX];
static int file_association_count = 0;

static char home_path[MAX_PATH] = "/";
static int resume_explorer = 0;

#define explorer_selected_item explorer_select.focus
#define explorer_anchor explorer_select.anchor
static DesktopItem desktop_shortcuts[DESKTOP_SHORTCUT_MAX];
static int shortcut_slots[DESKTOP_SHORTCUT_MAX];
typedef struct ShortcutDetails {
    char parent[MAX_PATH];
    char icon[MAX_PATH];
    unsigned char pixels[DESKTOP_ICON_W * DESKTOP_ICON_H];
    uint32_t rgb[DESKTOP_ICON_W * DESKTOP_ICON_H];
    int icon_loaded;
} ShortcutDetails;
static ShortcutDetails shortcut_details[DESKTOP_SHORTCUT_MAX];
static char desktop_folder[MAX_PATH];
static char explorer_shortcut_folder[MAX_PATH];
static int shortcuts_delete_selected(void);
static int desktop_picker_action;
static int desktop_picker_shortcut = -1;
static unsigned char desktop_selection_marks[DESKTOP_SELECTION_MAX];
static unsigned char desktop_drag_snapshot[DESKTOP_SELECTION_MAX];
static BudoSelection desktop_select = {
    .selected = desktop_selection_marks, .snapshot = desktop_drag_snapshot,
    .capacity = DESKTOP_SELECTION_MAX, .focus = -1, .anchor = -1
};
#define shortcut_selection (desktop_selection_marks + DESKTOP_SHORTCUT_BASE)
static int shortcut_count;
static int context_menu;
static int shortcut_launch_requested;
static int desktop_last_click = -1;
static clock_t desktop_click_time;
static int context_x, context_y;
static ExplorerClipboardItem explorer_clipboard[MAX_ITEMS];
static int explorer_clipboard_count = 0;
static int explorer_clipboard_mode = EXPLORER_CLIP_NONE;

static int explorer_desktop_slot = 0;
static int editor_desktop_slot = 1;
static int desktop_drag_app = APP_NONE;
static int desktop_drag_slots[DESKTOP_SELECTION_MAX];
static int desktop_drag_origin_x, desktop_drag_origin_y;
static int desktop_drag_offset_x, desktop_drag_offset_y;
static void desktop_group_drop(void);
static int desktop_icon_pressed = 0;
static int desktop_icon_dragging = 0;

static int desktop_press_x = 0;
static int desktop_press_y = 0;
static int desktop_drag_x = 0;
static int desktop_drag_y = 0;
static int desktop_drag_dx = 0;
static int desktop_drag_dy = 0;
static int active_window = APP_NONE;
static BwaLoadedApp bwa_external_apps[BWA_MAX_EXTERNAL];
static int bwa_external_app_count = 0;
static BwaLoadedApp bwa_instances[BWA_MAX_INSTANCES];
static int bwa_instance_count;
static BwaLoadedApp *bwa_new_instance(BwaLoadedApp *program);
static int builtin_poll_terminals(void);
static void desktop_focus_visible(void);
static BwaLoadedApp *bwa_callback_app = NULL;

static char *editor_clipboard;

static clock_t app_switch_until;
static void editor_selection_clear(void);
static int editor_selection_delete(void);

static void editor_history_reset(void);
static int editor_history_before_edit(void);

static int editor_search_dialog_active(void)
{
    return editor_dialog == EDITOR_DIALOG_FIND ||
           editor_dialog == EDITOR_DIALOG_REPLACE_FIND ||
           editor_dialog == EDITOR_DIALOG_REPLACE_ALL_FIND;
}

static int text_caret_visible = 1;
static clock_t text_caret_last_toggle = 0;

static const BwaHostApi bwa_host_api;
static int picker_owner = BWA_HOST_APP_EDITOR;
static int picker_overwrite = 0;
static int picker_name_cursor = 0;
static int picker_name_selected = 0;
static int picker_focus = 0;
static int picker_filter = 0;
static int picker_filter_menu = 0;
static int picker_mkdir = 0;
static char picker_previous_name[MAX_PATH];
static int confirm_owner = APP_NONE;
static int confirm_kind = 0;
static int confirm_is_picker = 0;
static char confirm_title[80];
static char confirm_message[256];

static int desktop_exit_requested = 0;

static BudoScrollbar picker_scroll;

static void explorer_folder_sync(void);
static int explorer_folder_pointer(int x, int y, int *page);
static int explorer_folder_key(int key, int scan, int *page);
static void explorer_folder_draw(void);
static void editor_mark_saved(void);
static void editor_close_now(void);
static void close_terminal(void);
static void close_explorer(void);
static int editor_modified(void);
static void editor_request_action(int action, const char *path);
static int desktop_confirm(int owner, int kind, const char *title, const char *message);
static void desktop_confirm_result(int response);
static void desktop_confirm_draw(void);
static void desktop_confirm_click(int x, int y);
static void desktop_file_menu_draw(int app);
static int desktop_file_menu_click(int x, int y, int *page);
static AppWindow *picker_window(void);
static void picker_cancel(void);
static void picker_geometry(int *x, int *y, int *w, int *h);
static int desktop_request_exit(void);
static void editor_set_top_visual_row(int row);
static int desktop_scroll_pointer(int x, int y, int event, int *page);
static void desktop_scroll_draw(int app, int page);
static void desktop_scroll_configure(int app, int page);
static int bwa_file_dialog(int mode, const char *initial);
static int bwa_confirm_dialog(int kind, const char *title, const char *message);
static int editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
static DesktopItem *editor_file_items = NULL;
static int editor_file_count = 0;
static int editor_file_top = 0;
static BudoSelection picker_selection = {.focus = -1, .anchor = -1};
#define editor_file_selected picker_selection.focus
static char editor_file_path[MAX_PATH] = "/";
static char editor_file_name[MAX_PATH] = "";
static int editor_file_name_len = 0;
static clock_t editor_file_last_click_time = 0;

static int window_contains(const AppWindow *window, int x, int y);
static int point_in_rect(int px, int py, int x, int y, int w, int h);
static void draw_text(int x, int y, const char *text,
                      unsigned char color, int max_chars);
static int page_count(void);
static void clamp_page(int *page);
static int item_for_slot(int page, int slot);
static void explorer_clear_selection(void);
static int shortcuts_save(void);
static int shortcut_icon_load(int index);
static int desktop_picker_accept(const char *path);
static void desktop_begin_picker(int action);
static int shortcut_move(int index, const char *parent);
static const char *shortcut_drop_parent(int x, int y);
static void explorer_shortcut_drag_begin(int item, int x, int y);
static int explorer_shortcut_drag_update(int x, int y, int down);
static int explorer_shortcut_drag_drop(int x, int y);
static void desktop_folder_up(void);
static int desktop_free_slot(const char *parent, int ignore);
static void shortcuts_load(void);
static int shortcuts_create(void);
static int shortcut_hit(int x, int y);
static int desktop_selection_pointer(int x, int y, int buttons, int *page);
static int desktop_selection_key(int key, int *page);
static void desktop_selection_drag_update(int x, int y);
static int desktop_order(int *order);
static int desktop_item_slot(int id);
static int desktop_has_selected_shortcuts(void);
static void explorer_navigate(int scan, int *page);
static void explorer_select_item(int target, unsigned int modifiers);
static void explorer_select_all_visible(void);
static void explorer_stage_clipboard(int mode);
static int explorer_paste_clipboard(void);
static int explorer_make_copy_name(const char *name,
                                   char *dest,
                                   size_t dest_size);
static int explorer_delete_selection(void);
static int recycle_load(void);
static int recycle_put(const char *path);
static int recycle_restore(void);
static int recycle_empty(void);
static int bwa_open_reader_file(const char *path);
static void editor_search_click(int mx, int my);
static void editor_selection_begin(void);
static int explorer_begin_rename(void);
static int explorer_commit_rename(void);
static void explorer_cancel_rename(void);
static int explorer_edit_selection(void);
static unsigned int keyboard_modifiers(void);
static int editor_visible_rows(void);
static int editor_text_x(void);
static int editor_visible_cols(void);
static int editor_cursor_screen_row(void);
static int editor_cursor_screen_col(void);
static void editor_ensure_cursor_visible(void);
static void editor_draw_file_dialog(void);
static void editor_begin_file_dialog(int mode);
static void editor_file_dialog_click(int x, int y, clock_t now);
static void editor_file_dialog_key(int key);
static int save_editor_settings(void);
static void load_editor_settings(void);
static void editor_update_last_save(const char *path);
static int editor_word_count(void);
static int text_input_active(void);
static int compare_names_ci(const char *a, const char *b);
static int executable_file(const char *name);
static int executable_path(const char *path);
static int text_file(const char *name);
static int code_file(const char *name);
static int find_file_association(const char *extension);
static int bwa_delete_file_association(const char *extension);
static void load_file_associations(void);
static int bwa_get_file_association_count(void);
static int bwa_get_file_association(int index,
                                    char *extension,
                                    unsigned int extension_size,
                                    char *app_id,
                                    unsigned int app_id_size);
static int bwa_set_file_association(const char *extension,
                                    const char *app_id);
static int open_associated_file(const char *path);
static void editor_wrap_segment(const char *text,
                                int start,
                                int cols,
                                int *draw_start,
                                int *draw_len,
                                int *next_start);
static int editor_path_is_rtf(const char *path);
static FILE *desktop_file_open(const char *path, char *temporary)
{
    struct stat info;
    int fd;
    FILE *file;
    int n = snprintf(temporary, MAX_PATH, "%s.tmp.XXXXXX", path);
    if (n < 0 || n >= MAX_PATH) return NULL;
    if (stat(path, &info) == 0 && !S_ISREG(info.st_mode)) return NULL;
    fd = mkstemp(temporary);
    if (fd < 0) return NULL;
    if (stat(path, &info) == 0 && fchmod(fd, info.st_mode & 0777) != 0) {
        close(fd);
        unlink(temporary);
        return NULL;
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        unlink(temporary);
    }
    return file;
}

static int desktop_file_finish(FILE *file, const char *path, const char *temporary)
{
    int ok = !ferror(file);
    if (fflush(file) != 0) ok = 0;
    if (fsync(fileno(file)) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (ok && rename(temporary, path) == 0) return 1;
    unlink(temporary);
    return 0;
}

static int editor_save_rtf(const char *path);
static int editor_load_rtf(const char *path);
static int editor_export_postscript(const char *path);
static int editor_writer_segment_indent(int segment);
static void open_explorer(void);
static void open_editor(void);
static void open_terminal(void);
static int terminal_visible_rows(void);
static int session_caret(int *x, int *y);
static void terminal_draw_caret_row(unsigned long offset, size_t length);
static void terminal_clear_selection(void);
static void terminal_history_trimmed(void);
static void terminal_load_resume_state(void);
static int copy_text(char *dest, size_t dest_size,
                     const char *src);
static int join_path(char *dest, size_t dest_size,
                     const char *dir, const char *name);
static void normalize_slashes(char *path);
static void bwa_load_external_apps(void);
static BwaLoadedApp *bwa_find_external_app(int runtime_id);
static BwaLoadedApp *bwa_find_external_app_id(const char *app_id);
static int bwa_get_file_app_count(void);
static int bwa_get_file_app(int index,
                            char *app_id,
                            unsigned int app_id_size,
                            char *name,
                            unsigned int name_size);
static int bwa_close_active_app(void);
static void load_pcx_assets(void);

static const unsigned char font5x7[36][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}
};

static const unsigned char font5x7_lower[26][7] = {
    {0,0,0x0E,0x01,0x0F,0x11,0x0F},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
    {0,0,0x0E,0x10,0x10,0x11,0x0E},
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
    {0,0,0x0E,0x11,0x1F,0x10,0x0E},
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
    {0,0,0x0F,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
    {0x04,0,0x0C,0x04,0x04,0x04,0x0E},
    {0x02,0,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0,0,0x1A,0x15,0x15,0x15,0x15},
    {0,0,0x16,0x19,0x11,0x11,0x11},
    {0,0,0x0E,0x11,0x11,0x11,0x0E},
    {0,0,0x1E,0x11,0x1E,0x10,0x10},
    {0,0,0x0F,0x11,0x0F,0x01,0x01},
    {0,0,0x16,0x19,0x10,0x10,0x10},
    {0,0,0x0F,0x10,0x0E,0x01,0x1E},
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    {0,0,0x11,0x11,0x11,0x13,0x0D},
    {0,0,0x11,0x11,0x11,0x0A,0x04},
    {0,0,0x11,0x11,0x15,0x15,0x0A},
    {0,0,0x11,0x0A,0x04,0x0A,0x11},
    {0,0,0x11,0x11,0x0F,0x01,0x0E},
    {0,0,0x1F,0x02,0x04,0x08,0x1F}
};

static void set_text_mode(void) { bw_screen_close(); }
static int set_graphics_mode(void) { return bw_screen_open(); }
static void set_palette_entry(unsigned char index, unsigned char r,
                              unsigned char g, unsigned char b)
{
    bw_palette_entry(index, r, g, b);
}

static BudoUiStyle desktop_ui_style = BUDO_UI_STYLE_CLASSIC;

/* Compatibility RGB style hook. Settings maps roles to fixed palette entries. */
static int desktop_apply_ui_style(const BudoUiStyle *style)
{
    if (style == NULL) return 0;
    for (int i = 0; i < 16; ++i) {
        for (int channel = 0; channel < 3; ++channel) {
            if (style->colors[i][channel] > 63) return 0;
        }
    }
    desktop_ui_style = *style;
    for (int i = 0; i < 16; ++i) {
        set_palette_entry((unsigned char)i, style->colors[i][0],
                          style->colors[i][1], style->colors[i][2]);
    }
    return 1;
}

static void set_classic_gui_palette(void)
{
    (void)desktop_apply_ui_style(&desktop_ui_style);
    for (int i = 16; i < 256; ++i) bw_palette_rgb((unsigned int)i, budo_palette_rgb(i));
}

static int ui_colors_save(const unsigned char *colors)
{
    char path[MAX_PATH], temporary[MAX_PATH];
    if (!copy_text(path, sizeof(path), bw_state_file("ui-colors.state")) ||
        !copy_text(temporary, sizeof(temporary), bw_state_file("ui-colors.tmp"))) return 0;
    FILE *file = fopen(temporary, "wb");
    if (!file) { perror(path); return 0; }
    int ok = fwrite(colors, 1, BUDO_SYS_COLOR_COUNT, file) == BUDO_SYS_COLOR_COUNT;
    if (fclose(file) != 0) ok = 0;
    if (ok && rename(temporary, path) == 0) return 1;
    perror("Save UI colors");
    (void)unlink(temporary);
    return 0;
}

static void ui_colors_load(void)
{
    unsigned char colors[BUDO_SYS_COLOR_COUNT];
    memcpy(ui_color_indices, ui_color_defaults, sizeof(ui_color_indices));
    FILE *file = fopen(bw_state_file("ui-colors.state"), "rb");
    if (!file) {
        if (errno != ENOENT) { perror("Load UI colors"); return; }
        file = fopen(bw_state_file("terminal-colors.state"), "r");
        if (file) {
            int background, foreground;
            if (fscanf(file, "%d %d", &background, &foreground) == 2 &&
                background >= 0 && background <= 255 && foreground >= 0 && foreground <= 255) {
                ui_color_indices[BUDO_SYS_COLOR_TERMINAL_BG] = (unsigned char)background;
                ui_color_indices[BUDO_SYS_COLOR_TERMINAL_TEXT] = (unsigned char)foreground;
            }
            if (fclose(file) != 0) perror("Load terminal colors");
        }
        return;
    }
    size_t count = fread(colors, 1, sizeof(colors), file);
    int extra = fgetc(file);
    if (fclose(file) != 0) { perror("Load UI colors"); return; }
    if (count == sizeof(colors) && extra == EOF) memcpy(ui_color_indices, colors, sizeof(colors));
    else fprintf(stderr, "BUDOWIN: invalid UI colors file; using defaults\n");
}

static int bwa_set_system_color(int role, int index)
{
    if (role < 0 || role >= BUDO_SYS_COLOR_COUNT || index < 0 || index > 255) return 0;
    unsigned char colors[BUDO_SYS_COLOR_COUNT];
    memcpy(colors, ui_color_indices, sizeof(colors));
    colors[role] = (unsigned char)index;
    if (!ui_colors_save(colors)) return 0;
    memcpy(ui_color_indices, colors, sizeof(colors));
    return 1;
}

static int bwa_reset_system_colors(void)
{
    if (!ui_colors_save(ui_color_defaults)) return 0;
    memcpy(ui_color_indices, ui_color_defaults, sizeof(ui_color_indices));
    return 1;
}

static int vesa_copy_to_screen(unsigned long offset,
                               const unsigned char *data, size_t length,
                               const unsigned char *mask)
{
    return bw_screen_copy(offset, data, length,
                          rgb_framebuffer + offset, mask);
}
static int present_framebuffer(void)
{
    return bw_screen_copy(0, framebuffer, sizeof(framebuffer),
                          rgb_framebuffer, rgb_mask);
}
static int mouse_init(void) { return 1; }
static void mouse_get_state(int *x, int *y, int *buttons)
{
    bw_mouse_state(x, y, buttons);
}

static void put_pixel(int x, int y, unsigned char color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    framebuffer[y * SCREEN_WIDTH + x] = color;
    rgb_mask[y * SCREEN_WIDTH + x] = 0;
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    int start_x = x;
    int start_y = y;
    int end_x = x + w;
    int end_y = y + h;
    int py;

    if (w <= 0 || h <= 0 ||
        end_x <= 0 || end_y <= 0 ||
        start_x >= SCREEN_WIDTH || start_y >= SCREEN_HEIGHT) {
        return;
    }

    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;
    if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;
    if (end_y > SCREEN_HEIGHT) end_y = SCREEN_HEIGHT;

    for (py = start_y; py < end_y; ++py) {
        memset(&framebuffer[py * SCREEN_WIDTH + start_x],
               color, (size_t)(end_x - start_x));
        memset(&rgb_mask[py * SCREEN_WIDTH + start_x],
               0, (size_t)(end_x - start_x));
    }
}

/* Exact RGB drawing shares clipping with the indexed UI primitives. */
static void put_rgb_pixel(int x, int y, uint32_t rgb)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    rgb_framebuffer[y * SCREEN_WIDTH + x] = 0xff000000u | (rgb & 0xffffffu);
    rgb_mask[y * SCREEN_WIDTH + x] = 1;
}

static void fill_rect_rgb(int x, int y, int w, int h, unsigned int rgb)
{
    if (w <= 0 || h <= 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT ||
        (int64_t)x + w <= 0 || (int64_t)y + h <= 0) return;
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = (int64_t)x + w > SCREEN_WIDTH ? SCREEN_WIDTH : x + w;
    int end_y = (int64_t)y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    for (int py = start_y; py < end_y; ++py) {
        for (int px = start_x; px < end_x; ++px) put_rgb_pixel(px, py, rgb);
    }
}

static void draw_rect(int x, int y, int w, int h, unsigned char color)
{
    int i;

    for (i = 0; i < w; ++i) {
        put_pixel(x + i, y, color);
        put_pixel(x + i, y + h - 1, color);
    }

    for (i = 0; i < h; ++i) {
        put_pixel(x, y + i, color);
        put_pixel(x + w - 1, y + i, color);
    }
}

static void draw_bevel(int x, int y, int w, int h, int raised)
{
    unsigned char top_left = raised ?
        WINDOW_HIGHLIGHT_COLOR : WINDOW_SHADOW_COLOR;
    unsigned char bottom_right = raised ?
        WINDOW_SHADOW_COLOR : WINDOW_HIGHLIGHT_COLOR;
    int i;

    if (w < 2 || h < 2) return;

    for (i = 0; i < w; ++i) {
        put_pixel(x + i, y, top_left);
        put_pixel(x + i, y + h - 1, bottom_right);
    }
    for (i = 0; i < h; ++i) {
        put_pixel(x, y + i, top_left);
        put_pixel(x + w - 1, y + i, bottom_right);
    }
}

/* Draw-time regions follow the same front-to-back ownership as windows. */
typedef struct UiRegion {
    int x, y, w, h, owner, scope, cursor, button;
    unsigned int state;
    char tip[MAX_NAME];
} UiRegion;
#define UI_OVERLAY_OWNER (-400)
static UiRegion ui_regions[512], ui_capture;
static int ui_region_count, ui_captured, ui_draw_owner, ui_draw_scope;
static int ui_pointer_x = -1, ui_pointer_y = -1, ui_pointer_buttons;
static int ui_tip_x = -1, ui_tip_y = -1, ui_tip_visible;
static clock_t ui_tip_since;
static int ui_tip_scope, ui_tip_owner;
static int ui_busy;
static int desktop_point_owner(int x, int y);
static int window_resize_cursor_at(int x, int y);
static void bwa_draw_standard_button(int x, int y, int w, int h,
                                     const char *label, int pressed);
static void bwa_draw_button_state(int x, int y, int w, int h,
                                  const char *label, unsigned int state);

static int ui_scope(void)
{
    if (confirm_kind) return -100 - confirm_kind;
    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) return -200 - editor_file_dialog;
    if (active_window == APP_EDITOR && editor_search_dialog_active()) return -300;
    return 0;
}

static int ui_region_contains(const UiRegion *region, int x, int y)
{
    return point_in_rect(x, y, region->x, region->y, region->w, region->h);
}

static int ui_region_visible(const UiRegion *region, int x, int y)
{
    return region->scope == ui_scope() &&
           (region->scope || region->owner == UI_OVERLAY_OWNER ||
            region->owner == desktop_point_owner(x, y));
}

static void ui_register(int x, int y, int w, int h, int cursor,
                         const char *tip, int button, unsigned int state)
{
    if (ui_region_count >= (int)(sizeof(ui_regions) / sizeof(ui_regions[0]))) return;
    UiRegion *region = &ui_regions[ui_region_count++];
    *region = (UiRegion){.x=x, .y=y, .w=w, .h=h, .owner=ui_draw_owner,
                        .scope=ui_draw_scope, .cursor=cursor, .button=button, .state=state};
    snprintf(region->tip, sizeof(region->tip), "%s", tip ? tip : "");
}

static unsigned long long bwa_get_time_ms(void)
{
    return (unsigned long long)(bw_clock() / (CLOCKS_PER_SEC / 1000));
}

static void bwa_pointer_region(int x, int y, int w, int h, int cursor,
                                const char *tooltip)
{
    int overlay = (cursor & BUDO_CURSOR_OVERLAY) != 0;
    cursor &= ~BUDO_CURSOR_OVERLAY;
    if (cursor < BUDO_CURSOR_ARROW || cursor > BUDO_CURSOR_RESIZE_NESW) return;
    int owner = ui_draw_owner;
    if (overlay && (owner == active_window || owner == APP_NONE))
        ui_draw_owner = UI_OVERLAY_OWNER;
    ui_register(x, y, w, h, cursor, tooltip, 0, 0);
    ui_draw_owner = owner;
}

static int ui_hit(int x, int y, int buttons_only)
{
    for (int i = ui_region_count - 1; i >= 0; --i)
        if (ui_region_contains(&ui_regions[i], x, y) &&
            ui_region_visible(&ui_regions[i], x, y))
            return !buttons_only || ui_regions[i].button ? i : -1;
    return -1;
}

static int ui_same_control(const UiRegion *a, const UiRegion *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
           a->owner == b->owner && a->scope == b->scope && !strcmp(a->tip, b->tip);
}

/* Returns whether the existing application down-handler should run. */
static int ui_button_event(int x, int y, int down, int up)
{
    int hit = ui_hit(x, y, 1);
    if (down && hit >= 0) {
        ui_capture = ui_regions[hit];
        ui_captured = 1;
        return 0;
    }
    if (up && ui_captured) {
        int activate = hit >= 0 && ui_same_control(&ui_capture, &ui_regions[hit]) &&
                       !(ui_regions[hit].state & BUDO_BUTTON_DISABLED);
        ui_captured = 0;
        return activate;
    }
    return down && !ui_captured;
}

static int ui_cursor_kind(int x, int y)
{
    if (ui_busy) return BUDO_CURSOR_BUSY;
    if (explorer_divider_dragging) return EXPLORER_DIVIDER_CURSOR;
    int hit = ui_hit(x, y, 0);
    if (ui_scope() == 0 && (hit < 0 || ui_regions[hit].owner != UI_OVERLAY_OWNER)) {
        int cursor = window_resize_cursor_at(x, y);
        if (cursor != BUDO_CURSOR_ARROW) return cursor;
    }
    if (hit >= 0) return ui_regions[hit].cursor;
    return BUDO_CURSOR_ARROW;
}

static int ui_cursor_ink(int kind, int x, int y)
{
    if (kind == EXPLORER_DIVIDER_CURSOR)
        return (y == CONTEXT_CURSOR_HOTSPOT && x >= 1 && x <= 11) ||
               (x >= 1 && x <= 4 && abs(y - CONTEXT_CURSOR_HOTSPOT) == x - 1) ||
               (x >= 8 && x <= 11 && abs(y - CONTEXT_CURSOR_HOTSPOT) == 11 - x);
    if (kind == BUDO_CURSOR_RESIZE_EW) return ui_cursor_ink(EXPLORER_DIVIDER_CURSOR, x, y);
    if (kind == BUDO_CURSOR_RESIZE_NS) return ui_cursor_ink(EXPLORER_DIVIDER_CURSOR, y, x);
    if (kind == BUDO_CURSOR_RESIZE_NESW) return ui_cursor_ink(BUDO_CURSOR_RESIZE, 12 - x, y);
    if (kind == BUDO_CURSOR_TEXT)
        return (x == CONTEXT_CURSOR_HOTSPOT && y >= 2 && y <= 10) ||
               ((y == 2 || y == 10) && x >= 4 && x <= 8);
    if (kind == BUDO_CURSOR_CROSSHAIR)
        return (x == CONTEXT_CURSOR_HOTSPOT && y >= 2 && y <= 10) ||
               (y == CONTEXT_CURSOR_HOTSPOT && x >= 2 && x <= 10);
    if (kind == BUDO_CURSOR_RESIZE)
        return (x == y && x >= 2 && x <= 10) ||
               (x == 2 && y >= 2 && y <= 5) || (y == 2 && x >= 2 && x <= 5) ||
               (x == 10 && y >= 7 && y <= 10) || (y == 10 && x >= 7 && x <= 10);
    if (kind == BUDO_CURSOR_BUSY)
        return ((y == 2 || y == 10) && x >= 3 && x <= 9) ||
               (y >= 3 && y <= 9 && (x == y || x == 12 - y));
    return 0;
}

static unsigned char ui_cursor_pixel(int kind, int x, int y)
{
    if (ui_cursor_ink(kind, x, y)) return MOUSE_CURSOR_OUTLINE_COLOR;
    if (ui_cursor_ink(kind, x - 1, y) || ui_cursor_ink(kind, x + 1, y) ||
        ui_cursor_ink(kind, x, y - 1) || ui_cursor_ink(kind, x, y + 1))
        return MOUSE_CURSOR_FILL_COLOR;
    return PCX_TRANSPARENT;
}

static void ui_tooltip_draw(void)
{
    int hit = ui_hit(ui_pointer_x, ui_pointer_y, 0);
    if (hit < 0 || !ui_regions[hit].tip[0] || !ui_tip_visible ||
        ui_pointer_buttons || ui_captured) return;
    const char *tip = ui_regions[hit].tip;
    char cells[MAX_NAME];
    bw_text_decode(cells, tip);
    tip = cells;
    int length = (int)strlen(tip);
    int cols = length > 100 ? 100 : length;
    int rows = (length + cols - 1) / cols;
    int w = cols * 6 + 10;
    int h = rows * 9 + 8;
    if (h < 18) h = 18;
    int x = ui_pointer_x + 12, y = ui_pointer_y + 22;
    if (x + w > SCREEN_WIDTH) x = SCREEN_WIDTH - w;
    if (y + h > SCREEN_HEIGHT) y = ui_pointer_y - h - 2;
    if (y < 0) y = 0;
    fill_rect(x, y, w, h, 8U);
    draw_rect(x, y, w, h, TEXT_COLOR);
    for (int row = 0; row < rows; ++row)
        draw_text(x + 5, y + 5 + row * 9, tip + row * cols, TEXT_COLOR, cols);
}

static unsigned char window_title_text_color(void)
{
    return ui_draw_owner == active_window || ui_draw_scope ? TITLE_TEXT_COLOR : TEXT_COLOR;
}

/* Resize direction bits: left, right, top, bottom. */
static int window_resize_edges(const AppWindow *w, int x, int y)
{
    if (w->maximized || !window_contains(w, x, y)) return 0;
    int left = x - w->x, top = y - w->y;
    int right = w->w - 1 - left, bottom = w->h - 1 - top;
    int corner = (left < 8 || right < 8) && (top < 8 || bottom < 8);
    int reach = corner ? 8 : WINDOW_BORDER;
    return (left < reach ? 1 : right < reach ? 2 : 0) |
           (top < reach ? 4 : bottom < reach ? 8 : 0);
}

static AppWindow *window_at_point(int x, int y)
{
    int owner = desktop_point_owner(x, y);
    AppWindow *builtin = builtin_window(owner);
    if (builtin) return builtin;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (bwa_instances[i].definition.runtime_id == owner && bwa_instances[i].managed_window)
            return &bwa_instances[i].window;
    return NULL;
}

static int window_resize_cursor_at(int x, int y)
{
    AppWindow *w = window_at_point(x, y);
    if (!w) return BUDO_CURSOR_ARROW;
    int edges = w->resizing ? w->resizing : window_resize_edges(w, x, y);
    if ((edges & 3) && (edges & 12))
        return edges == 5 || edges == 10 ? BUDO_CURSOR_RESIZE : BUDO_CURSOR_RESIZE_NESW;
    if (edges & 3) return BUDO_CURSOR_RESIZE_EW;
    if (edges & 12) return BUDO_CURSOR_RESIZE_NS;
    return BUDO_CURSOR_ARROW;
}

static int window_begin_resize(AppWindow *w, int x, int y)
{
    int edges = window_resize_edges(w, x, y);
    if (!edges) return 0;
    w->resizing = edges;
    w->resize_mouse_x = x;
    w->resize_mouse_y = y;
    w->resize_x = w->x;
    w->resize_y = w->y;
    w->resize_w = w->w;
    w->resize_h = w->h;
    return 1;
}

static void window_begin_drag(AppWindow *w, int x, int y)
{
    w->dragging = 1;
    w->drag_dx = x - w->x;
    w->drag_dy = y - w->y;
    w->resize_mouse_x = x;
    w->resize_mouse_y = y;
}

static void draw_window_chrome(const AppWindow *window)
{
    if (!window->maximized) {
        int x = window->x, y = window->y, w = window->w, h = window->h;
        bwa_pointer_region(x, y + 8, 4, h - 16, BUDO_CURSOR_RESIZE_EW, "Resize window");
        bwa_pointer_region(x + w - 4, y + 8, 4, h - 16, BUDO_CURSOR_RESIZE_EW, "Resize window");
        bwa_pointer_region(x + 8, y, w - 16, 4, BUDO_CURSOR_RESIZE_NS, "Resize window");
        bwa_pointer_region(x + 8, y + h - 4, w - 16, 4, BUDO_CURSOR_RESIZE_NS, "Resize window");
        bwa_pointer_region(x, y, 8, 8, BUDO_CURSOR_RESIZE, "Resize window");
        bwa_pointer_region(x + w - 8, y + h - 8, 8, 8, BUDO_CURSOR_RESIZE, "Resize window");
        bwa_pointer_region(x + w - 8, y, 8, 8, BUDO_CURSOR_RESIZE_NESW, "Resize window");
        bwa_pointer_region(x, y + h - 8, 8, 8, BUDO_CURSOR_RESIZE_NESW, "Resize window");
    }
    fill_rect(window->x, window->y,
              window->w, window->h, WINDOW_CHROME_COLOR);

    draw_rect(window->x, window->y,
              window->w, window->h, TEXT_COLOR);

    fill_rect(window->x + 1, window->y + 1,
              window->w - 2, window->h - 2,
              WINDOW_CHROME_COLOR);

    fill_rect(window->x + 1, window->y + 1,
              window->w - 2, 1, WINDOW_HIGHLIGHT_COLOR);
    fill_rect(window->x + 1, window->y + 1,
              1, window->h - 2, WINDOW_HIGHLIGHT_COLOR);

    fill_rect(window->x + window->w - 2,
              window->y + 1,
              1, window->h - 2,
              WINDOW_FRAME_COLOR);
    fill_rect(window->x + 1,
              window->y + window->h - 2,
              window->w - 2, 1,
              WINDOW_FRAME_COLOR);

    fill_rect(window->x + WINDOW_BORDER,
              window->y + WINDOW_BORDER,
              window->w - WINDOW_BORDER * 2,
              window->h - WINDOW_BORDER * 2,
              WINDOW_FACE_COLOR);

    fill_rect(window->x + WINDOW_BORDER,
              window->y + WINDOW_BORDER,
              window->w - WINDOW_BORDER * 2,
              WINDOW_TITLE_H - WINDOW_BORDER,
              ui_draw_owner == active_window || ui_draw_scope ? TITLE_COLOR : TITLE_INACTIVE_COLOR);
}

static int window_close_button_x(const AppWindow *window)
{
    return window->x + window->w - WINDOW_BORDER - 14;
}

static int window_max_button_x(const AppWindow *window)
{
    return window_close_button_x(window) - 16;
}

static int window_min_button_x(const AppWindow *window)
{
    return window_max_button_x(window) - 16;
}

static void window_open_state(AppWindow *window)
{
    window->open = 1;
    window->minimized = 0;
}

static void window_minimize_state(AppWindow *window)
{
    window->minimized = 1;
    window->dragging = 0;
    window->resizing = 0;
}

static void window_close_state(AppWindow *window)
{
    window->open = 0;
    window->minimized = 0;
    window->dragging = 0;
    window->resizing = 0;
}

static void window_toggle_maximize_state(AppWindow *window)
{
    if (!window->maximized) {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_w = window->w;
        window->restore_h = window->h;
        window->x = 0;
        window->y = 21;
        window->w = SCREEN_WIDTH;
        window->h = SCREEN_HEIGHT - 21;
        window->maximized = 1;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->w = window->restore_w;
        window->h = window->restore_h;
        window->maximized = 0;
    }

    window->dragging = 0;
    window->resizing = 0;
}

static int window_frame_mouse_down(AppWindow *window, int x, int y)
{
    int min_x;
    int max_x;
    int close_x;

    if (!window_contains(window, x, y)) {
        return WINDOW_FRAME_NONE;
    }

    min_x = window_min_button_x(window);
    max_x = window_max_button_x(window);
    close_x = window_close_button_x(window);

    if (point_in_rect(x, y, close_x, window->y + WINDOW_BORDER, 14, 14)) {
        return WINDOW_FRAME_CLOSE;
    }
    if (point_in_rect(x, y, max_x, window->y + WINDOW_BORDER, 14, 14)) {
        return WINDOW_FRAME_MAXIMIZE;
    }
    if (point_in_rect(x, y, min_x, window->y + WINDOW_BORDER, 14, 14)) {
        return WINDOW_FRAME_MINIMIZE;
    }
    if (window_begin_resize(window, x, y)) return WINDOW_FRAME_RESIZE;
    if (point_in_rect(x, y, window->x + 2, window->y + 2,
                      window->w - 52, WINDOW_TITLE_H - 2)) {
        window_begin_drag(window, x, y);
        return WINDOW_FRAME_DRAG;
    }

    return WINDOW_FRAME_CLIENT;
}

static int window_update_pointer(AppWindow *window,
                                 int mouse_x, int mouse_y,
                                 int min_w, int min_h)
{
    int changed = 0;

    if (window->dragging) {
        if (window->maximized) {
            if (mouse_x == window->resize_mouse_x && mouse_y == window->resize_mouse_y) return 0;
            int grab = window->drag_dx * window->restore_w / window->w;
            int dy = window->drag_dy;
            window_toggle_maximize_state(window);
            window->dragging = 1;
            window->drag_dx = grab;
            window->drag_dy = dy;
        }
        int new_x = mouse_x - window->drag_dx;
        int new_y = mouse_y - window->drag_dy;

        if (new_x < 0) new_x = 0;
        if (new_y < 21) new_y = 21;
        if (new_x + window->w > SCREEN_WIDTH)
            new_x = SCREEN_WIDTH - window->w;
        if (new_y + window->h > SCREEN_HEIGHT)
            new_y = SCREEN_HEIGHT - window->h;

        if (new_x != window->x || new_y != window->y) {
            window->x = new_x;
            window->y = new_y;
            changed = 1;
        }
    }

    if (window->resizing) {
        int left = window->resize_x, top = window->resize_y;
        int right = left + window->resize_w, bottom = top + window->resize_h;
        int dx = mouse_x - window->resize_mouse_x, dy = mouse_y - window->resize_mouse_y;
        if (window->resizing & 1) {
            left += dx;
            if (left < 0) left = 0;
            if (left > right - min_w) left = right - min_w;
        }
        if (window->resizing & 2) {
            right += dx;
            if (right < left + min_w) right = left + min_w;
            if (right > SCREEN_WIDTH) right = SCREEN_WIDTH;
        }
        if (window->resizing & 4) {
            top += dy;
            if (top < 21) top = 21;
            if (top > bottom - min_h) top = bottom - min_h;
        }
        if (window->resizing & 8) {
            bottom += dy;
            if (bottom < top + min_h) bottom = top + min_h;
            if (bottom > SCREEN_HEIGHT) bottom = SCREEN_HEIGHT;
        }
        if (left != window->x || top != window->y || right - left != window->w || bottom - top != window->h) {
            window->x = left;
            window->y = top;
            window->w = right - left;
            window->h = bottom - top;
            changed = 1;
        }
    }

    return changed;
}

static void window_end_pointer(AppWindow *window)
{
    window->dragging = 0;
    window->resizing = 0;
}

static void draw_menu_bar_item(int x, int y, int w,
                               const char *label, int active)
{
    budo_menu_bar_item(&bwa_host_api, x, y, w, label, active);
}

static void draw_popup_menu(int x, int y, int w,
                            const BudoMenuItem *menu_items, int count)
{
    budo_menu_items_draw(&bwa_host_api, x, y, w, menu_items, count);
}

static unsigned int pcx_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned char pcx_rgb_to_index(unsigned char red,
                                      unsigned char green,
                                      unsigned char blue)
{
    int r = ((int)red * 5 + 127) / 255;
    int g = ((int)green * 5 + 127) / 255;
    int b = ((int)blue * 5 + 127) / 255;

    return (unsigned char)(16 + r * 36 + g * 6 + b);
}

typedef struct PcxRleState {
    int count;
    int value;
} PcxRleState;

static int pcx_decode_bytes(FILE *file,
                            unsigned char *dest,
                            int length,
                            int encoding,
                            PcxRleState *state)
{
    int written = 0;

    while (written < length) {
        if (state->count == 0) {
            int value = fgetc(file);

            if (value == EOF) return 0;

            state->count = 1;
            state->value = value;

            if (encoding == 1 && (value & 0xC0) == 0xC0) {
                state->count = value & 0x3F;
                state->value = fgetc(file);
                if (state->value == EOF) return 0;
            }
        }

        while (state->count > 0 && written < length) {
            dest[written++] = (unsigned char)state->value;
            --state->count;
        }
    }

    return 1;
}

static int load_pcx_image(const char *path,
                          int expected_w,
                          int expected_h,
                          unsigned char *dest,
                          uint32_t *rgb_dest)
{
    FILE *file;
    unsigned char header[128];
    unsigned char palette[768];
    unsigned char *scanline = NULL;
    unsigned int xmin;
    unsigned int ymin;
    unsigned int xmax;
    unsigned int ymax;
    int width;
    int height;
    int bits_per_pixel;
    int encoding;
    int planes;
    int bytes_per_line;
    int row_bytes;
    int palette_mode = 0;
    PcxRleState rle_state;
    int y;

    file = fopen(path, "rb");
    if (file == NULL) return 0;

    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        header[0] != 0x0A) {
        fclose(file);
        return 0;
    }

    encoding = header[2];
    bits_per_pixel = header[3];
    xmin = pcx_u16(&header[4]);
    ymin = pcx_u16(&header[6]);
    xmax = pcx_u16(&header[8]);
    ymax = pcx_u16(&header[10]);
    width = (int)(xmax - xmin + 1);
    height = (int)(ymax - ymin + 1);
    planes = header[65];
    bytes_per_line = (int)pcx_u16(&header[66]);

    if (width != expected_w || height != expected_h ||
        bytes_per_line <= 0 ||
        (encoding != 0 && encoding != 1)) {
        fclose(file);
        return 0;
    }

    if (bits_per_pixel == 8 && planes == 1) {
        long end_pos;

        if (bytes_per_line < width ||
            fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return 0;
        }

        end_pos = ftell(file);
        if (end_pos < 769L ||
            fseek(file, end_pos - 769L, SEEK_SET) != 0 ||
            fgetc(file) != 0x0C ||
            fread(palette, 1, sizeof(palette), file) != sizeof(palette)) {
            fclose(file);
            return 0;
        }

        palette_mode = 256;
    } else if ((bits_per_pixel == 1 &&
                (planes == 1 || planes == 2 || planes == 3 || planes == 4)) ||
               (bits_per_pixel == 4 && planes == 1)) {
        int i;

        for (i = 0; i < 16; ++i) {
            palette[i * 3] = header[16 + i * 3];
            palette[i * 3 + 1] = header[16 + i * 3 + 1];
            palette[i * 3 + 2] = header[16 + i * 3 + 2];
        }

        palette_mode = 16;
    } else if (bits_per_pixel == 8 && planes == 3) {
        if (bytes_per_line < width) {
            fclose(file);
            return 0;
        }
        palette_mode = 0;
    } else {
        fclose(file);
        return 0;
    }

    row_bytes = bytes_per_line * planes;
    rle_state.count = 0;
    rle_state.value = 0;
    scanline = (unsigned char *)malloc((size_t)row_bytes);
    if (scanline == NULL || fseek(file, 128L, SEEK_SET) != 0) {
        free(scanline);
        fclose(file);
        return 0;
    }

    for (y = 0; y < height; ++y) {
        int x;

        if (!pcx_decode_bytes(file, scanline, row_bytes,
                              encoding, &rle_state)) {
            free(scanline);
            fclose(file);
            return 0;
        }

        for (x = 0; x < width; ++x) {
            unsigned char red;
            unsigned char green;
            unsigned char blue;

            if (bits_per_pixel == 8 && planes == 3) {
                red = scanline[x];
                green = scanline[bytes_per_line + x];
                blue = scanline[bytes_per_line * 2 + x];
            } else {
                int source = 0;

                if (bits_per_pixel == 8) {
                    source = scanline[x];
                } else if (bits_per_pixel == 4) {
                    unsigned char packed = scanline[x / 2];
                    source = (x & 1) ?
                             (packed & 0x0F) : ((packed >> 4) & 0x0F);
                } else {
                    int plane;
                    int byte_index = x / 8;
                    int bit = 7 - (x & 7);

                    for (plane = 0; plane < planes; ++plane) {
                        if (scanline[plane * bytes_per_line + byte_index] &
                            (1U << bit)) {
                            source |= 1 << plane;
                        }
                    }
                }

                if ((palette_mode == 16 && source >= 16) ||
                    (palette_mode == 256 && source >= 256)) {
                    free(scanline);
                    fclose(file);
                    return 0;
                }

                red = palette[source * 3];
                green = palette[source * 3 + 1];
                blue = palette[source * 3 + 2];
            }

            dest[y * width + x] =
                pcx_rgb_to_index(red, green, blue);
            if (rgb_dest != NULL) {
                rgb_dest[y * width + x] = 0xff000000u |
                    ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
            }
        }
    }

    free(scanline);
    fclose(file);
    return 1;
}

static const uint32_t *pcx_icon_rgb(const unsigned char *indices)
{
    for (int i = 0; i < pcx_icon_color_count; ++i) {
        if (pcx_icon_colors[i].indices == indices) return pcx_icon_colors[i].rgb;
    }
    return NULL;
}

static void draw_pcx_icon(int x, int y, const unsigned char *pixels)
{
    int px;
    int py;
    const uint32_t *rgb = pcx_icon_rgb(pixels);

    for (py = 0; py < DESKTOP_ICON_H; ++py) {
        for (px = 0; px < DESKTOP_ICON_W; ++px) {
            unsigned char color =
                pixels[py * DESKTOP_ICON_W + px];

            if (color != PCX_TRANSPARENT) {
                if (rgb != NULL) {
                    put_rgb_pixel(x + px, y + py, rgb[py * DESKTOP_ICON_W + px]);
                } else {
                    put_pixel(x + px, y + py, color);
                }
            }
        }
    }
}

static void pcx_app_filename(char *dest, size_t dest_size,
                             const char *app_id)
{
    char base[9];
    int i = 0;

    while (*app_id != '\0' && i < 8) {
        unsigned char ch = (unsigned char)*app_id++;

        if (isalnum(ch) || ch == '_') {
            base[i++] = (char)toupper(ch);
        }
    }

    if (i == 0) {
        strcpy(base, "APP");
    } else {
        base[i] = '\0';
    }

    snprintf(dest, dest_size, "%s.PCX", base);
}

static int find_asset_filename(const char *dir,
                               const char *wanted,
                               char *found,
                               size_t found_size)
{
    char pattern[MAX_PATH];
    struct ffblk entry;
    int done;

    if (!join_path(pattern, sizeof(pattern), dir, "*.*")) {
        return 0;
    }

    done = findfirst(pattern, &entry,
                     FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH);
    while (!done) {
        if (!(entry.ff_attrib & FA_DIREC) &&
            compare_names_ci(entry.ff_name, wanted) == 0) {
            return copy_text(found, found_size, entry.ff_name);
        }
        done = findnext(&entry);
    }

    return 0;
}

static int load_named_pcx(const char *filename,
                          int width,
                          int height,
                          unsigned char *dest,
                          int icon_transparency)
{
    char pcx_dir[MAX_PATH];
    char path[MAX_PATH];
    char alternate[32];
    size_t len;
    uint32_t *rgb_dest = desktop_background_rgb;
    PcxIconColors *icon = NULL;
    if (dest != desktop_background) {
        for (int slot = 0; slot < pcx_icon_color_count; ++slot) {
            if (pcx_icon_colors[slot].indices == dest) icon = &pcx_icon_colors[slot];
        }
        if (icon == NULL && pcx_icon_color_count < BWA_MAX_EXTERNAL + 9) {
            icon = &pcx_icon_colors[pcx_icon_color_count];
        }
        if (icon == NULL || width * height > DESKTOP_ICON_W * DESKTOP_ICON_H) {
            fprintf(stderr, "BUDOWIN: too many or oversized PCX icons\n");
            return 0;
        }
        rgb_dest = icon->rgb;
    }
    int loaded = 0;
    int i;

    if (!join_path(pcx_dir, sizeof(pcx_dir), home_path, "PCX")) {
        return 0;
    }

    if (join_path(path, sizeof(path), pcx_dir, filename)) {
        loaded = load_pcx_image(path, width, height, dest, rgb_dest);
    }

    if (!loaded) {
        len = strlen(filename);
        if (len < sizeof(alternate)) {
            memcpy(alternate, filename, len + 1);

            if (len >= 4 &&
                alternate[len - 4] == '.' &&
                toupper((unsigned char)alternate[len - 3]) == 'P' &&
                toupper((unsigned char)alternate[len - 2]) == 'C' &&
                toupper((unsigned char)alternate[len - 1]) == 'X') {
                alternate[len - 3] = 'p';
                alternate[len - 2] = 'c';
                alternate[len - 1] = 'x';

                if (join_path(path, sizeof(path), pcx_dir, alternate)) {
                    loaded = load_pcx_image(path, width, height, dest, rgb_dest);
                }
            }
        }
    }

    if (!loaded) {
        len = strlen(filename);
        if (len < sizeof(alternate)) {
            for (i = 0; i < (int)len; ++i) {
                alternate[i] =
                    (char)tolower((unsigned char)filename[i]);
            }
            alternate[len] = '\0';

            if (join_path(path, sizeof(path), pcx_dir, alternate)) {
                loaded = load_pcx_image(path, width, height, dest, rgb_dest);
            }
        }
    }

    if (!loaded &&
        find_asset_filename(pcx_dir, filename,
                            alternate, sizeof(alternate)) &&
        join_path(path, sizeof(path), pcx_dir, alternate)) {
        loaded = load_pcx_image(path, width, height, dest, rgb_dest);
    }

    if (loaded && icon_transparency) {
        uint32_t transparent = rgb_dest[0];
        int count = width * height;

        for (i = 0; i < count; ++i) {
            if (rgb_dest[i] == transparent) {
                dest[i] = PCX_TRANSPARENT;
            }
        }
    }

    if (loaded && icon != NULL && icon->indices == NULL) {
        icon->indices = dest;
        ++pcx_icon_color_count;
    }
    return loaded;
}

static void load_pcx_assets(void)
{
    int i;

    desktop_background_loaded =
        load_named_pcx("BACKGR~1.PCX",
                       SCREEN_WIDTH, SCREEN_HEIGHT,
                       desktop_background, 0);
    if (!desktop_background_loaded) {
        desktop_background_loaded =
            load_named_pcx("BACKGROUND.PCX",
                           SCREEN_WIDTH, SCREEN_HEIGHT,
                           desktop_background, 0);
    }

    explorer_pcx_icon_loaded =
        load_named_pcx("EXPLORER.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       explorer_pcx_icon, 1);
    editor_pcx_icon_loaded =
        load_named_pcx("EDITOR.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       editor_pcx_icon, 1);
    folder_pcx_icon_loaded =
        load_named_pcx("FOLDER.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       folder_pcx_icon, 1);
    file_pcx_icon_loaded =
        load_named_pcx("FILE.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       file_pcx_icon, 1);
    exec_pcx_icon_loaded =
        load_named_pcx("EXEC.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       exec_pcx_icon, 1);
    text_pcx_icon_loaded =
        load_named_pcx("TEXT.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       text_pcx_icon, 1);
    code_pcx_icon_loaded =
        load_named_pcx("CODE.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       code_pcx_icon, 1);
    back_pcx_icon_loaded =
        load_named_pcx("BACK.PCX",
                       DESKTOP_ICON_W, DESKTOP_ICON_H,
                       back_pcx_icon, 1);
    cursor_pcx_icon_loaded =
        load_named_pcx("CURSOR.PCX",
                       MOUSE_CURSOR_W, MOUSE_CURSOR_H,
                       cursor_pcx_icon, 1);

    for (i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];
        char filename[16];

        pcx_app_filename(filename, sizeof(filename),
                         app->definition.app_id);
        app->pcx_icon_loaded =
            load_named_pcx(filename,
                           DESKTOP_ICON_W, DESKTOP_ICON_H,
                           app->pcx_icon, 1);
    }
}

static int point_in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static const unsigned char *glyph_for(char c)
{
    static const unsigned char blank[7] = {0,0,0,0,0,0,0};
    static const unsigned char dash[7] = {0,0,0,0x0E,0,0,0};
    static const unsigned char dot[7] = {0,0,0,0,0,0x04,0x04};
    static const unsigned char under[7] = {0,0,0,0,0,0,0x1F};
    static const unsigned char slash[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    static const unsigned char colon[7] = {0,0x04,0x04,0,0x04,0x04,0};
    static const unsigned char semicolon[7] = {0,0x04,0x04,0,0x04,0x04,0x08};
    static const unsigned char comma[7] = {0,0,0,0,0,0x04,0x08};
    static const unsigned char plus[7] = {0,0x04,0x04,0x1F,0x04,0x04,0};
    static const unsigned char equal[7] = {0,0,0x1F,0,0x1F,0,0};
    static const unsigned char lparen[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    static const unsigned char rparen[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    static const unsigned char lbracket[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    static const unsigned char rbracket[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    static const unsigned char star[7] = {0,0x15,0x0E,0x1F,0x0E,0x15,0};
    static const unsigned char hash[7] = {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0};
    static const unsigned char exclaim[7] = {0x04,0x04,0x04,0x04,0x04,0,0x04};
    static const unsigned char quote[7] = {0x0A,0x0A,0x0A,0,0,0,0};
    static const unsigned char dollar[7] = {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04};
    static const unsigned char percent[7] = {0x18,0x19,0x02,0x04,0x08,0x13,0x03};
    static const unsigned char amp[7] = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D};
    static const unsigned char apostrophe[7] = {0x04,0x04,0x08,0,0,0,0};
    static const unsigned char question[7] = {0x0E,0x11,0x01,0x02,0x04,0,0x04};
    static const unsigned char atsign[7] = {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E};
    static const unsigned char backslash[7] = {0x10,0x08,0x08,0x04,0x02,0x02,0x01};
    static const unsigned char caret[7] = {0x04,0x0A,0x11,0,0,0,0};
    static const unsigned char grave[7] = {0x08,0x04,0,0,0,0,0};
    static const unsigned char lbrace[7] = {0x03,0x04,0x04,0x08,0x04,0x04,0x03};
    static const unsigned char pipe[7] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04};
    static const unsigned char rbrace[7] = {0x18,0x04,0x04,0x02,0x04,0x04,0x18};
    static const unsigned char tilde[7] = {0,0,0x09,0x16,0,0,0};
    static const unsigned char less[7] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01};
    static const unsigned char greater[7] = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    static const unsigned char nordic_a_umlaut_lower[7] = {0x0A,0,0x0E,0x01,0x0F,0x11,0x0F};
    static const unsigned char nordic_a_umlaut_upper[7] = {0x0A,0,0x0E,0x11,0x1F,0x11,0x11};
    static const unsigned char nordic_a_ring_lower[7] = {0x04,0x0A,0x04,0x0E,0x01,0x11,0x0F};
    static const unsigned char nordic_a_ring_upper[7] = {0x04,0x0A,0x04,0x0E,0x11,0x1F,0x11};
    static const unsigned char nordic_o_umlaut_lower[7] = {0x0A,0,0x0E,0x11,0x11,0x11,0x0E};
    static const unsigned char nordic_o_umlaut_upper[7] = {0x0A,0,0x0E,0x11,0x11,0x11,0x0E};
    static const unsigned char nordic_ae_lower[7] = {0,0,0x1B,0x05,0x0F,0x14,0x0F};
    static const unsigned char nordic_ae_upper[7] = {0x0F,0x14,0x14,0x1E,0x14,0x14,0x17};
    static const unsigned char nordic_o_slash_lower[7] = {0,0x01,0x0E,0x13,0x15,0x19,0x0E};
    static const unsigned char nordic_o_slash_upper[7] = {0x01,0x0E,0x13,0x15,0x19,0x0E,0x10};
    static const unsigned char currency_sign[7] = {0,0x11,0x0E,0x0A,0x0E,0x11,0};
    static const unsigned char section_sign[7] = {0x0E,0x10,0x0E,0x11,0x0E,0x01,0x0E};
    static const unsigned char half_sign[7] = {0x11,0x12,0x04,0x08,0x13,0x05,0x07};
    static const unsigned char pound_sign[7] = {0x06,0x09,0x08,0x1E,0x08,0x08,0x1F};
    unsigned char uc = (unsigned char)c;

    if (c >= 'a' && c <= 'z') return font5x7_lower[c - 'a'];
    if (c >= 'A' && c <= 'Z') return font5x7[c - 'A'];
    if (c >= '0' && c <= '9') return font5x7[26 + c - '0'];

    switch (uc) {
        case 33: return exclaim;
        case 34: return quote;
        case 35: return hash;
        case 36: return dollar;
        case 37: return percent;
        case 38: return amp;
        case 39: return apostrophe;
        case 40: return lparen;
        case 41: return rparen;
        case 42: return star;
        case 43: return plus;
        case 44: return comma;
        case 45: return dash;
        case 46: return dot;
        case 47: return slash;
        case 58: return colon;
        case 59: return semicolon;
        case 60: return less;
        case 61: return equal;
        case 62: return greater;
        case 63: return question;
        case 64: return atsign;
        case 91: return lbracket;
        case 92: return backslash;
        case 93: return rbracket;
        case 94: return caret;
        case 95: return under;
        case 96: return grave;
        case 123: return lbrace;
        case 124: return pipe;
        case 125: return rbrace;
        case 126: return tilde;
        case 132: return nordic_a_umlaut_lower;
        case 134: return nordic_a_ring_lower;
        case 142: return nordic_a_umlaut_upper;
        case 143: return nordic_a_ring_upper;
        case 145: return nordic_ae_lower;
        case 146: return nordic_ae_upper;
        case 148: return nordic_o_umlaut_lower;
        case 153: return nordic_o_umlaut_upper;
        case 155: return nordic_o_slash_lower;
        case 156: return pound_sign;
        case 157: return nordic_o_slash_upper;
        case 164: return currency_sign;
        case 171: return half_sign;
        case 245: return section_sign;
        default: return blank;
    }
}

static void draw_char(int x, int y, char c, unsigned char color)
{
    const unsigned char *glyph = glyph_for(c);
    int row;
    int col;

    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 5; ++col) {
            if (glyph[row] & (0x10 >> col)) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, unsigned char color, int max_chars)
{
    int i;

    for (i = 0; *text != '\0' && i < max_chars; ++i) {
        draw_char(x + i * TEXT_CELL_WIDTH, y, (char)bw_text_cell(&text), color);
    }
}

static void draw_text_elided(int x, int y, const char *text, unsigned char color, int cols)
{
    char cells[MAX_NAME + 32];
    size_t len = strlen(text);
    if (len >= sizeof(cells)) len = sizeof(cells) - 1;
    memcpy(cells, text, len);
    cells[len] = '\0';
    bw_text_decode(cells, cells);
    if (cols >= 3 && strlen(cells) > (size_t)cols) {
        cells[cols - 3] = '.';
        cells[cols - 2] = '.';
        cells[cols - 1] = '.';
        cells[cols] = '\0';
    }
    draw_text(x, y, cells, color, cols);
}

static void draw_text_centered(int y, const char *text, unsigned char color)
{
    int length = (int)strlen(text);
    int width = length * 6 - 1;
    int x = (SCREEN_WIDTH - width) / 2;

    draw_text(x, y, text, color, length);
}

/*
 * Mouse pointer loaded from PCX/CURSOR.PCX.
 *
 * The existing PCX transparency convention is used: every source pixel
 * matching the image's top-left pixel becomes PCX_TRANSPARENT.
 */
static void restore_cursor_area(int x, int y)
{
    int row;
    x -= 8;
    y -= 8;

    for (row = 0; row < MOUSE_CURSOR_H + 8; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + MOUSE_CURSOR_W + 8;
        int offset;
        int length;

        if (sy < 0 || sy >= SCREEN_HEIGHT) {
            continue;
        }

        if (start_x < 0) start_x = 0;
        if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;

        length = end_x - start_x;
        if (length <= 0) {
            continue;
        }

        offset = sy * SCREEN_WIDTH + start_x;
        (void)vesa_copy_to_screen((unsigned long)offset,
                                  &framebuffer[offset],
                                  (size_t)length, &rgb_mask[offset]);
    }
}

static void draw_cursor_vga(int x, int y)
{
    unsigned char rowbuf[MOUSE_CURSOR_W];
    unsigned char rowmask[MOUSE_CURSOR_W];
    uint32_t rowrgb[MOUSE_CURSOR_W];
    const uint32_t *cursor_rgb = pcx_icon_rgb(cursor_pcx_icon);
    int row;
    int kind = ui_cursor_kind(x, y);
    if (kind != BUDO_CURSOR_ARROW) {
        cursor_rgb = NULL;
        x -= CONTEXT_CURSOR_HOTSPOT;
        y -= CONTEXT_CURSOR_HOTSPOT;
    }

    if (!cursor_pcx_icon_loaded) {
        return;
    }

    for (row = 0; row < MOUSE_CURSOR_H; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + MOUSE_CURSOR_W;
        int col;
        int length;
        int offset;

        if (sy < 0 || sy >= SCREEN_HEIGHT) {
            continue;
        }

        if (start_x < 0) start_x = 0;
        if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;

        length = end_x - start_x;
        if (length <= 0) {
            continue;
        }

        for (col = 0; col < length; ++col) {
            int sx = start_x + col;
            int image_x = sx - x;
            unsigned char pixel = framebuffer[sy * SCREEN_WIDTH + sx];
            unsigned char cursor_pixel =
                kind == BUDO_CURSOR_ARROW ? cursor_pcx_icon[row * MOUSE_CURSOR_W + image_x] :
                ui_cursor_pixel(kind, image_x, row);
            if (kind == BUDO_CURSOR_ARROW && !cursor_rgb && cursor_pixel != PCX_TRANSPARENT)
                cursor_pixel = cursor_pixel == 1 ? MOUSE_CURSOR_OUTLINE_COLOR : MOUSE_CURSOR_FILL_COLOR;

            if (cursor_pixel != PCX_TRANSPARENT) {
                pixel = cursor_pixel;
            }

            rowbuf[col] = pixel;
            rowmask[col] = cursor_pixel == PCX_TRANSPARENT ?
                rgb_mask[sy * SCREEN_WIDTH + sx] : cursor_rgb != NULL;
            rowrgb[col] = cursor_pixel == PCX_TRANSPARENT ?
                rgb_framebuffer[sy * SCREEN_WIDTH + sx] :
                cursor_rgb != NULL ? cursor_rgb[row * MOUSE_CURSOR_W + image_x] : 0;
        }

        offset = sy * SCREEN_WIDTH + start_x;
        (void)bw_screen_copy((unsigned long)offset, rowbuf,
                             (size_t)length, rowrgb, rowmask);
    }
}

static int ui_busy_begin(void)
{
    int previous = ui_busy;
    ui_busy = 1;
    draw_cursor_vga(ui_pointer_x, ui_pointer_y);
    bw_screen_flush();
    return previous;
}

static void ui_busy_end(int previous)
{
    ui_busy = previous;
    restore_cursor_area(ui_pointer_x, ui_pointer_y);
    draw_cursor_vga(ui_pointer_x, ui_pointer_y);
}

static int text_caret_rect(int *x, int *y)
{
    if (confirm_kind || editor_file_dialog != EDITOR_FILE_DIALOG_NONE ||
        explorer_file_menu || explorer_view_menu || terminal_file_menu) return 0;
    if (active_window == APP_EXPLORER &&
        explorer_window.open &&
        !explorer_window.minimized &&
        explorer_rename_active) {
        int dialog_w =
            explorer_window.w > 360 ? 320 : explorer_window.w - 30;
        int dialog_x =
            explorer_window.x + (explorer_window.w - dialog_w) / 2;
        int dialog_y =
            explorer_window.y + (explorer_window.h - 66) / 2;
        int caret_x = dialog_x + 12 + explorer_rename_len * 6;
        int max_x = dialog_x + dialog_w - 12;

        if (caret_x >= max_x) return 0;

        *x = caret_x;
        *y = dialog_y + 35;
        return 1;
    }

    if (active_window == APP_EDITOR &&
        editor_window.open &&
        !editor_window.minimized) {
        if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
            int dialog_w =
                editor_window.w > 470 ? 440 : editor_window.w - 30;
            int dialog_h =
                editor_window.h > 310 ? 270 : editor_window.h - 40;
            int dialog_x =
                editor_window.x + (editor_window.w - dialog_w) / 2;
            int dialog_y =
                editor_window.y + (editor_window.h - dialog_h) / 2;
            int caret_x = dialog_x + 77 + editor_file_name_len * 6;
            int max_x = dialog_x + dialog_w - 12;

            if (caret_x >= max_x) return 0;

            *x = caret_x;
            *y = dialog_y + dialog_h - 48;
            return 1;
        }

        if (editor_menu != EDITOR_MENU_NONE || editor_search_dialog_active())
            return 0;
        if (editor_dialog != EDITOR_DIALOG_NONE) {
            int dialog_w =
                editor_window.w > 390 ? 360 : editor_window.w - 30;
            int dialog_x =
                editor_window.x + (editor_window.w - dialog_w) / 2;
            int dialog_y =
                editor_window.y + editor_window.h / 2 - 32;
            int caret_x = dialog_x + 10 + editor_dialog_len * 6;
            int max_x = dialog_x + dialog_w - 12;

            if (caret_x >= max_x) return 0;

            *x = caret_x;
            *y = dialog_y + 29;
            return 1;
        }

        {
            int ruler_h =
                editor_writer_mode && editor_writer_ruler ?
                EDITOR_RULER_H : 0;
            int text_x = editor_text_x();
            int text_y =
                editor_window.y + WINDOW_TITLE_H +
                EDITOR_MENU_H + ruler_h + 5;
            int row = editor_cursor_screen_row();
            int col = editor_cursor_screen_col();
            int rows = editor_visible_rows();
            int cols = editor_visible_cols();

            if (row < 0 || row >= rows ||
                col < 0 || col >= cols) {
                return 0;
            }

            *x = text_x + col * 6;
            *y = text_y + row * 9;
            return 1;
        }
    }

    if (active_window == APP_TERMINAL &&
        terminal_window.open &&
        !terminal_window.minimized &&
        !terminal_paging) {
        int caret = session_caret(x, y);
        if (caret >= 0) return caret;
        char prompt[MAX_PATH + TERMINAL_INPUT_LEN + 2];
        int text_x = terminal_window.x + 7;
        int cols = (terminal_window.w - 30) / 6;
        int prompt_len;
        int cursor_position;
        int visible_start = 0;
        int caret_col;

        snprintf(prompt, sizeof(prompt), "%s>%s",
                 terminal_cwd, terminal_input);
        prompt_len = (int)strlen(prompt);
        cursor_position =
            (int)strlen(terminal_cwd) + 1 + terminal_cursor;

        if (prompt_len > cols) {
            visible_start = cursor_position - cols + 1;
            if (visible_start < 0) visible_start = 0;
            if (visible_start > prompt_len - cols) {
                visible_start = prompt_len - cols;
            }
        }

        caret_col = cursor_position - visible_start;
        if (caret_col < 0 || caret_col >= cols) return 0;

        *x = text_x + caret_col * 6;
        *y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6 +
             ((terminal_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 12) / 9 - 1) * 9;
        return 1;
    }

    return 0;
}

static void restore_text_caret_vga(void)
{
    int x;
    int y;
    int row;

    if (!text_caret_rect(&x, &y)) return;
    --y;

    for (row = 0; row < TEXT_CELL_HEIGHT; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + TEXT_CELL_WIDTH;
        int offset;
        int length;

        if (sy < 0 || sy >= SCREEN_HEIGHT) continue;
        if (start_x < 0) start_x = 0;
        if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;

        length = end_x - start_x;
        if (length <= 0) continue;

        offset = sy * SCREEN_WIDTH + start_x;
        (void)vesa_copy_to_screen((unsigned long)offset,
                                  &framebuffer[offset],
                                  (size_t)length, &rgb_mask[offset]);
    }
}

static void draw_text_caret_vga(void)
{
    unsigned char rowbuf[TEXT_CELL_WIDTH];
    int x;
    int y;
    int row;
    int col;

    if (!text_caret_rect(&x, &y)) return;
    --y;

    for (row = 0; row < TEXT_CELL_HEIGHT; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + TEXT_CELL_WIDTH;
        int offset;
        int length;

        if (sy < 0 || sy >= SCREEN_HEIGHT) continue;
        if (start_x < 0) start_x = 0;
        if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;

        length = end_x - start_x;
        if (length <= 0) continue;

        offset = sy * SCREEN_WIDTH + start_x;
        if (active_window == APP_TERMINAL) {
            terminal_draw_caret_row((unsigned long)offset, (size_t)length);
            continue;
        }
        for (col = 0; col < length; ++col) {
            unsigned char underlying = framebuffer[offset + col];
            rowbuf[start_x - x + col] =
                underlying == FILE_COLOR ? TEXT_COLOR : FILE_COLOR;
        }
        (void)vesa_copy_to_screen((unsigned long)offset,
                                  rowbuf + (start_x - x),
                                  (size_t)length, NULL);
    }
}

static int desktop_slot_count(void)
{
    return DESKTOP_GRID_COLS * DESKTOP_GRID_ROWS;
}

static void desktop_slot_position(int slot, int *x, int *y)
{
    int col;
    int row;

    if (slot < 0) {
        slot = 0;
    }
    if (slot >= desktop_slot_count()) {
        slot = desktop_slot_count() - 1;
    }

    col = slot % DESKTOP_GRID_COLS;
    row = slot / DESKTOP_GRID_COLS;

    *x = DESKTOP_GRID_X + col * DESKTOP_GRID_X_STEP;
    *y = DESKTOP_GRID_Y + row * DESKTOP_GRID_Y_STEP;
}

static int desktop_slot_from_point(int x, int y)
{
    int col = (x - DESKTOP_GRID_X + DESKTOP_GRID_X_STEP / 2) /
              DESKTOP_GRID_X_STEP;
    int row = (y - DESKTOP_GRID_Y + DESKTOP_GRID_Y_STEP / 2) /
              DESKTOP_GRID_Y_STEP;

    if (col < 0) col = 0;
    if (col >= DESKTOP_GRID_COLS) col = DESKTOP_GRID_COLS - 1;
    if (row < 0) row = 0;
    if (row >= DESKTOP_GRID_ROWS) row = DESKTOP_GRID_ROWS - 1;

    return row * DESKTOP_GRID_COLS + col;
}

static int save_desktop_layout(void)
{
    FILE *file = fopen(DESKTOP_STATE_FILE, "wt");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "%d %d\n",
            explorer_desktop_slot,
            editor_desktop_slot);

    {
        int i;
        for (i = 0; i < bwa_external_app_count; ++i) {
            const char *app_id = bwa_external_apps[i].definition.app_id;

            if (app_id != NULL) {
                fprintf(file, "%s %d\n",
                        app_id,
                        bwa_external_apps[i].desktop_slot);
            }
        }
    }

    fclose(file);
    return 1;
}

static void load_desktop_layout(void)
{
    FILE *file = fopen(DESKTOP_STATE_FILE, "rt");
    int explorer_slot;
    int editor_slot;

    if (file == NULL) {
        return;
    }

    if (fscanf(file, "%d %d", &explorer_slot, &editor_slot) >= 1) {
        if (explorer_slot >= 0 && explorer_slot < desktop_slot_count()) {
            explorer_desktop_slot = explorer_slot;
        }
        if (editor_slot >= 0 && editor_slot < desktop_slot_count()) {
            editor_desktop_slot = editor_slot;
        }
    }

    {
        char app_id[BWA_ID_LEN];
        int slot;

        while (fscanf(file, "%15s %d", app_id, &slot) == 2) {
            int i;

            if (slot < 0 || slot >= desktop_slot_count()) {
                continue;
            }

            for (i = 0; i < bwa_external_app_count; ++i) {
                const char *loaded_id =
                    bwa_external_apps[i].definition.app_id;

                if (loaded_id != NULL &&
                    strcmp(loaded_id, app_id) == 0) {
                    bwa_external_apps[i].desktop_slot = slot;
                    break;
                }
            }
        }
    }

    fclose(file);
}

static int editor_wrap_enabled(void)
{
    return editor_writer_mode || editor_word_wrap;
}

static int editor_rows_enabled(void)
{
    return !editor_writer_mode && editor_show_row_numbers;
}

static int save_editor_settings(void)
{
    FILE *file = fopen(EDITOR_STATE_FILE, "wt");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "%d %d %d %d\n",
            editor_word_wrap,
            editor_show_row_numbers,
            editor_writer_mode,
            editor_writer_ruler);
    fprintf(file, "%d %d %d",
            editor_writer_left_indent,
            editor_writer_first_indent,
            editor_writer_right_indent);
    {
        int i;
        for (i = 0; i < EDITOR_WRITER_TABS; ++i) {
            fprintf(file, " %d", editor_writer_tabs[i]);
        }
    }
    fprintf(file, "\n");
    fclose(file);
    return 1;
}

static void load_editor_settings(void)
{
    FILE *file = fopen(EDITOR_STATE_FILE, "rt");
    int wrap = 0;
    int rows = 1;
    int writer = 0;
    int ruler = 1;
    int fields;

    if (file == NULL) {
        return;
    }

    fields = fscanf(file, "%d %d %d %d",
                    &wrap, &rows, &writer, &ruler);

    if (fields >= 2) {
        editor_word_wrap = wrap ? 1 : 0;
        editor_show_row_numbers = rows ? 1 : 0;
    }
    if (fields >= 3) {
        editor_writer_mode = writer ? 1 : 0;
    }
    if (fields >= 4) {
        editor_writer_ruler = ruler ? 1 : 0;
    }

    {
        int left_indent;
        int first_indent;
        int right_indent;

        if (fscanf(file, "%d %d %d",
                   &left_indent, &first_indent, &right_indent) == 3) {
            int i;

            editor_writer_left_indent =
                left_indent < 0 ? 0 : left_indent;
            editor_writer_first_indent =
                first_indent < 0 ? 0 : first_indent;
            editor_writer_right_indent =
                right_indent < 0 ? 0 : right_indent;

            for (i = 0; i < EDITOR_WRITER_TABS; ++i) {
                int tab;
                if (fscanf(file, "%d", &tab) == 1) {
                    editor_writer_tabs[i] = tab;
                } else {
                    break;
                }
            }
        }
    }

    fclose(file);
}

static void editor_update_last_save(const char *path)
{
    struct stat info;

    if (path[0] != '\0' && stat(path, &info) == 0) {
        struct tm *saved = localtime(&info.st_mtime);

        if (saved != NULL &&
            strftime(editor_last_save,
                     sizeof(editor_last_save),
                     "%Y-%m-%d %H:%M",
                     saved) > 0) {
            return;
        }
    }

    strcpy(editor_last_save, "Not saved");
}

static int editor_word_count(void)
{
    int line;
    int words = 0;

    for (line = 0; line < editor_line_count; ++line) {
        const unsigned char *p =
            (const unsigned char *)editor_lines[line];
        int in_word = 0;

        while (*p != '\0') {
            if (isspace(*p)) {
                in_word = 0;
            } else if (!in_word) {
                ++words;
                in_word = 1;
            }
            ++p;
        }
    }

    return words;
}

static int text_input_active(void)
{
    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) return 1;
    if (active_window == APP_EXPLORER &&
        explorer_window.open &&
        !explorer_window.minimized &&
        explorer_rename_active) {
        return 1;
    }

    if (active_window == APP_EDITOR &&
        editor_window.open &&
        !editor_window.minimized) {
        return 1;
    }

    return active_window == APP_TERMINAL &&
           terminal_window.open &&
           !terminal_window.minimized;
}

static int explorer_folder_width(void)
{
    if (recycle_bin) return 0;
    int width = explorer_folder_preferred_width;
    if (!width) {
        width = explorer_window.w / 3;
        if (width > 160) width = 160;
    }
    int maximum = explorer_window.w - 40 - GRID_X_STEP;
    if (width > maximum) width = maximum;
    if (width < 96) width = 96;
    return width;
}

static int explorer_folder_y(void)
{
    return explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 20;
}

static int explorer_folder_rows(void)
{
    int rows = (explorer_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 44 - BUDO_SCROLL_WIDTH) / 12;
    return rows > 0 ? rows : 1;
}

static int explorer_client_x(void)
{
    return explorer_window.x + 8 + explorer_folder_width() + 8;
}

static int explorer_client_y(void)
{
    return explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 8 + (explorer_list_view ? 14 : 0);
}

static int explorer_client_w(void)
{
    return explorer_window.w - 40 - explorer_folder_width();
}

static int explorer_client_h(void)
{
    return explorer_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 20 - (explorer_list_view ? 14 : 0);
}

static int explorer_cols(void)
{
    int cols = explorer_list_view ? 1 : explorer_client_w() / GRID_X_STEP;
    return cols > 0 ? cols : 1;
}

static int explorer_rows(void)
{
    int rows = (explorer_client_h() - 12) / (explorer_list_view ? 12 : GRID_Y_STEP);
    return rows > 0 ? rows : 1;
}

static int explorer_slots(void)
{
    return explorer_cols() * explorer_rows();
}

static void slot_position(int slot, int *x, int *y)
{
    int cols = explorer_cols();
    int col = slot % cols;
    int row = slot / cols;

    *x = explorer_client_x() + (explorer_list_view ? 0 : col * GRID_X_STEP + 20);
    *y = explorer_client_y() + row * (explorer_list_view ? 12 : GRID_Y_STEP);
}

static int point_to_slot(int x, int y)
{
    int cols = explorer_cols();
    int rows = explorer_rows();
    int rel_x = x - explorer_client_x();
    int rel_y = y - explorer_client_y();
    int col;
    int row;
    int slot;
    int icon_x;
    int icon_y;

    if (rel_x < 0 || rel_y < 0) {
        return -1;
    }

    if (rel_x >= explorer_client_w()) return -1;
    col = explorer_list_view ? 0 : rel_x / GRID_X_STEP;
    row = rel_y / (explorer_list_view ? 12 : GRID_Y_STEP);

    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return -1;
    }

    slot = row * cols + col;
    slot_position(slot, &icon_x, &icon_y);

    if (!explorer_list_view && (x < icon_x - 20 || x >= icon_x + 52 ||
        y < icon_y - 2 || y >= icon_y + 40)) {
        return -1;
    }

    return slot;
}

static void draw_folder(int x, int y)
{
    fill_rect(x + 2, y, 9, 4, FOLDER_COLOR);
    fill_rect(x, y + 3, ICON_W, ICON_H - 3, FOLDER_DARK);
    fill_rect(x + 1, y + 4, ICON_W - 2, ICON_H - 5, FOLDER_COLOR);
}

static void draw_file(int x, int y)
{
    fill_rect(x + 4, y, 16, 16, FILE_DARK);
    fill_rect(x + 5, y + 1, 14, 14, ui_color_indices[BUDO_SYS_COLOR_FILE_ICON]);
    fill_rect(x + 15, y + 1, 4, 4, DESKTOP_COLOR);
    put_pixel(x + 15, y + 4, FILE_DARK);
    put_pixel(x + 16, y + 4, FILE_DARK);
    put_pixel(x + 17, y + 4, FILE_DARK);
    put_pixel(x + 18, y + 4, FILE_DARK);
}

static void draw_executable(int x, int y)
{
    fill_rect(x + 2, y + 1, 20, 13, FILE_DARK);
    fill_rect(x + 3, y + 2, 18, 11, WINDOW_FRAME_COLOR);
    fill_rect(x + 5, y + 4, 14, 7, WINDOW_FACE_COLOR);
    draw_text(x + 6, y + 4, "/>", WINDOW_FRAME_COLOR, 3);
    fill_rect(x + 8, y + 14, 8, 2, FILE_DARK);
    fill_rect(x + 5, y + 16, 14, 1, FILE_DARK);
}

static void draw_text_file_icon(int x, int y)
{
    draw_file(x, y);
    draw_text(x + 6, y + 5, "T", WINDOW_FRAME_COLOR, 1);
}

static void draw_code_file_icon(int x, int y)
{
    draw_file(x, y);
    draw_text(x + 5, y + 5, "<>", WINDOW_FRAME_COLOR, 2);
}

static void draw_back(int x, int y)
{
    int row;

    for (row = 0; row < 11; ++row) {
        int width = row <= 5 ? row : 10 - row;
        int col;

        for (col = 0; col <= width; ++col) {
            put_pixel(x + 4 + col, y + 5 + row, BACK_COLOR);
        }
    }

    fill_rect(x + 9, y + 8, 13, 6, BACK_COLOR);
}

static void draw_labeled_icon_color(int x, int y, const char *name, int type,
                              int selected, const char *path,
                              unsigned char label_color)
{
    int len = (int)strlen(name);
    int shown = len > 12 ? 12 : len;
    int is_exec = type == TYPE_FILE && executable_path(path);
    int is_code = type == TYPE_FILE && code_file(name);
    int is_text = type == TYPE_FILE && text_file(name);
    int pcx;
    const unsigned char *pixels;
    int label_width = shown * 6 - 1;
    if (len > shown) bwa_pointer_region(x, y, DESKTOP_ICON_W, DESKTOP_ICON_H + 12, BUDO_CURSOR_ARROW, name);
    int label_x;
    budo_selection_icon_draw(&bwa_host_api, x, y, DESKTOP_ICON_W,
                             DESKTOP_ICON_H, selected, 0);

    if (type == TYPE_FOLDER) {
        pcx = folder_pcx_icon_loaded;
        pixels = folder_pcx_icon;
    } else if (is_exec) {
        pcx = exec_pcx_icon_loaded;
        pixels = exec_pcx_icon;
    } else if (is_code) {
        pcx = code_pcx_icon_loaded;
        pixels = code_pcx_icon;
    } else if (is_text) {
        pcx = text_pcx_icon_loaded;
        pixels = text_pcx_icon;
    } else {
        pcx = file_pcx_icon_loaded;
        pixels = file_pcx_icon;
    }

    if (pcx) {
        label_x = x + DESKTOP_ICON_W / 2 - label_width / 2;
        draw_pcx_icon(x, y, pixels);
        draw_text_elided(label_x, y + DESKTOP_ICON_H + 2,
                  name, label_color, shown);
        return;
    }

    label_x = x + ICON_W / 2 - label_width / 2;
    if (type == TYPE_FOLDER) {
        draw_folder(x, y);
    } else if (is_exec) {
        draw_executable(x, y);
    } else if (is_code) {
        draw_code_file_icon(x, y);
    } else if (is_text) {
        draw_text_file_icon(x, y);
    } else {
        draw_file(x, y);
    }

    draw_text_elided(label_x, y + DESKTOP_ICON_H + 2, name, label_color, shown);
}

static void draw_labeled_icon(int x, int y, const char *name, int type,
                              int selected, const char *path)
{
    draw_labeled_icon_color(x, y, name, type, selected, path, TEXT_COLOR);
}

static void draw_back_icon(int x, int y)
{
    int label_width = 4 * 6 - 1;
    int label_x;

    if (back_pcx_icon_loaded) {
        label_x = x + DESKTOP_ICON_W / 2 - label_width / 2;
        draw_pcx_icon(x, y, back_pcx_icon);
        draw_text_elided(label_x, y + DESKTOP_ICON_H + 2,
                  "BACK", TEXT_COLOR, 4);
        return;
    }

    label_x = x + ICON_W / 2 - label_width / 2;
    draw_back(x, y);
    draw_text_elided(label_x, y + DESKTOP_ICON_H + 2, "BACK", TEXT_COLOR, 4);
}

static void draw_explorer_app_icon(int x, int y)
{
    int pcx_center_x = x + DESKTOP_ICON_W / 2;
    int center_x = x + ICON_W / 2;
    int file_width = 4 * 6 - 1;
    int explorer_width = 8 * 6 - 1;

    if (explorer_pcx_icon_loaded) {
        draw_pcx_icon(x, y, explorer_pcx_icon);
        draw_text(pcx_center_x - explorer_width / 2,
                  y + DESKTOP_ICON_H + 3,
                  "Explorer", DESKTOP_TEXT_COLOR, 8);
        return;
    }

    draw_folder(x, y);
    draw_text(center_x - file_width / 2,
              y + DESKTOP_ICON_H + 3, "File", DESKTOP_TEXT_COLOR, 4);
    draw_text(center_x - explorer_width / 2,
              y + DESKTOP_ICON_H + 12, "Explorer", DESKTOP_TEXT_COLOR, 8);
}

static void draw_editor_app_icon(int x, int y)
{
    int pcx_center_x = x + DESKTOP_ICON_W / 2;
    int center_x = x + ICON_W / 2;
    int label_width = 6 * 6 - 1;

    if (editor_pcx_icon_loaded) {
        draw_pcx_icon(x, y, editor_pcx_icon);
        draw_text(pcx_center_x - label_width / 2,
                  y + DESKTOP_ICON_H + 3,
                  "Editor", DESKTOP_TEXT_COLOR, 6);
        return;
    }

    draw_file(x, y);
    draw_text(center_x - label_width / 2,
              y + DESKTOP_ICON_H + 3, "Editor", DESKTOP_TEXT_COLOR, 6);
}

static void draw_window_button(int x, int y, int kind)
{
    const char *tip = kind == 0 ? "Minimize" : kind == 1 ? "Maximize / Restore" : "Close";
    UiRegion region = {.x=x, .y=y, .w=14, .h=14, .owner=ui_draw_owner, .scope=ui_draw_scope};
    snprintf(region.tip, sizeof(region.tip), "%s", tip);
    ui_register(x, y, 14, 14, BUDO_CURSOR_ARROW, tip, 1, 0);
    int hover = point_in_rect(ui_pointer_x, ui_pointer_y, x, y, 14, 14) &&
                ui_region_visible(&region, ui_pointer_x, ui_pointer_y);
    int pressed = hover && ui_captured &&
                  ui_same_control(&region, &ui_capture);
    unsigned char ink = hover ? TITLE_TEXT_COLOR : WINDOW_FRAME_COLOR;
    fill_rect(x, y, 14, 14, pressed ? CONTROL_PRESSED_COLOR :
              hover ? CONTROL_HOVER_COLOR : WINDOW_CHROME_COLOR);
    draw_bevel(x, y, 14, 14, !pressed);

    if (kind == 0) {
        fill_rect(x + 3, y + 9, 8, 2, ink);
    } else if (kind == 1) {
        draw_rect(x + 3, y + 3, 8, 8, ink);
        fill_rect(x + 4, y + 4, 6, 1, ink);
    } else {
        int i;
        for (i = 0; i < 8; ++i) {
            put_pixel(x + 3 + i, y + 3 + i, ink);
            put_pixel(x + 10 - i, y + 3 + i, ink);
        }
    }
}

static void draw_checkbox(int x, int y, int checked)
{
    int hover = point_in_rect(ui_pointer_x, ui_pointer_y, x, y, 10, 10) &&
                ui_draw_scope == ui_scope() &&
                (ui_draw_scope || ui_draw_owner == desktop_point_owner(ui_pointer_x, ui_pointer_y));
    int pressed = ui_captured && hover && ui_region_contains(&ui_capture, x, y);
    unsigned char ink = hover ? TITLE_TEXT_COLOR : WINDOW_FRAME_COLOR;
    fill_rect(x, y, 10, 10, pressed ? CONTROL_PRESSED_COLOR :
              hover ? CONTROL_HOVER_COLOR : FILE_COLOR);
    if (pressed) draw_rect(x + 1, y + 1, 8, 8, WINDOW_SHADOW_COLOR);
    draw_bevel(x, y, 10, 10, 0);

    if (checked) {
        put_pixel(x + 2, y + 5, ink);
        put_pixel(x + 3, y + 6, ink);
        put_pixel(x + 4, y + 7, ink);
        put_pixel(x + 5, y + 6, ink);
        put_pixel(x + 6, y + 5, ink);
        put_pixel(x + 7, y + 4, ink);
        put_pixel(x + 8, y + 3, ink);
    }
}

static void draw_explorer_list_entry(int index, int x, int y)
{
    int width = explorer_client_w();
    int name_w = width >= 360 ? width - 246 : width >= 270 ? width - 180 : width >= 210 ? width - 78 : width;
    int selected = index == -2 ? explorer_up_selected : index >= 0 && explorer_selection[index];
    unsigned char ink = selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR;
    fill_rect(x, y, width, 12, selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_BG] : WINDOW_FACE_COLOR);
    const char *name = index == -2 ? ".. (Up)" : directory_items[index].name;
    draw_text_elided(x + 3, y + 2, name, ink, (name_w - 6) / 6);
    bwa_pointer_region(x, y, name_w, 12, BUDO_CURSOR_ARROW, name);
    if (index >= 0) {
        struct stat info;
        char size[32] = "?", date[24] = "?", attributes[6] = "----";
        if (lstat(directory_items[index].path, &info) == 0) {
            if (directory_items[index].type == TYPE_FOLDER) snprintf(size, sizeof(size), "<DIR>");
            else snprintf(size, sizeof(size), "%lld", (long long)info.st_size);
            struct tm when;
            if (localtime_r(&info.st_mtime, &when)) strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &when);
            attributes[0] = info.st_mode & 0222 ? '-' : 'R';
            attributes[1] = directory_items[index].name[0] == '.' ? 'H' : '-';
            attributes[2] = S_ISLNK(info.st_mode) ? 'L' : '-';
            attributes[3] = directory_items[index].type == TYPE_FOLDER ? 'D' : 'A';
        }
        if (width - name_w >= 78) draw_text(x + name_w, y + 2, size, ink, 12);
        if (width - name_w >= 180) draw_text(x + name_w + 78, y + 2, date, ink, 16);
        if (width - name_w >= 246) draw_text(x + name_w + 180, y + 2, attributes, ink, 4);
        if (index == explorer_selected_item) draw_rect(x, y, width, 12, ink);
    }
}

static void draw_file_explorer_window(int page)
{
    int title_y = explorer_window.y;
    int min_x = window_min_button_x(&explorer_window);
    int max_x = window_max_button_x(&explorer_window);
    int close_x = window_close_button_x(&explorer_window);
    int filter_x = explorer_window.x + 110;
    int filter_y = title_y + 6;
    int slot;
    int slots;

    if (!explorer_window.open || explorer_window.minimized) {
        return;
    }

    draw_window_chrome(&explorer_window);
    draw_text(explorer_window.x + 7, title_y + 6,
              recycle_bin ? "Recycle Bin" : "File Explorer", window_title_text_color(), 13);

    if (!recycle_bin) {
        ui_register(filter_x, filter_y, 112, 12, BUDO_CURSOR_ARROW, "Only Executables", 1, 0);
        draw_checkbox(filter_x, filter_y, only_executables);
        draw_text(filter_x + 14, title_y + 6,
                  "Only Executables", window_title_text_color(), 16);
    }

    draw_window_button(min_x, title_y + WINDOW_BORDER, 0);
    draw_window_button(max_x, title_y + WINDOW_BORDER, 1);
    draw_window_button(close_x, title_y + WINDOW_BORDER, 2);

    explorer_folder_draw();
    slots = explorer_slots();
    if (explorer_list_view) {
        int x = explorer_client_x(), y = explorer_client_y() - 12;
        int width = explorer_client_w();
        int name_w = width >= 360 ? width - 246 : width >= 270 ? width - 180 : width >= 210 ? width - 78 : width;
        draw_text(x + 3, y, "Name", TEXT_COLOR, 4);
        if (width - name_w >= 78) draw_text(x + name_w, y, "Bytes", TEXT_COLOR, 5);
        if (width - name_w >= 180) draw_text(x + name_w + 78, y, "Modified", TEXT_COLOR, 8);
        if (width - name_w >= 246) draw_text(x + name_w + 180, y, "Attr", TEXT_COLOR, 4);
    }

    for (slot = 0; slot < slots; ++slot) {
        int item_index = item_for_slot(page, slot);
        int x;
        int y;

        slot_position(slot, &x, &y);

        if (explorer_list_view && item_index != -1) {
            draw_explorer_list_entry(item_index, x, y);
        } else if (item_index == -2) {
            budo_selection_icon_draw(&bwa_host_api, x, y, DESKTOP_ICON_W, DESKTOP_ICON_H, explorer_up_selected, 0);
            draw_back_icon(x, y);
        } else if (item_index >= 0) {
            draw_labeled_icon(x, y,
                              directory_items[item_index].name,
                              directory_items[item_index].type,
                              explorer_selection[item_index] != 0, directory_items[item_index].path);
            budo_selection_icon_draw(&bwa_host_api, x, y, DESKTOP_ICON_W,
                                     DESKTOP_ICON_H, 0,
                                     item_index == explorer_selected_item);

        }
    }

    budo_selection_drag_draw(&bwa_host_api, &explorer_select);

    {
        const char *shortcuts =
            "Ctrl+A All  Ctrl+C Copy  Ctrl+X Cut  Ctrl+V Paste  "
            "Ctrl+E Edit  Ctrl+R Rename  Del Delete";
        int status_y = explorer_window.y + explorer_window.h - 15;
        int max_chars = (explorer_window.w - 18) / 6;

        fill_rect(explorer_window.x + WINDOW_BORDER,
                  status_y - 1,
                  explorer_window.w - WINDOW_BORDER * 2,
                  13, WINDOW_CHROME_COLOR);
        fill_rect(explorer_window.x + WINDOW_BORDER,
                  status_y - 2,
                  explorer_window.w - WINDOW_BORDER * 2,
                  1, WINDOW_SHADOW_COLOR);
        draw_text(explorer_window.x + 7, status_y + 2,
                  explorer_status ? explorer_status : recycle_bin ? "Select entries to Restore; Empty deletes permanently." : shortcuts, TEXT_COLOR, max_chars);
    }

    desktop_scroll_draw(APP_EXPLORER, page);
    desktop_file_menu_draw(APP_EXPLORER);
    if (explorer_rename_active) {
        int dialog_w =
            explorer_window.w > 360 ? 320 : explorer_window.w - 30;
        int dialog_x =
            explorer_window.x + (explorer_window.w - dialog_w) / 2;
        int dialog_y =
            explorer_window.y + (explorer_window.h - 66) / 2;

        fill_rect(dialog_x, dialog_y, dialog_w, 66,
                  WINDOW_CHROME_COLOR);
        draw_rect(dialog_x, dialog_y, dialog_w, 66, TEXT_COLOR);
        draw_bevel(dialog_x + 1, dialog_y + 1,
                   dialog_w - 2, 64, 1);
        fill_rect(dialog_x + 4, dialog_y + 4,
                  dialog_w - 8, 14, TITLE_COLOR);
        draw_text(dialog_x + 8, dialog_y + 7,
                  explorer_creating_folder ? "New Folder" : "Rename", TITLE_TEXT_COLOR, 10);
        draw_text(dialog_x + 8, dialog_y + 24,
                  "New name:", TEXT_COLOR, 9);
        fill_rect(dialog_x + 8, dialog_y + 32,
                  dialog_w - 16, 15, WINDOW_FACE_COLOR);
        draw_bevel(dialog_x + 8, dialog_y + 32,
                   dialog_w - 16, 15, 0);
        draw_text(dialog_x + 12, dialog_y + 35,
                  explorer_rename_input, TEXT_COLOR,
                  (dialog_w - 24) / 6);
        draw_text(dialog_x + 8, dialog_y + 52,
                  "Enter=OK  Esc=Cancel",
                  TEXT_COLOR, 24);
    }

    fill_rect(explorer_window.x + explorer_window.w - 9,
              explorer_window.y + explorer_window.h - 9,
              7, 2, TEXT_COLOR);
    fill_rect(explorer_window.x + explorer_window.w - 5,
              explorer_window.y + explorer_window.h - 13,
              3, 2, TEXT_COLOR);
}

static void editor_set_status(const char *text)
{
    size_t len = strlen(text);

    if (len >= sizeof(editor_status)) {
        len = sizeof(editor_status) - 1;
    }

    memcpy(editor_status, text, len);
    editor_status[len] = '\0';
}

static void editor_reset_document(void)
{
    editor_state->v_editor_end_affinity = 0;
    editor_selection_clear();
    editor_match_line = -1;
    editor_history_reset();
    memset(editor_lines, 0, sizeof(editor_lines));
    editor_line_count = 1;
    editor_cursor_line = 0;
    editor_cursor_col = 0;
    editor_top_line = 0;
    editor_top_segment = 0;
    editor_left_col = 0;
    editor_path[0] = '\0';
    strcpy(editor_last_save, "Not saved");
    editor_mark_saved();
    editor_set_status("New document");
}

static int editor_path_is_rtf(const char *path)
{
    const char *dot = strrchr(path, '.');

    return dot != NULL && compare_names_ci(dot, ".RTF") == 0;
}

static void editor_rtf_write_text(FILE *file, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    while (*p != '\0') {
        if (*p == '\\' || *p == '{' || *p == '}') {
            fputc('\\', file);
            fputc(*p, file);
        } else if (*p < 32 || *p >= 127) {
            fprintf(file, "\\'%02x", (unsigned int)*p);
        } else {
            fputc(*p, file);
        }
        ++p;
    }
}

static int editor_save_rtf(const char *path)
{
    FILE *file = fopen(path, "wt");
    int i;
    int left = editor_writer_left_indent * 120;
    int first = editor_writer_first_indent * 120;
    int right = editor_writer_right_indent * 120;

    if (file == NULL) {
        editor_set_status("Save failed");
        return 0;
    }

    fputs("{\\rtf1\\ansi\\ansicpg850\\deff0", file);
    fputs("{\\fonttbl{\\f0\\fnil Courier;}}", file);
    fputs("\\viewkind4\\uc1\\f0\\fs20\n", file);

    for (i = 0; i < editor_line_count; ++i) {
        int tab;

        fprintf(file, "\\pard\\li%d\\ri%d\\fi%d",
                left, right, first);
        for (tab = 0; tab < EDITOR_WRITER_TABS; ++tab) {
            if (editor_writer_tabs[tab] > 0) {
                fprintf(file, "\\tx%d", editor_writer_tabs[tab] * 120);
            }
        }
        fputc(' ', file);
        editor_rtf_write_text(file, editor_lines[i]);
        if (i + 1 < editor_line_count) {
            fputs("\\par\n", file);
        }
    }

    fputs("}\n", file);
    fclose(file);

    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    editor_writer_mode = 1;
    editor_update_last_save(editor_path);
    editor_mark_saved();
    editor_set_status("RTF saved");
    return 1;
}

static int editor_rtf_hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void editor_rtf_append_char(unsigned char ch)
{
    if (!ch) { editor_rtf_overflow = 1; return; }
    char *line = editor_lines[editor_line_count - 1];
    int len = (int)strlen(line);

    if (len < EDITOR_MAX_COLS - 1) {
        line[len] = (char)ch;
        line[len + 1] = '\0';
    } else editor_rtf_overflow = 1;
}

static void editor_rtf_new_paragraph(void)
{
    if (editor_line_count < EDITOR_MAX_LINES) {
        ++editor_line_count;
        editor_lines[editor_line_count - 1][0] = '\0';
    } else editor_rtf_overflow = 1;
}

static int editor_parse_rtf(const char *path)
{
    FILE *file = fopen(path, "rt");
    int ch;
    int group_depth = 0;
    int skip_group = 0;
    int first_layout = 1;
    int tabs_loaded = 0;

    if (file == NULL) {
        editor_set_status("Open failed");
        return 0;
    }

    editor_rtf_overflow = 0;
    memset(editor_lines, 0, sizeof(editor_lines));
    editor_line_count = 1;
    editor_writer_left_indent = 0;
    editor_writer_first_indent = 0;
    editor_writer_right_indent = 0;
    memset(editor_writer_tabs, 0, sizeof(editor_writer_tabs));

    while ((ch = fgetc(file)) != EOF) {
        if (ch == '{') {
            ++group_depth;
            continue;
        }

        if (ch == '}') {
            if (group_depth > 0) --group_depth;
            if (skip_group > group_depth) skip_group = 0;
            continue;
        }

        if (skip_group != 0) {
            continue;
        }

        if (ch == '\\') {
            int next = fgetc(file);

            if (next == EOF) break;

            if (next == '\\' || next == '{' || next == '}') {
                editor_rtf_append_char((unsigned char)next);
                continue;
            }

            if (next == '\'') {
                int h1 = fgetc(file);
                int h2 = fgetc(file);
                int v1 = editor_rtf_hex_value(h1);
                int v2 = editor_rtf_hex_value(h2);

                if (v1 >= 0 && v2 >= 0) {
                    editor_rtf_append_char(
                        (unsigned char)((v1 << 4) | v2));
                }
                continue;
            }

            if (next == '*') {
                skip_group = group_depth;
                continue;
            }

            if (isalpha((unsigned char)next)) {
                char word[24];
                int wi = 0;
                int sign = 1;
                int value = 0;
                int have_value = 0;
                int term;

                word[wi++] = (char)next;
                while ((term = fgetc(file)) != EOF &&
                       isalpha((unsigned char)term)) {
                    if (wi < (int)sizeof(word) - 1) {
                        word[wi++] = (char)term;
                    }
                }
                word[wi] = '\0';

                if (term == '-') {
                    sign = -1;
                    term = fgetc(file);
                }
                while (term != EOF && isdigit((unsigned char)term)) {
                    have_value = 1;
                    value = value * 10 + term - '0';
                    term = fgetc(file);
                }
                value *= sign;

                if (strcmp(word, "par") == 0) {
                    editor_rtf_new_paragraph();
                } else if (strcmp(word, "li") == 0 && have_value &&
                           first_layout) {
                    editor_writer_left_indent =
                        value > 0 ? value / 120 : 0;
                } else if (strcmp(word, "fi") == 0 && have_value &&
                           first_layout) {
                    editor_writer_first_indent =
                        value > 0 ? value / 120 : 0;
                } else if (strcmp(word, "ri") == 0 && have_value &&
                           first_layout) {
                    editor_writer_right_indent =
                        value > 0 ? value / 120 : 0;
                } else if (strcmp(word, "tx") == 0 && have_value &&
                           tabs_loaded < EDITOR_WRITER_TABS) {
                    editor_writer_tabs[tabs_loaded++] =
                        value > 0 ? value / 120 : 0;
                } else if (strcmp(word, "fonttbl") == 0 ||
                           strcmp(word, "colortbl") == 0 ||
                           strcmp(word, "stylesheet") == 0 ||
                           strcmp(word, "info") == 0) {
                    skip_group = group_depth;
                }

                if (strcmp(word, "par") == 0) {
                    first_layout = 0;
                }

                if (term != EOF && term != ' ') {
                    ungetc(term, file);
                }
                continue;
            }

            continue;
        }

        if (ch == '\r' || ch == '\n') {
            continue;
        }

        editor_rtf_append_char((unsigned char)ch);
    }

    int ok = !ferror(file) && !editor_rtf_overflow && group_depth == 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) return 0;

    while (editor_line_count > 1 &&
           editor_lines[editor_line_count - 1][0] == '\0') {
        --editor_line_count;
    }

    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    editor_writer_mode = 1;
    editor_word_wrap = 1;
    editor_cursor_line = 0;
    editor_cursor_col = 0;
    editor_top_line = 0;
    editor_top_segment = 0;
    editor_left_col = 0;
    editor_preferred_visual_col = -1;
    editor_update_last_save(editor_path);
    editor_mark_saved();
    editor_set_status("RTF opened");
    return 1;
}

static int editor_load_rtf(const char *path)
{
    FILE *file = fopen(path, "rb");
    char signature[6] = {0};
    if (!file) { editor_set_status("Open failed; current document preserved"); return 0; }
    size_t size = fread(signature, 1, 5, file);
    fclose(file);
    if (size != 5 || memcmp(signature, "{\\rtf", 5) != 0) {
        editor_set_status("Invalid RTF; current document preserved");
        return 0;
    }
    char (*previous)[EDITOR_MAX_COLS] = malloc(sizeof(editor_lines));
    if (!previous) { editor_set_status("Not enough memory to open RTF"); return 0; }
    memcpy(previous, editor_lines, sizeof(editor_lines));
    int old_count = editor_line_count;
    int old_left = editor_writer_left_indent;
    int old_first = editor_writer_first_indent;
    int old_right = editor_writer_right_indent;
    int old_tabs[EDITOR_WRITER_TABS];
    memcpy(old_tabs, editor_writer_tabs, sizeof(old_tabs));
    int ok = editor_parse_rtf(path);
    if (!ok) {
        memcpy(editor_lines, previous, sizeof(editor_lines));
        editor_line_count = old_count;
        editor_writer_left_indent = old_left;
        editor_writer_first_indent = old_first;
        editor_writer_right_indent = old_right;
        memcpy(editor_writer_tabs, old_tabs, sizeof(old_tabs));
        editor_set_status("Invalid or oversized RTF; document preserved");
    } else editor_history_reset();
    free(previous);
    return ok;
}

static unsigned char editor_cp850_to_latin1(unsigned char ch)
{
    switch (ch) {
        case 132: return 228; /* ä */
        case 134: return 229; /* å */
        case 142: return 196; /* Ä */
        case 143: return 197; /* Å */
        case 145: return 230; /* æ */
        case 146: return 198; /* Æ */
        case 148: return 246; /* ö */
        case 153: return 214; /* Ö */
        case 155: return 248; /* ø */
        case 156: return 163; /* £ */
        case 157: return 216; /* Ø */
        case 171: return 189; /* 1/2 */
        case 245: return 167; /* § */
        default: return ch;
    }
}

static void editor_ps_write_string(FILE *file, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    fputc('(', file);
    while (*p != '\0') {
        unsigned char out = editor_cp850_to_latin1(*p);

        if (out == '(' || out == ')' || out == '\\') {
            fputc('\\', file);
            fputc(out, file);
        } else if (out >= 32 && out < 127) {
            fputc(out, file);
        } else {
            fprintf(file, "\\%03o", (unsigned int)out);
        }
        ++p;
    }
    fputc(')', file);
}

static int editor_export_postscript(const char *path)
{
    FILE *file = fopen(path, "wt");
    int line;
    int page = 1;
    int y = 790;
    const int line_height = 12;
    const int left_margin = 54;
    const int right_margin = 54;
    const int page_width = 595;
    const int usable_chars =
        (page_width - left_margin - right_margin) / 6;

    if (file == NULL) {
        editor_set_status("Export failed");
        return 0;
    }

    fputs("%!PS-Adobe-3.0\n", file);
    fputs("%%Creator: BUDOWIN Writer\n", file);
    fputs("%%Pages: (atend)\n", file);
    fputs("%%BoundingBox: 0 0 595 842\n", file);
    fputs("%%EndComments\n", file);
    fputs("/Courier findfont dup length dict begin\n", file);
    fputs("{1 index /FID ne {def} {pop pop} ifelse} forall\n", file);
    fputs("/Encoding ISOLatin1Encoding def\n", file);
    fputs("currentdict end /CourierISO exch definefont pop\n", file);
    fputs("/CourierISO findfont 10 scalefont setfont\n", file);
    fprintf(file, "%%%%Page: %d %d\n", page, page);

    for (line = 0; line < editor_line_count; ++line) {
        const char *text = editor_lines[line];
        int len = (int)strlen(text);
        int start = 0;
        int segment = 0;

        if (len == 0) {
            y -= line_height;
        }

        while (start < len) {
            int draw_start;
            int draw_len;
            int next_start;
            int indent_cols = editor_writer_segment_indent(segment);
            int cols = usable_chars - indent_cols -
                       editor_writer_right_indent;

            if (cols < 8) cols = 8;
            editor_wrap_segment(text, start, cols,
                                &draw_start, &draw_len, &next_start);

            if (y < 54) {
                fputs("showpage\n", file);
                ++page;
                fprintf(file, "%%%%Page: %d %d\n", page, page);
                fputs("/CourierISO findfont 10 scalefont setfont\n", file);
                y = 790;
            }

            fprintf(file, "%d %d moveto ",
                    left_margin + indent_cols * 6, y);
            {
                char temp[EDITOR_MAX_COLS];
                int copy_len = draw_len;

                if (copy_len >= EDITOR_MAX_COLS) {
                    copy_len = EDITOR_MAX_COLS - 1;
                }
                memcpy(temp, text + draw_start, (size_t)copy_len);
                temp[copy_len] = '\0';
                editor_ps_write_string(file, temp);
            }
            fputs(" show\n", file);

            y -= line_height;
            ++segment;
            if (next_start <= start) break;
            start = next_start;
        }
    }

    fputs("showpage\n", file);
    fputs("%%Trailer\n", file);
    fprintf(file, "%%%%Pages: %d\n", page);
    fputs("%%EOF\n", file);
    fclose(file);

    editor_set_status("PostScript exported");
    return 1;
}

static int editor_load_file_impl(const char *path);
static int editor_load_file(const char *path)
{
    editor_state->v_editor_end_affinity = 0;
    editor_selection_clear();
    int previous = ui_busy_begin();
    int result = editor_load_file_impl(path);
    if (result && editor_read_only) {
        editor_writer_mode = 1;
        editor_writer_ruler = 0;
        editor_writer_left_indent = editor_writer_first_indent = editor_writer_right_indent = 0;
        editor_ensure_cursor_visible();
    }
    ui_busy_end(previous);
    return result;
}

static int editor_load_file_impl(const char *path)
{
    FILE *file;
    char (*loaded)[EDITOR_MAX_COLS];
    char buffer[EDITOR_MAX_COLS * 2 + 2];
    int count = 0;
    int ok = 1;
    if (editor_path_is_rtf(path)) return editor_load_rtf(path);
    file = fopen(path, "rt");
    if (!file) { editor_set_status("Open failed; current document preserved"); return 0; }
    loaded = calloc(EDITOR_MAX_LINES, sizeof(*loaded));
    if (!loaded) { fclose(file); editor_set_status("Not enough memory to open file"); return 0; }
    for (;;) {
        /* A sentinel lets us distinguish embedded NUL bytes from the
         * terminator written by fgets, while keeping reads bounded. */
        memset(buffer, 0xff, sizeof(buffer));
        if (!fgets(buffer, sizeof(buffer), file)) break;
        size_t len = sizeof(buffer);
        while (len && (unsigned char)buffer[len - 1] == 0xff) --len;
        if (!len) { ok = 0; break; }
        --len;
        if (memchr(buffer, '\0', len)) { ok = 0; break; }
        while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) buffer[--len] = '\0';
        bw_text_decode(buffer, buffer);
        len = strlen(buffer);
        if (len >= EDITOR_MAX_COLS || count >= EDITOR_MAX_LINES) { ok = 0; break; }
        memcpy(loaded[count++], buffer, len + 1);
    }
    if (ferror(file)) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) {
        free(loaded);
        editor_set_status("Unsupported, oversized or unreadable file; document preserved");
        return 0;
    }
    memcpy(editor_lines, loaded, sizeof(editor_lines));
    free(loaded);
    editor_history_reset();
    editor_line_count = count ? count : 1;
    editor_cursor_line = editor_cursor_col = editor_top_line = editor_top_segment = editor_left_col = 0;
    snprintf(editor_path, sizeof(editor_path), "%s", path);
    editor_writer_mode = 0;
    editor_update_last_save(editor_path);
    editor_mark_saved();
    editor_set_status("File opened");
    return 1;
}

static int editor_save_file_impl(const char *path);
static int editor_save_file(const char *path)
{
    if (editor_read_only) return 0;
    int previous = ui_busy_begin();
    int result = editor_save_file_impl(path);
    ui_busy_end(previous);
    return result;
}

static int editor_save_file_impl(const char *path)
{
    FILE *file;
    char temporary[MAX_PATH];
    int i;

    if (editor_path_is_rtf(path)) {
        return editor_save_rtf(path);
    }

    file = desktop_file_open(path, temporary);

    if (file == NULL) {
        editor_set_status("Save failed");
        return 0;
    }

    for (i = 0; i < editor_line_count; ++i) {
        char encoded[EDITOR_MAX_COLS * 2];
        if (!bw_text_encode(encoded, sizeof(encoded), editor_lines[i])) {
            fclose(file);
            (void)remove(temporary);
            editor_set_status("Unable to encode text");
            return 0;
        }
        fputs(encoded, file);
        if (i + 1 < editor_line_count) {
            fputc('\n', file);
        }
    }

    if (!desktop_file_finish(file, path, temporary)) {
        editor_set_status("Write failed; original file preserved");
        return 0;
    }
    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    editor_update_last_save(editor_path);
    editor_mark_saved();
    editor_set_status("File saved");
    return 1;
}

static void editor_history_clear(struct EditorSnapshot *history, int *count)
{
    while (*count > 0) free(history[--*count].text);
}

static void editor_history_reset(void)
{
    editor_history_clear(editor_undo, &editor_undo_count);
    editor_history_clear(editor_redo, &editor_redo_count);
    editor_typing_line = -1;
    editor_match_line = -1;
}

static int editor_history_capture(struct EditorSnapshot *history, int *count)
{
    struct EditorSnapshot snapshot;
    size_t size = 0;
    size_t offset = 0;
    int i;
    for (i = 0; i < editor_line_count; ++i)
        size += strlen(editor_lines[i]) + 1;
    snapshot.text = malloc(size);
    if (snapshot.text == NULL) {
        editor_set_status("Not enough memory for undo; edit cancelled");
        return 0;
    }
    for (i = 0; i < editor_line_count; ++i) {
        size_t len = strlen(editor_lines[i]) + 1;
        memcpy(snapshot.text + offset, editor_lines[i], len);
        offset += len;
    }
    snapshot.lines = editor_line_count;
    snapshot.row = editor_cursor_line;
    snapshot.col = editor_cursor_col;
    if (*count == EDITOR_HISTORY_LIMIT) {
        free(history[0].text);
        memmove(history, history + 1,
                (EDITOR_HISTORY_LIMIT - 1) * sizeof(*history));
        --*count;
    }
    history[(*count)++] = snapshot;
    return 1;
}

static int editor_history_before_edit(void)
{
    if (editor_read_only) return 0;
    editor_typing_line = -1;
    if (!editor_history_capture(editor_undo, &editor_undo_count)) return 0;
    editor_history_clear(editor_redo, &editor_redo_count);
    editor_match_line = -1;
    return 1;
}

static void editor_history_restore(int redo)
{
    if (editor_read_only) return;
    editor_selection_clear();
    editor_typing_line = -1;
    struct EditorSnapshot *source = redo ? editor_redo : editor_undo;
    struct EditorSnapshot *dest = redo ? editor_undo : editor_redo;
    int *source_count = redo ? &editor_redo_count : &editor_undo_count;
    int *dest_count = redo ? &editor_undo_count : &editor_redo_count;
    struct EditorSnapshot snapshot;
    size_t offset = 0;
    int i;
    if (*source_count == 0) {
        editor_set_status(redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    if (!editor_history_capture(dest, dest_count)) return;
    snapshot = source[--*source_count];
    memset(editor_lines, 0, sizeof(editor_lines));
    for (i = 0; i < snapshot.lines; ++i) {
        size_t len = strlen(snapshot.text + offset) + 1;
        memcpy(editor_lines[i], snapshot.text + offset, len);
        offset += len;
    }
    editor_line_count = snapshot.lines;
    editor_cursor_line = snapshot.row;
    editor_cursor_col = snapshot.col;
    free(snapshot.text);
    editor_match_line = -1;
    editor_ensure_cursor_visible();
    editor_set_status(redo ? "Redone" : "Undone");
}

static int editor_word_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

static int editor_match_at(const char *line, int col, const char *needle)
{
    int i;
    int len = (int)strlen(needle);
    int line_len = (int)strlen(line);
    if (!len || col < 0 || col + len > line_len) return 0;
    for (i = 0; i < len; ++i) {
        unsigned char a = (unsigned char)line[col + i];
        unsigned char b = (unsigned char)needle[i];
        if (editor_search_case ? a != b : tolower(a) != tolower(b))
            return 0;
    }
    return !editor_search_word ||
           ((col == 0 || !editor_word_char((unsigned char)line[col - 1])) &&
            !editor_word_char((unsigned char)line[col + len]));
}

static int editor_find_direction(const char *needle, int start_line,
                                 int start_col, int direction)
{
    int pass;
    int line;
    char status[64];
    editor_match_line = -1;
    if (!needle[0]) {
        editor_set_status("Enter search text");
        return 0;
    }
    for (pass = 0; pass < (editor_search_wrap ? 2 : 1); ++pass) {
        for (line = pass ? (direction > 0 ? 0 : editor_line_count - 1) : start_line;
             line >= 0 && line < editor_line_count; line += direction) {
            int col;
            int len = (int)strlen(editor_lines[line]);
            int begin = !pass && line == start_line ? start_col :
                        (direction > 0 ? 0 : len);
            for (col = begin; col >= 0 && col <= len; col += direction) {
                if (pass && (direction > 0 ?
                    (line > start_line || (line == start_line && col >= start_col)) :
                    (line < start_line || (line == start_line && col <= start_col)))) break;
                if (editor_match_at(editor_lines[line], col, needle)) {
                    editor_cursor_line = line;
                    editor_cursor_col = col;
                    editor_match_line = line;
                    editor_match_col = col;
                    editor_match_len = (int)strlen(needle);
                    editor_ensure_cursor_visible();
                    snprintf(status, sizeof(status), "%sLine %d, column %d",
                             pass ? "Wrapped: " : "Found: ", line + 1, col + 1);
                    editor_set_status(status);
                    return 1;
                }
            }
        }
    }
    editor_set_status("No matches");
    return 0;
}

static int editor_find_from(const char *needle, int start_line, int start_col)
{
    return editor_find_direction(needle, start_line, start_col, 1);
}

static int editor_replace_at_cursor(const char *find_text,
                                    const char *replace_text)
{
    char *line = editor_lines[editor_cursor_line];
    int find_len = (int)strlen(find_text);
    int replace_len = (int)strlen(replace_text);
    int line_len = (int)strlen(line);

    if (find_len == 0 ||
        editor_cursor_col + find_len > line_len ||
        !editor_match_at(line, editor_cursor_col, find_text)) {
        return 0;
    }

    if (line_len - find_len + replace_len >= EDITOR_MAX_COLS) {
        editor_set_status("Replacement too long");
        return 0;
    }

    if (!editor_history_before_edit()) return 0;
    memmove(line + editor_cursor_col + replace_len,
            line + editor_cursor_col + find_len,
            (size_t)(line_len - editor_cursor_col - find_len + 1));
    memcpy(line + editor_cursor_col,
           replace_text, (size_t)replace_len);
    editor_cursor_col += replace_len;
    editor_match_line = -1;
    editor_ensure_cursor_visible();
    editor_set_status("Replaced");
    return 1;
}

static int editor_replace_all(const char *find_text,
                              const char *replace_text)
{
    int line;
    int count = 0;
    int find_len = (int)strlen(find_text);
    int replace_len = (int)strlen(replace_text);

    if (find_len == 0) {
        return 0;
    }

    /* Validate every resulting line before changing the document. */
    for (line = 0; line < editor_line_count; ++line) {
        int pos;
        int matches = 0;
        int len = (int)strlen(editor_lines[line]);
        for (pos = 0; pos < len;) {
            if (editor_match_at(editor_lines[line], pos, find_text)) {
                ++matches;
                pos += find_len;
            } else {
                ++pos;
            }
        }
        if (len + matches * (replace_len - find_len) >= EDITOR_MAX_COLS) {
            editor_set_status("Replacement too long; nothing changed");
            return 0;
        }
    }

    {
        int matches = 0;
        for (line = 0; line < editor_line_count; ++line) {
            int pos;
            for (pos = 0; editor_lines[line][pos];) {
                if (editor_match_at(editor_lines[line], pos, find_text)) {
                    ++matches;
                    pos += find_len;
                } else {
                    ++pos;
                }
            }
        }
        if (!matches) {
            editor_set_status("No matches");
            return 0;
        }
        if (!editor_history_before_edit()) return 0;
    }

    for (line = 0; line < editor_line_count; ++line) {
        char *text = editor_lines[line];
        int pos = 0;

        while (text[pos] != '\0') {
            int line_len = (int)strlen(text);
            while (pos < line_len && !editor_match_at(text, pos, find_text))
                ++pos;
            if (pos >= line_len) break;

            if (line_len - find_len + replace_len >= EDITOR_MAX_COLS) {
                break;
            }

            memmove(text + pos + replace_len,
                    text + pos + find_len,
                    (size_t)(line_len - pos - find_len + 1));
            memcpy(text + pos, replace_text, (size_t)replace_len);
            pos += replace_len;
            ++count;
        }
    }

    if (count > 0) {
        char message[64];
        snprintf(message, sizeof(message), "Replaced %d occurrence%s",
                 count, count == 1 ? "" : "s");
        editor_set_status(message);
    } else {
        editor_set_status("No matches");
    }

    editor_match_line = -1;
    if (editor_cursor_col > (int)strlen(editor_lines[editor_cursor_line]))
        editor_cursor_col = (int)strlen(editor_lines[editor_cursor_line]);
    editor_ensure_cursor_visible();
    return count;
}

static void editor_begin_dialog(int dialog, const char *initial)
{
    if (editor_read_only && dialog != EDITOR_DIALOG_FIND) return;
    size_t len = strlen(initial);

    if (len >= sizeof(editor_dialog_input)) {
        len = sizeof(editor_dialog_input) - 1;
    }

    editor_dialog = dialog;
    editor_menu = EDITOR_MENU_NONE;
    if (editor_search_dialog_active()) {
        if (editor_window.h < 220) editor_window.h = 220;
        if (editor_window.y + editor_window.h > SCREEN_HEIGHT)
            editor_window.y = SCREEN_HEIGHT - editor_window.h;
        editor_search_field = 0;
        editor_search_caret = (int)strlen(editor_search_text);
        editor_search_selected = 1;
        return;
    }
    memcpy(editor_dialog_input, initial, len);
    editor_dialog_input[len] = '\0';
    editor_dialog_len = (int)len;
    editor_menu = EDITOR_MENU_NONE;
}

static void editor_delete_line(void)
{
    if (!editor_history_before_edit()) return;
    editor_match_line = -1;
    if (editor_line_count <= 1) {
        editor_lines[0][0] = '\0';
        editor_cursor_line = 0;
        editor_cursor_col = 0;
        return;
    }

    memmove(&editor_lines[editor_cursor_line],
            &editor_lines[editor_cursor_line + 1],
            (size_t)(editor_line_count - editor_cursor_line - 1) *
            sizeof(editor_lines[0]));
    --editor_line_count;
    editor_lines[editor_line_count][0] = '\0';

    if (editor_cursor_line >= editor_line_count) {
        editor_cursor_line = editor_line_count - 1;
    }

    if (editor_cursor_col >
        (int)strlen(editor_lines[editor_cursor_line])) {
        editor_cursor_col =
            (int)strlen(editor_lines[editor_cursor_line]);
    }

    editor_ensure_cursor_visible();
    editor_set_status("Line deleted");
}

static int window_contains(const AppWindow *window, int x, int y)
{
    return window->open &&
           !window->minimized &&
           point_in_rect(x, y, window->x, window->y, window->w, window->h);
}

static int editor_visible_rows(void)
{
    int ruler_h =
        editor_writer_mode && editor_writer_ruler ? EDITOR_RULER_H : 0;
    int rows =
        (editor_window.h - WINDOW_TITLE_H - EDITOR_MENU_H -
         EDITOR_STATUS_H - ruler_h - 10 - (editor_wrap_enabled() ? 0 : 16)) / 9;
    return rows > 0 ? rows : 1;
}

static int editor_text_x(void)
{
    if (editor_writer_mode) {
        int available = editor_window.w - 32;
        int page_width = available < EDITOR_WRITER_PAGE_W ?
                         available : EDITOR_WRITER_PAGE_W;
        return editor_window.x + 8 + (available - page_width) / 2;
    }

    return editor_window.x + (editor_rows_enabled() ? 40 : 8);
}

static int editor_visible_cols(void)
{
    int right_padding = 24;
    int width = editor_window.x + editor_window.w -
                right_padding - editor_text_x();
    int cols = width / 6;

    return cols > 0 ? cols : 1;
}

static int editor_writer_segment_indent(int segment)
{
    int indent = editor_writer_left_indent;

    if (segment == 0) {
        indent += editor_writer_first_indent;
    }

    return indent;
}

static int editor_writer_segment_cols(int segment)
{
    int cols = editor_visible_cols();

    if (editor_writer_mode) {
        if (cols > EDITOR_WRITER_PAGE_COLS) {
            cols = EDITOR_WRITER_PAGE_COLS;
        }
        cols -= editor_writer_segment_indent(segment);
        cols -= editor_writer_right_indent;
    }

    if (cols < 8) cols = 8;
    return cols;
}

static int editor_writer_next_tab(int visual_col)
{
    int i;
    int best = -1;

    for (i = 0; i < EDITOR_WRITER_TABS; ++i) {
        int tab = editor_writer_tabs[i];

        if (tab > visual_col && (best < 0 || tab < best)) {
            best = tab;
        }
    }

    if (best < 0) {
        best = ((visual_col / 8) + 1) * 8;
    }

    return best;
}

static void editor_writer_toggle_tab(int col)
{
    int i;
    int empty = -1;

    if (col < 1) return;

    for (i = 0; i < EDITOR_WRITER_TABS; ++i) {
        if (editor_writer_tabs[i] == col) {
            editor_writer_tabs[i] = 0;
            (void)save_editor_settings();
            return;
        }
        if (editor_writer_tabs[i] <= 0 && empty < 0) {
            empty = i;
        }
    }

    if (empty >= 0) {
        editor_writer_tabs[empty] = col;
    } else {
        editor_writer_tabs[EDITOR_WRITER_TABS - 1] = col;
    }

    (void)save_editor_settings();
}

static void editor_wrap_segment(const char *text,
                                int start,
                                int cols,
                                int *draw_start,
                                int *draw_len,
                                int *next_start)
{
    int len = (int)strlen(text);
    int limit;
    int break_at;
    int i;

    *draw_start = start;

    if (start >= len) {
        *draw_len = 0;
        *next_start = len;
        return;
    }

    limit = start + cols;
    if (limit >= len) {
        *draw_len = len - start;
        *next_start = len;
        return;
    }

    break_at = -1;
    for (i = start; i < limit; ++i) {
        if (isspace((unsigned char)text[i])) {
            break_at = i;
        }
    }

    if (break_at > start) {
        /* Every document byte needs a visible caret position. Hiding wrap
         * spaces made distinct insertion positions share the same caret. */
        *draw_len = break_at - start + 1;
        *next_start = break_at + 1;
    } else {
        *draw_len = cols;
        *next_start = start + cols;
    }
}

static int editor_wrapped_rows_for_line(int line, int cols)
{
    const char *text = editor_lines[line];
    int len = (int)strlen(text);
    int start = 0;
    int rows = 0;

    (void)cols;

    if (!editor_wrap_enabled() || len == 0) {
        return 1;
    }

    while (start < len) {
        int draw_start;
        int draw_len;
        int next_start;
        int segment_cols = editor_writer_segment_cols(rows);

        editor_wrap_segment(text, start, segment_cols,
                            &draw_start, &draw_len, &next_start);
        ++rows;
        if (next_start == len && draw_len == segment_cols) ++rows;

        if (next_start <= start) {
            break;
        }
        start = next_start;
    }

    return rows > 0 ? rows : 1;
}

static void editor_cursor_wrap_position(int *row_out, int *col_out)
{
    const char *text = editor_lines[editor_cursor_line];
    int len = (int)strlen(text);
    int start = 0;
    int row = 0;
    int end_affinity = editor_writer_mode && editor_state->v_editor_end_affinity &&
        editor_state->v_editor_end_line == editor_cursor_line &&
        editor_state->v_editor_end_col == editor_cursor_col;

    if (editor_cursor_col > len) {
        editor_cursor_col = len;
    }

    if (len == 0) {
        *row_out = 0;
        *col_out = editor_writer_mode ?
                   editor_writer_segment_indent(0) : 0;
        return;
    }

    while (start < len) {
        int draw_start;
        int draw_len;
        int next_start;
        int draw_end;
        int segment_cols = editor_writer_segment_cols(row);
        int indent = editor_writer_mode ?
                     editor_writer_segment_indent(row) : 0;

        editor_wrap_segment(text, start, segment_cols,
                            &draw_start, &draw_len, &next_start);
        draw_end = draw_start + draw_len;

        if (!end_affinity && editor_cursor_col == len && next_start == len &&
            draw_len == segment_cols) {
            *row_out = row + 1;
            *col_out = editor_writer_mode ?
                       editor_writer_segment_indent(row + 1) : 0;
            return;
        }

        if (editor_cursor_col < draw_end ||
            (editor_cursor_col == draw_end && (next_start == len || end_affinity))) {
            int col = editor_cursor_col - draw_start;

            if (col < 0) col = 0;
            if (col > draw_len) col = draw_len;
            *row_out = row;
            *col_out = indent + col;
            return;
        }

        if (editor_cursor_col < next_start) {
            *row_out = row;
            *col_out = indent + draw_len;
            return;
        }

        ++row;
        if (next_start <= start) {
            break;
        }
        start = next_start;
    }

    *row_out = row > 0 ? row - 1 : 0;
    *col_out = editor_writer_mode ?
               editor_writer_segment_indent(*row_out) : 0;
}
static int editor_cursor_screen_row(void)
{
    int cols = editor_visible_cols();
    int line;
    int row = 0;
    int local_row = 0;
    int local_col = 0;

    if (!editor_wrap_enabled()) {
        return editor_cursor_line - editor_top_line;
    }

    for (line = 0; line < editor_cursor_line; ++line)
        row += editor_wrapped_rows_for_line(line, cols);
    for (line = 0; line < editor_top_line; ++line)
        row -= editor_wrapped_rows_for_line(line, cols);
    editor_cursor_wrap_position(&local_row, &local_col);
    return row + local_row - editor_top_segment;
}

static int editor_cursor_screen_col(void)
{
    int row;
    int col;

    if (!editor_wrap_enabled()) {
        return editor_cursor_col - editor_left_col;
    }

    editor_cursor_wrap_position(&row, &col);
    return col;
}

static int editor_cursor_absolute_visual_row(void)
{
    int cols = editor_visible_cols();
    int line;
    int row = 0;
    int local_row = 0;
    int local_col = 0;

    if (!editor_wrap_enabled()) {
        return editor_cursor_line;
    }

    for (line = 0; line < editor_cursor_line; ++line) {
        row += editor_wrapped_rows_for_line(line, cols);
    }

    editor_cursor_wrap_position(&local_row, &local_col);
    return row + local_row;
}

static int editor_set_cursor_from_visual_row(int target_row, int desired_col)
{
    editor_state->v_editor_end_affinity = 0;
    int line;
    int row = 0;

    if (target_row < 0) {
        target_row = 0;
    }

    if (!editor_wrap_enabled()) {
        if (target_row >= editor_line_count) {
            target_row = editor_line_count - 1;
        }
        editor_cursor_line = target_row;
        if (desired_col < 0) desired_col = 0;
        if (desired_col > (int)strlen(editor_lines[editor_cursor_line])) {
            desired_col = (int)strlen(editor_lines[editor_cursor_line]);
        }
        editor_cursor_col = desired_col;
        return 1;
    }

    for (line = 0; line < editor_line_count; ++line) {
        const char *text = editor_lines[line];
        int len = (int)strlen(text);

        if (len == 0) {
            if (row == target_row) {
                editor_cursor_line = line;
                editor_cursor_col = 0;
                return 1;
            }
            ++row;
            continue;
        }

        {
            int start = 0;
            int segment = 0;

            while (start < len) {
                int draw_start;
                int draw_len;
                int next_start;
                int segment_cols =
                    editor_writer_segment_cols(segment);

                editor_wrap_segment(text, start, segment_cols,
                                    &draw_start, &draw_len, &next_start);

                if (row == target_row) {
                    int indent = editor_writer_mode ?
                                 editor_writer_segment_indent(segment) : 0;
                    int col = desired_col - indent;

                    if (col < 0) col = 0;
                    if (col > draw_len) col = draw_len;

                    editor_cursor_line = line;
                    editor_cursor_col = draw_start + col;
                    editor_state->v_editor_end_affinity = editor_writer_mode && col == draw_len && draw_len > 0;
                    editor_state->v_editor_end_line = line;
                    editor_state->v_editor_end_col = editor_cursor_col;
                    return 1;
                }

                ++row;
                ++segment;
                if (next_start == len && draw_len == segment_cols &&
                    row == target_row) {
                    editor_cursor_line = line;
                    editor_cursor_col = len;
                    return 1;
                }
                if (next_start == len && draw_len == segment_cols) ++row;
                if (next_start <= start) {
                    break;
                }
                start = next_start;
            }
        }
    }

    editor_cursor_line = editor_line_count - 1;
    editor_cursor_col =
        (int)strlen(editor_lines[editor_cursor_line]);
    return 1;
}

static void editor_move_visual_row(int delta)
{
    int current_row = editor_cursor_absolute_visual_row();
    int desired_col;

    if (editor_preferred_visual_col < 0) {
        editor_preferred_visual_col = editor_cursor_screen_col();
    }

    desired_col = editor_preferred_visual_col;
    (void)editor_set_cursor_from_visual_row(current_row + delta,
                                            desired_col);
    editor_ensure_cursor_visible();
}

static void editor_place_cursor_from_point(int x, int y)
{
    editor_state->v_editor_end_affinity = 0;
    int text_x = editor_text_x();
    int ruler_h =
        editor_writer_mode && editor_writer_ruler ? EDITOR_RULER_H : 0;
    int text_y = editor_window.y + WINDOW_TITLE_H +
                 EDITOR_MENU_H + ruler_h + 5;
    int status_y = editor_window.y + editor_window.h - EDITOR_STATUS_H;
    int row;
    int col;
    int top_abs_row = 0;
    int line;
    int cols = editor_visible_cols();

    if (x < text_x || y < text_y || y >= status_y) {
        return;
    }

    row = (y - text_y) / 9;
    col = (x - text_x + 3) / 6;
    if (col < 0) col = 0;
    if (col > cols) col = cols;

    if (editor_wrap_enabled()) {
        for (line = 0; line < editor_top_line; ++line) {
            top_abs_row += editor_wrapped_rows_for_line(line, cols);
        }
        (void)editor_set_cursor_from_visual_row(top_abs_row + editor_top_segment + row, col);
    } else {
        line = editor_top_line + row;
        if (line >= editor_line_count) {
            line = editor_line_count - 1;
        }
        if (line < 0) line = 0;

        editor_cursor_line = line;
        editor_cursor_col = editor_left_col + col;
        if (editor_cursor_col >
            (int)strlen(editor_lines[editor_cursor_line])) {
            editor_cursor_col =
                (int)strlen(editor_lines[editor_cursor_line]);
        }
    }

    editor_preferred_visual_col = -1;
    editor_ensure_cursor_visible();
}

static int editor_word_character(unsigned char ch)
{
    return isalnum(ch) || ch == '_' || ch >= 128;
}

static void editor_text_click(int x, int y, clock_t now, unsigned int modifiers)
{
    if (modifiers & KEYMOD_SHIFT) editor_selection_begin();
    else editor_selection_clear();
    editor_place_cursor_from_point(x, y);
    int row = editor_cursor_line, col = editor_cursor_col;
    int twice = !(modifiers & (KEYMOD_SHIFT | KEYMOD_CTRL)) &&
        editor_state->v_editor_click_time &&
        now - editor_state->v_editor_click_time <= CLOCKS_PER_SEC / 2 &&
        row == editor_state->v_editor_last_click_line &&
        abs(col - editor_state->v_editor_last_click_col) <= 1;
    if (twice) {
        int text_x = editor_text_x();
        editor_place_cursor_from_point(x - 3 < text_x ? text_x : x - 3, y);
        row = editor_cursor_line;
        col = editor_cursor_col;
    }
    if (twice && editor_word_character((unsigned char)editor_lines[row][col])) {
        int first = col, last = col;
        while (first > 0 && editor_word_character((unsigned char)editor_lines[row][first - 1])) --first;
        while (editor_word_character((unsigned char)editor_lines[row][last])) ++last;
        editor_anchor_line = row;
        editor_anchor_col = first;
        editor_cursor_col = last;
        editor_selection_active = 1;
        editor_selecting = 0;
        editor_state->v_editor_click_time = 0;
    } else {
        editor_selection_begin();
        editor_selecting = 1;
        editor_state->v_editor_click_time = now;
        editor_state->v_editor_last_click_line = row;
        editor_state->v_editor_last_click_col = col;
    }
}

static int reader_menu_click(int x, int y)
{
    if (!editor_read_only) return 0;
    int menu_y = editor_window.y + WINDOW_TITLE_H;
    if (editor_dialog != EDITOR_DIALOG_NONE) {
        editor_search_click(x, y);
        return 1;
    }
    if (point_in_rect(x, y, editor_window.x + 5, menu_y, 48, EDITOR_MENU_H)) {
        editor_begin_file_dialog(EDITOR_FILE_DIALOG_OPEN);
        return 1;
    }
    if (point_in_rect(x, y, editor_window.x + 57, menu_y, 48, EDITOR_MENU_H)) {
        editor_begin_dialog(EDITOR_DIALOG_FIND, "");
        return 1;
    }
    return y < menu_y + EDITOR_MENU_H && y >= editor_window.y + WINDOW_TITLE_H;
}

static void editor_ensure_cursor_visible(void)
{
    int rows = editor_visible_rows();
    int cols = editor_visible_cols();

    if (editor_wrap_enabled()) {
        editor_left_col = 0;
        int screen_row = editor_cursor_screen_row();
        if (screen_row < 0 || screen_row >= rows) {
            int target = editor_cursor_absolute_visual_row();
            if (screen_row >= rows) target -= rows - 1;
            editor_set_top_visual_row(target);
        }
    } else {
        editor_top_segment = 0;
        if (editor_cursor_line < editor_top_line) editor_top_line = editor_cursor_line;
        if (editor_cursor_line >= editor_top_line + rows)
            editor_top_line = editor_cursor_line - rows + 1;

        if (editor_cursor_col < editor_left_col) {
            editor_left_col = editor_cursor_col;
        }
        if (editor_cursor_col >= editor_left_col + cols) {
            editor_left_col = editor_cursor_col - cols + 1;
        }
    }

    if (editor_top_line < 0) editor_top_line = 0;
    if (editor_left_col < 0) editor_left_col = 0;
}

/* Positions are byte columns in the internal CP850 document. */
static void editor_selection_clear(void)
{
    editor_selection_active = editor_selecting = 0;
}

static void editor_selection_begin(void)
{
    if (!editor_selection_active) {
        editor_anchor_line = editor_cursor_line;
        editor_anchor_col = editor_cursor_col;
        editor_selection_active = 1;
    }
}

static int editor_selection_bounds(int *first, int *fc, int *last, int *lc)
{
    if (!editor_selection_active) return 0;
    *first = editor_anchor_line;
    *fc = editor_anchor_col;
    *last = editor_cursor_line;
    *lc = editor_cursor_col;
    if (*first > *last || (*first == *last && *fc > *lc)) {
        int t = *first; *first = *last; *last = t;
        t = *fc; *fc = *lc; *lc = t;
    }
    return *first != *last || *fc != *lc;
}

static int editor_selection_contains(int row, int col)
{
    int first, fc, last, lc;
    return editor_selection_bounds(&first, &fc, &last, &lc) &&
        (row > first || (row == first && col >= fc)) &&
        (row < last || (row == last && col < lc));
}

static int editor_selection_delete(void)
{
    if (editor_read_only) return 0;
    int first, fc, last, lc;
    if (!editor_selection_bounds(&first, &fc, &last, &lc)) return 0;
    size_t tail = strlen(editor_lines[last] + lc);
    if ((size_t)fc + tail >= EDITOR_MAX_COLS) {
        editor_set_status("Selection join exceeds line capacity");
        return 0;
    }
    if (!editor_history_before_edit()) return 0;
    memmove(editor_lines[first] + fc, editor_lines[last] + lc, tail + 1);
    memmove(editor_lines + first + 1, editor_lines + last + 1,
            (size_t)(editor_line_count - last - 1) * sizeof(editor_lines[0]));
    editor_line_count -= last - first;
    editor_cursor_line = first;
    editor_cursor_col = fc;
    editor_selection_clear();
    editor_typing_line = -1;
    editor_ensure_cursor_visible();
    return 1;
}

static void desktop_text_clipboard_publish(const char *cells)
{
    size_t size = strlen(cells) * 4 + 1;
    char *encoded = malloc(size);
    if (!encoded) { perror("Clipboard"); return; }
    if (bw_text_encode(encoded, size, cells) && bw_clipboard_set(NULL, encoded) != 0 &&
        errno != ENOTCONN && errno != ENOTSUP) perror("System clipboard");
    free(encoded);
}

static void desktop_text_clipboard_refresh(void)
{
    char *text = bw_clipboard_get(NULL);
    if (!text) {
        if (errno != ENOTCONN && errno != ENOTSUP) perror("System clipboard");
        return;
    }
    char *cells = malloc(strlen(text) + 1);
    if (cells) {
        bw_text_decode(cells, text);
        free(editor_clipboard);
        editor_clipboard = cells;
    } else perror("Clipboard");
    free(text);
}

static void editor_selection_copy(int cut)
{
    int first, fc, last, lc;
    if (!editor_selection_bounds(&first, &fc, &last, &lc)) return;
    size_t size = 1;
    for (int row = first; row <= last; ++row)
        size += (size_t)((row == last ? lc : (int)strlen(editor_lines[row])) -
                        (row == first ? fc : 0)) + (row < last);
    char *copy = malloc(size);
    if (!copy) { perror("Editor clipboard"); return; }
    size_t offset = 0;
    for (int row = first; row <= last; ++row) {
        int start = row == first ? fc : 0;
        int end = row == last ? lc : (int)strlen(editor_lines[row]);
        memcpy(copy + offset, editor_lines[row] + start, (size_t)(end - start));
        offset += (size_t)(end - start);
        if (row < last) copy[offset++] = '\n';
    }
    copy[offset] = 0;
    free(editor_clipboard);
    editor_clipboard = copy;
    desktop_text_clipboard_publish(copy);
    if (cut && !editor_read_only) (void)editor_selection_delete();
}

static void editor_replace_selection(const char *text)
{
    if (editor_read_only || !text) return;
    int first = editor_cursor_line, fc = editor_cursor_col;
    int last = first, lc = fc;
    (void)editor_selection_bounds(&first, &fc, &last, &lc);
    char (*replacement)[EDITOR_MAX_COLS] = calloc(EDITOR_MAX_LINES, sizeof(editor_lines[0]));
    if (!replacement) { perror("Editor paste"); return; }
    memcpy(replacement, editor_lines, (size_t)first * sizeof(editor_lines[0]));
    memcpy(replacement[first], editor_lines[first], (size_t)fc);
    int row = first, col = fc;
    for (const char *c = text; *c; ++c) {
        if (*c == '\n') { ++row; col = 0; }
        else {
            if (row >= EDITOR_MAX_LINES || col >= EDITOR_MAX_COLS - 1) goto full;
            replacement[row][col++] = *c;
        }
        if (row >= EDITOR_MAX_LINES) goto full;
    }
    int end_col = col;
    size_t tail = strlen(editor_lines[last] + lc);
    int remaining = editor_line_count - last - 1;
    if ((size_t)col + tail >= EDITOR_MAX_COLS || row + 1 + remaining > EDITOR_MAX_LINES) goto full;
    memcpy(replacement[row] + col, editor_lines[last] + lc, tail + 1);
    memcpy(replacement + row + 1, editor_lines + last + 1, (size_t)remaining * sizeof(editor_lines[0]));
    if (editor_history_before_edit()) {
        memcpy(editor_lines, replacement, sizeof(editor_lines));
        editor_line_count = row + 1 + remaining;
        editor_cursor_line = row;
        editor_cursor_col = end_col;
        editor_selection_clear();
        editor_typing_line = -1;
        editor_ensure_cursor_visible();
    }
    free(replacement);
    return;
full:
    free(replacement);
    editor_set_status("Paste exceeds document capacity");
}

static void editor_paste(void)
{
    if (editor_read_only) return;
    desktop_text_clipboard_refresh();
    editor_replace_selection(editor_clipboard);
}

static int editor_navigation(int scan, unsigned int modifiers)
{
    if (scan != 71 && scan != 72 && scan != 73 && scan != 75 &&
        scan != 77 && scan != 79 && scan != 80 && scan != 81) return 0;
    if (scan == 75 || scan == 77) editor_state->v_editor_end_affinity = 0;
    int shift = (modifiers & KEYMOD_SHIFT) != 0;
    int ctrl = (modifiers & KEYMOD_CTRL) != 0;
    int first, fc, last, lc;
    if (!shift && (scan == 75 || scan == 77) &&
        editor_selection_bounds(&first, &fc, &last, &lc)) {
        editor_cursor_line = scan == 75 ? first : last;
        editor_cursor_col = scan == 75 ? fc : lc;
        editor_selection_clear();
    } else {
        if (shift) editor_selection_begin();
        else editor_selection_clear();
        if (scan == 75 || scan == 77) {
            int direction = scan == 75 ? -1 : 1;
            int moved = 0;
            do {
                int length = (int)strlen(editor_lines[editor_cursor_line]);
                if (direction < 0) {
                    if (editor_cursor_col > 0) --editor_cursor_col;
                    else if (editor_cursor_line > 0) {
                        --editor_cursor_line;
                        editor_cursor_col = (int)strlen(editor_lines[editor_cursor_line]);
                    } else break;
                } else {
                    if (editor_cursor_col < length) ++editor_cursor_col;
                    else if (editor_cursor_line + 1 < editor_line_count) {
                        ++editor_cursor_line;
                        editor_cursor_col = 0;
                    } else break;
                }
                ++moved;
                if (!ctrl) break;
                const char *line = editor_lines[editor_cursor_line];
                int col = editor_cursor_col;
                if (direction < 0 && col && !isspace((unsigned char)line[col-1]) &&
                    (col == (int)strlen(line) || isspace((unsigned char)line[col]))) break;
                if (direction > 0 && line[col] && !isspace((unsigned char)line[col]) &&
                    (!col || isspace((unsigned char)line[col-1]))) break;
            } while (moved < EDITOR_MAX_LINES * EDITOR_MAX_COLS);
            editor_preferred_visual_col = -1;
        } else if (scan == 71 || scan == 79) {
            if (editor_writer_mode && !ctrl) {
                int row = editor_cursor_absolute_visual_row();
                (void)editor_set_cursor_from_visual_row(row, scan == 71 ? 0 : EDITOR_MAX_COLS);
            } else {
                editor_state->v_editor_end_affinity = 0;
                if (ctrl) editor_cursor_line = scan == 71 ? 0 : editor_line_count - 1;
                editor_cursor_col = scan == 71 ? 0 : (int)strlen(editor_lines[editor_cursor_line]);
            }
            editor_preferred_visual_col = -1;
        } else {
            int delta = scan == 72 || scan == 73 ? -1 : 1;
            if (scan == 73 || scan == 81) delta *= editor_visible_rows();
            editor_move_visual_row(delta);
        }
    }
    editor_match_line = -1;
    editor_typing_line = -1;
    editor_ensure_cursor_visible();
    return 1;
}

static void editor_insert_char(char ch)
{
    if (editor_read_only) return;
    editor_state->v_editor_end_affinity = 0;
    int first, fc, last, lc;
    if (editor_selection_bounds(&first, &fc, &last, &lc)) {
        char replacement[2] = {ch, 0};
        editor_replace_selection(replacement);
        return;
    }
    editor_selection_clear();
    editor_match_line = -1;
    if (editor_cursor_line < 0 || editor_cursor_line >= EDITOR_MAX_LINES ||
        editor_cursor_col < 0) return;
    char *line = editor_lines[editor_cursor_line];
    int len = (int)strlen(line);

    if (len >= EDITOR_MAX_COLS - 1) {
        return;
    }

    if (editor_cursor_col > len) {
        editor_cursor_col = len;
    }

    /* Group continuous typing into words; navigation, other edits and
     * pauses start a new undo step. */
    clock_t now = bw_clock();
    if (ch == ' ' || editor_typing_line != editor_cursor_line ||
        editor_typing_col != editor_cursor_col ||
        now - editor_typing_time >= CLOCKS_PER_SEC) {
        if (!editor_history_before_edit()) return;
    }
    memmove(line + editor_cursor_col + 1,
            line + editor_cursor_col,
            (size_t)(len - editor_cursor_col + 1));
    line[editor_cursor_col] = ch;
    ++editor_cursor_col;
    editor_typing_line = editor_cursor_line;
    editor_typing_col = editor_cursor_col;
    editor_typing_time = now;
    editor_ensure_cursor_visible();
}

static void editor_newline(void)
{
    if (editor_read_only) return;
    editor_state->v_editor_end_affinity = 0;
    int first, fc, last, lc;
    if (editor_selection_bounds(&first, &fc, &last, &lc)) {
        char replacement[] = "\n";
        editor_replace_selection(replacement);
        return;
    }
    editor_selection_clear();
    editor_match_line = -1;
    char *line;
    int len;

    if (editor_line_count >= EDITOR_MAX_LINES) {
        return;
    }

    line = editor_lines[editor_cursor_line];
    len = (int)strlen(line);

    if (editor_cursor_col > len) {
        editor_cursor_col = len;
    }

    if (!editor_history_before_edit()) return;
    memmove(&editor_lines[editor_cursor_line + 2],
            &editor_lines[editor_cursor_line + 1],
            (size_t)(editor_line_count - editor_cursor_line - 1) *
            sizeof(editor_lines[0]));

    {
        int tail_len = len - editor_cursor_col;
        memcpy(editor_lines[editor_cursor_line + 1],
               line + editor_cursor_col,
               (size_t)tail_len);
        editor_lines[editor_cursor_line + 1][tail_len] = '\0';
        line[editor_cursor_col] = '\0';
    }

    ++editor_line_count;
    ++editor_cursor_line;
    editor_cursor_col = 0;
    editor_ensure_cursor_visible();
}

static void editor_delete_forward(void)
{
    if (editor_read_only) return;
    if (editor_selection_delete()) return;
    char *line = editor_lines[editor_cursor_line];
    int len = (int)strlen(line);
    editor_match_line = -1;
    editor_preferred_visual_col = -1;
    if (editor_cursor_col < len) {
        if (!editor_history_before_edit()) return;
        memmove(line + editor_cursor_col, line + editor_cursor_col + 1,
                (size_t)(len - editor_cursor_col));
    } else if (editor_cursor_line + 1 < editor_line_count) {
        char *next = editor_lines[editor_cursor_line + 1];
        if (len + strlen(next) >= EDITOR_MAX_COLS) return;
        if (!editor_history_before_edit()) return;
        memcpy(line + len, next, strlen(next) + 1);
        memmove(next, next + EDITOR_MAX_COLS,
                (size_t)(editor_line_count - editor_cursor_line - 2) * sizeof(editor_lines[0]));
        editor_lines[--editor_line_count][0] = '\0';
    }
    editor_ensure_cursor_visible();
}

static void editor_backspace(void)
{
    if (editor_read_only) return;
    editor_state->v_editor_end_affinity = 0;
    int first, fc, last, lc;
    if (editor_selection_bounds(&first, &fc, &last, &lc)) {
        (void)editor_selection_delete();
        return;
    }
    editor_match_line = -1;
    char *line = editor_lines[editor_cursor_line];
    int len = (int)strlen(line);

    if (editor_cursor_col > 0) {
        if (!editor_history_before_edit()) return;
        memmove(line + editor_cursor_col - 1,
                line + editor_cursor_col,
                (size_t)(len - editor_cursor_col + 1));
        --editor_cursor_col;
    } else if (editor_cursor_line > 0) {
        char *previous = editor_lines[editor_cursor_line - 1];
        int previous_len = (int)strlen(previous);

        if (previous_len + len >= EDITOR_MAX_COLS) {
            return;
        }

        if (!editor_history_before_edit()) return;
        memcpy(previous + previous_len,
               line,
               (size_t)len + 1);
        memmove(&editor_lines[editor_cursor_line],
                &editor_lines[editor_cursor_line + 1],
                (size_t)(editor_line_count - editor_cursor_line - 1) *
                sizeof(editor_lines[0]));
        --editor_line_count;
        --editor_cursor_line;
        editor_cursor_col = previous_len;
        editor_lines[editor_line_count][0] = '\0';
    }

    editor_ensure_cursor_visible();
}

static void editor_search_geometry(int *x, int *y, int *w)
{
    *w = editor_window.w > 390 ? 360 : editor_window.w - 20;
    *x = editor_window.x + (editor_window.w - *w) / 2;
    *y = editor_window.y + (editor_window.h - 154) / 2;
    if (editor_match_line >= 0) {
        int match_y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H +
                      (editor_writer_mode && editor_writer_ruler ? EDITOR_RULER_H : 0) +
                      5 + editor_cursor_screen_row() * 9;
        if (match_y >= *y && match_y < *y + 154) {
            *y = match_y < editor_window.y + editor_window.h / 2 ?
                 editor_window.y + editor_window.h - EDITOR_STATUS_H - 158 :
                 editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 4;
        }
    }
}

static void editor_search_action(int action)
{
    if (editor_read_only && action >= 2) return;
    if (action == 3) {
        (void)editor_replace_all(editor_search_text, editor_replacement);
    } else if (action == 2) {
        if (editor_match_line == editor_cursor_line &&
            editor_match_col == editor_cursor_col &&
            editor_replace_at_cursor(editor_search_text, editor_replacement)) {
            (void)editor_find_from(editor_search_text, editor_cursor_line,
                                  editor_cursor_col);
        } else {
            (void)editor_find_from(editor_search_text, editor_cursor_line,
                                  editor_cursor_col);
        }
    } else {
        int direction = action == 1 ? -1 : 1;
        int start = editor_cursor_col;
        if (direction < 0 || (editor_match_line == editor_cursor_line &&
                             editor_match_col == editor_cursor_col))
            start += direction;
        (void)editor_find_direction(editor_search_text, editor_cursor_line,
                                    start, direction);
    }
}

static void editor_draw_search_dialog(void)
{
    int x, y, w;
    int i;
    const char *buttons[4] = {"Next", "Previous", "Replace", "Replace All"};
    if (active_window == APP_EDITOR) ui_region_count = 0;
    ui_draw_scope = ui_scope();
    editor_search_geometry(&x, &y, &w);
    fill_rect(x + 3, y + 3, w, 154, WINDOW_SHADOW_COLOR);
    fill_rect(x, y, w, 154, WINDOW_CHROME_COLOR);
    draw_bevel(x, y, w, 154, 1);
    fill_rect(x + 2, y + 2, w - 4, 16, TITLE_COLOR);
    draw_text(x + 6, y + 6, editor_read_only ? "Find" : "Find / Replace", TITLE_TEXT_COLOR, 24);
    bwa_draw_standard_button(x + w - 18, y + 3, 14, 13, "x", 0);
    for (i = 0; i < (editor_read_only ? 1 : 2); ++i) {
        const char *value = i ? editor_replacement : editor_search_text;
        int cols = (w - 82) / 6;
        int offset = editor_search_field == i && editor_search_caret >= cols ?
                     editor_search_caret - cols + 1 : 0;
        draw_text(x + 8, y + 29 + i * 24, i ? "Replace" : "Find", TEXT_COLOR, 10);
        fill_rect(x + 70, y + 24 + i * 24, w - 78, 18, WINDOW_FACE_COLOR);
        draw_bevel(x + 70, y + 24 + i * 24, w - 78, 18, 0);
        if (editor_search_field == i && editor_search_selected) {
            fill_rect(x + 73, y + 27 + i * 24, w - 84, 12, TITLE_COLOR);
        }
        bwa_pointer_region(x + 70, y + 24 + i * 24, w - 78, 18, BUDO_CURSOR_TEXT, NULL);
        draw_text(x + 74, y + 29 + i * 24, value + offset,
                  editor_search_field == i && editor_search_selected ?
                  TITLE_TEXT_COLOR : TEXT_COLOR, cols);
        if (editor_search_field == i && !editor_search_selected && text_caret_visible)
            fill_rect(x + 74 + (editor_search_caret - offset) * 6,
                      y + 28 + i * 24, 2, TEXT_CELL_HEIGHT, TEXT_COLOR);
    }
    bwa_draw_standard_button(x + 8, y + 73, 60, 16, "Case", editor_search_case);
    bwa_draw_standard_button(x + 76, y + 73, 60, 16, "Word", editor_search_word);
    bwa_draw_standard_button(x + 144, y + 73, 60, 16, "Wrap", editor_search_wrap);
    for (i = 0; i < (editor_read_only ? 2 : 4); ++i) {
        int bw = (w - 16) / 4;
        bwa_draw_standard_button(x + 8 + i * bw, y + 94, bw - 3, 19, buttons[i], 0);
    }
    draw_text(x + 8, y + 120, editor_status, TEXT_COLOR, (w - 16) / 6);
    draw_text(x + 8, y + 138, "Enter=Next Ctrl+P=Prev Esc=Close", TEXT_COLOR, (w - 16) / 6);
}

static void editor_search_click(int mx, int my)
{
    int x, y, w;
    int i;
    editor_search_geometry(&x, &y, &w);
    if (point_in_rect(mx, my, x + w - 18, y + 3, 14, 13)) {
        editor_dialog = EDITOR_DIALOG_NONE;
        return;
    }
    for (i = 0; i < (editor_read_only ? 1 : 2); ++i) {
        if (point_in_rect(mx, my, x + 70, y + 24 + i * 24, w - 78, 18)) {
            int cols = (w - 82) / 6;
            int offset = editor_search_field == i && editor_search_caret >= cols ?
                         editor_search_caret - cols + 1 : 0;
            int len = (int)strlen(i ? editor_replacement : editor_search_text);
            editor_search_field = i;
            editor_search_selected = 0;
            editor_search_caret = offset + (mx - x - 74) / 6;
            if (editor_search_caret < 0) editor_search_caret = 0;
            if (editor_search_caret > len) editor_search_caret = len;
            return;
        }
    }
    if (point_in_rect(mx, my, x + 8, y + 73, 60, 16))
        editor_search_case = !editor_search_case;
    else if (point_in_rect(mx, my, x + 76, y + 73, 60, 16))
        editor_search_word = !editor_search_word;
    else if (point_in_rect(mx, my, x + 144, y + 73, 60, 16))
        editor_search_wrap = !editor_search_wrap;
    else if (point_in_rect(mx, my, x + 8, y + 94, w - 16, 19)) {
        int action = (mx - x - 8) / ((w - 16) / 4);
        if (action < (editor_read_only ? 2 : 4)) editor_search_action(action);
        return;
    }
    editor_match_line = -1;
}

static void editor_search_key(int key)
{
    char *value = editor_search_field ? editor_replacement : editor_search_text;
    int len = (int)strlen(value);
    int *caret = &editor_search_caret;
    if (editor_search_selected && (key == 8 || key >= 32)) {
        value[0] = '\0';
        len = 0;
        *caret = 0;
        editor_search_selected = 0;
        editor_match_line = -1;
    }
    if (key == 27) {
        editor_dialog = EDITOR_DIALOG_NONE;
    } else if (key == 9) {
        editor_search_field = editor_read_only ? 0 : !editor_search_field;
        editor_search_selected = 1;
        *caret = (int)strlen(editor_search_field ? editor_replacement : editor_search_text);
    } else if (key == 13) {
        editor_search_action((bw_modifiers() & 4u) ?
                             ((bw_modifiers() & 3u) ? 3 : 2) : 0);
    } else if (key == 12 || key == 23 || key == 2) {
        if (key == 12) editor_search_case = !editor_search_case;
        if (key == 23) editor_search_word = !editor_search_word;
        if (key == 2) editor_search_wrap = !editor_search_wrap;
        editor_match_line = -1;
    } else if (key == 26 || key == 25) {
        editor_history_restore(key == 25);
    } else if (key == 16) {
        editor_search_action(1);
    } else if (key == 18) {
        editor_search_action(2);
    } else if (key == 1) {
        editor_search_selected = 1;
    } else if (key == 0) {
        int extended = getch();
        if (editor_search_selected && extended == 83) {
            value[0] = '\0';
            len = 0;
            *caret = 0;
            editor_match_line = -1;
        }
        editor_search_selected = 0;
        if (extended == 61) editor_search_action((bw_modifiers() & 3u) ? 1 : 0);
        else if (extended == 75 && *caret > 0) --*caret;
        else if (extended == 77 && *caret < len) ++*caret;
        else if (extended == 71) *caret = 0;
        else if (extended == 79) *caret = len;
        else if (extended == 83 && *caret < len) {
            memmove(value + *caret, value + *caret + 1, (size_t)(len - *caret));
            editor_match_line = -1;
        }
    } else if (key == 8 && *caret > 0) {
        memmove(value + *caret - 1, value + *caret, (size_t)(len - *caret + 1));
        --*caret;
        editor_match_line = -1;
    } else if (key >= 32 && key != 127 && len < MAX_PATH - 1) {
        memmove(value + *caret + 1, value + *caret, (size_t)(len - *caret + 1));
        value[(*caret)++] = (char)key;
        editor_match_line = -1;
    }
}

static void editor_draw_text(int x, int y, int line, int start, int count)
{
    int i;
    int len = (int)strlen(editor_lines[line]);
    for (i = 0; i < count && start + i < len; ++i) {
        char character[2] = {editor_lines[line][start + i], '\0'};
        int matched = editor_selection_contains(line, start + i) || (editor_match_line == line &&
                      start + i >= editor_match_col &&
                      start + i < editor_match_col + editor_match_len);
        if (matched) fill_rect(x + i * 6, y, 6, 9, ui_color_indices[BUDO_SYS_COLOR_SELECTION_BG]);
        draw_text(x + i * 6, y, character, matched ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR, 1);
    }
}

static void draw_editor_window(void)
{
    int min_x = window_min_button_x(&editor_window);
    int max_x = window_max_button_x(&editor_window);
    int close_x = window_close_button_x(&editor_window);
    int control_x = min_x - 154;
    int wrap_x = control_x;
    int rows_x = control_x + 48;
    int writer_x = control_x + 96;
    int text_x = editor_text_x();
    int ruler_h =
        editor_writer_mode && editor_writer_ruler ? EDITOR_RULER_H : 0;
    int text_y = editor_window.y + WINDOW_TITLE_H +
                 EDITOR_MENU_H + ruler_h + 5;
    int visible_rows = editor_visible_rows();
    int visible_cols = editor_visible_cols();
    int screen_row = 0;
    int line;

    if (!editor_window.open || editor_window.minimized) {
        return;
    }

    draw_window_chrome(&editor_window);
    {
        char title[MAX_NAME + 24];
        const char *name = strrchr(editor_path, '/');
        name = name ? name + 1 : (editor_path[0] ? editor_path : "Untitled");
        int max_chars = (wrap_x - editor_window.x - 13) / 6;
        snprintf(title, sizeof(title), "%s%s - %.255s", editor_read_only ? "Reader" : editor_writer_mode ? "Write" : "Editor",
                 editor_modified() ? " *" : "", name);
        draw_text_elided(editor_window.x + 7, editor_window.y + 6, title,
                  window_title_text_color(), max_chars);
        bwa_pointer_region(editor_window.x + 7, editor_window.y + 2,
                           wrap_x - editor_window.x - 10, 16, BUDO_CURSOR_ARROW,
                           name);
    }

    /*
     * Keep Writer deliberately quiet: wrapping and row numbers are
     * implementation details there, so only expose the mode switch.
     * The plain editor retains its compact technical controls.
     */
    if (!editor_writer_mode) {
        ui_register(wrap_x, editor_window.y + 6, 44, 12, BUDO_CURSOR_ARROW, "Wrap lines", 1, 0);
        draw_checkbox(wrap_x, editor_window.y + 6,
                      editor_wrap_enabled());
        draw_text(wrap_x + 14, editor_window.y + 6,
                  "Wrap", window_title_text_color(), 4);
        ui_register(rows_x, editor_window.y + 6, 44, 12, BUDO_CURSOR_ARROW, "Show row numbers", 1, 0);
        draw_checkbox(rows_x, editor_window.y + 6,
                      editor_rows_enabled());
        draw_text(rows_x + 14, editor_window.y + 6,
                  "Rows", window_title_text_color(), 4);
    }
    if (!editor_read_only) {
        ui_register(writer_x, editor_window.y + 6, 56, 12, BUDO_CURSOR_ARROW, "Writer mode", 1, 0);
        draw_checkbox(writer_x, editor_window.y + 6,
                      editor_writer_mode);
        draw_text(writer_x + 14, editor_window.y + 6,
                  "Writer", window_title_text_color(), 6);
    }

    draw_window_button(min_x, editor_window.y + WINDOW_BORDER, 0);
    draw_window_button(max_x, editor_window.y + WINDOW_BORDER, 1);
    draw_window_button(close_x, editor_window.y + WINDOW_BORDER, 2);

    if (editor_read_only) {
        draw_menu_bar_item(editor_window.x + 5, editor_window.y + WINDOW_TITLE_H, 48, "Open...", 0);
        draw_menu_bar_item(editor_window.x + 57, editor_window.y + WINDOW_TITLE_H, 48, "Find...", 0);
    } else {
        draw_menu_bar_item(editor_window.x + 5,
                           editor_window.y + WINDOW_TITLE_H,
                           32, "File",
                           editor_menu == EDITOR_MENU_FILE);
        draw_menu_bar_item(editor_window.x + 41,
                           editor_window.y + WINDOW_TITLE_H,
                           32, "Edit",
                           editor_menu == EDITOR_MENU_EDIT);
        draw_menu_bar_item(editor_window.x + 77,
                           editor_window.y + WINDOW_TITLE_H,
                           48, editor_writer_mode ? "Find" : "Search",
                           editor_menu == EDITOR_MENU_SEARCH);
        if (editor_writer_mode) {
            draw_menu_bar_item(editor_window.x + 113,
                               editor_window.y + WINDOW_TITLE_H,
                               56, "Document",
                               editor_menu == EDITOR_MENU_DOCUMENT);
        }
    }
    fill_rect(editor_window.x + 2,
              editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H,
              editor_window.w - 4, 1, FOLDER_DARK);

    /* Both modes share one writing surface and the same status-bar edge. */
    fill_rect(editor_window.x + 2,
              editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 1,
              editor_window.w - 4,
              editor_window.h - WINDOW_TITLE_H - EDITOR_MENU_H -
              EDITOR_STATUS_H - 1, FILE_COLOR);

    if (editor_writer_mode && editor_writer_ruler) {
        int ruler_y = editor_window.y + WINDOW_TITLE_H +
                      EDITOR_MENU_H + 2;
        int ruler_x = text_x;
        int page_cols = editor_visible_cols();
        if (page_cols > EDITOR_WRITER_PAGE_COLS) page_cols = EDITOR_WRITER_PAGE_COLS;
        int ruler_w = page_cols * 6;
        int tick;
        int i;
        int left_x = ruler_x + editor_writer_left_indent * 6;
        int first_x = ruler_x +
                      (editor_writer_left_indent +
                       editor_writer_first_indent) * 6;
        int right_x = ruler_x +
                      (page_cols -
                       editor_writer_right_indent) * 6;

        /*
         * Writer ruler belongs to the page, not to application chrome.
         * Keep the complete writing surface visually continuous.
         */
        fill_rect(ruler_x, ruler_y, ruler_w, EDITOR_RULER_H - 3,
                  FILE_COLOR);
        fill_rect(ruler_x, ruler_y + 10, ruler_w, 1, FOLDER_DARK);

        for (tick = 0; tick <= ruler_w / 30; ++tick) {
            int x = ruler_x + tick * 30;
            fill_rect(x, ruler_y + 7, 1,
                      tick % 2 == 0 ? 7 : 4,
                      FOLDER_COLOR);
        }

        for (i = 0; i < EDITOR_WRITER_TABS; ++i) {
            int tab = editor_writer_tabs[i];
            if (tab > 0 && ruler_x + tab * 6 < ruler_x + ruler_w) {
                int tx = ruler_x + tab * 6;
                fill_rect(tx - 2, ruler_y + 2, 5, 2, TEXT_COLOR);
                fill_rect(tx, ruler_y + 2, 1, 5, TEXT_COLOR);
            }
        }

        if (left_x < ruler_x + ruler_w) {
            draw_text(left_x, ruler_y + 11, "L", TEXT_COLOR, 1);
        }
        if (first_x < ruler_x + ruler_w) {
            draw_text(first_x, ruler_y + 1, "F", TEXT_COLOR, 1);
        }
        if (right_x >= ruler_x) {
            draw_text(right_x - 5, ruler_y + 11, "R", TEXT_COLOR, 1);
        }
    }

    for (line = editor_top_line;
         line < editor_line_count && screen_row < visible_rows;
         ++line) {
        int len = (int)strlen(editor_lines[line]);

        if (editor_wrap_enabled()) {
            int start = 0;
            int segment = 0;

            if (len == 0) {
                int y = text_y + screen_row * 9;

                if (editor_rows_enabled()) {
                    char number[16];
                    snprintf(number, sizeof(number), "%3d", line + 1);
                    draw_text(editor_window.x + 6, y,
                              number, TEXT_COLOR, 3);
                }
                ++screen_row;
                continue;
            }

            while (start < len && screen_row < visible_rows) {
                int draw_start;
                int draw_len;
                int next_start;
                int y = text_y + screen_row * 9;

                {
                    int segment_cols =
                        editor_writer_segment_cols(segment);
                    editor_wrap_segment(editor_lines[line], start,
                                        segment_cols,
                                        &draw_start, &draw_len, &next_start);
                }

                if (line == editor_top_line && segment < editor_top_segment) {
                    ++segment;
                    if (next_start == len &&
                        draw_len == editor_writer_segment_cols(segment - 1) &&
                        segment >= editor_top_segment) ++screen_row;
                    if (next_start <= start) break;
                    start = next_start;
                    continue;
                }
                if (editor_rows_enabled() && segment == 0) {
                    char number[16];
                    snprintf(number, sizeof(number), "%3d", line + 1);
                    draw_text(editor_window.x + 6, y,
                              number, TEXT_COLOR, 3);
                }

                if (draw_len > 0) {
                    int draw_x = text_x +
                                 (editor_writer_mode ?
                                  editor_writer_segment_indent(segment) * 6 :
                                  0);
                    editor_draw_text(draw_x, y, line, draw_start, draw_len);
                }

                ++screen_row;
                ++segment;

                if (next_start == len &&
                    draw_len == editor_writer_segment_cols(segment - 1)) {
                    if (line != editor_top_line || segment >= editor_top_segment)
                        ++screen_row;
                }

                if (next_start <= start) {
                    break;
                }
                start = next_start;
            }
        } else {
            int y = text_y + screen_row * 9;

            if (editor_rows_enabled()) {
                char number[16];
                snprintf(number, sizeof(number), "%3d", line + 1);
                draw_text(editor_window.x + 6, y,
                          number, TEXT_COLOR, 3);
            }

            if (editor_left_col < len) {
                editor_draw_text(text_x, y, line, editor_left_col, visible_cols);
            }
            ++screen_row;
        }
    }

    {
        char status_text[96];
        int status_y = editor_window.y + editor_window.h -
                       EDITOR_STATUS_H;
        int words = editor_word_count();

        fill_rect(editor_window.x + 2, status_y,
                  editor_window.w - 4,
                  EDITOR_STATUS_H - 2, ui_color_indices[BUDO_SYS_COLOR_STATUS_BG]);
        if (editor_read_only) {
            snprintf(status_text, sizeof(status_text), "READER  Words %d  Read only", words);
        } else if (editor_writer_mode) {
            snprintf(status_text, sizeof(status_text),
                     "WRITER  Ln %d  Col %d  Words %d  Saved %s",
                     editor_cursor_line + 1,
                     editor_cursor_col + 1,
                     words,
                     editor_last_save);
        } else {
            snprintf(status_text, sizeof(status_text),
                     "EDITOR  Ln %d  Col %d  Words %d  Saved %s",
                     editor_cursor_line + 1,
                     editor_cursor_col + 1,
                     words,
                     editor_last_save);
        }
        draw_text(editor_window.x + 6, status_y + 3,
                  status_text, ui_color_indices[BUDO_SYS_COLOR_STATUS_TEXT],
                  (editor_window.w - 16) / 6);
    }

    desktop_scroll_draw(APP_EDITOR, 0);
    if (!editor_read_only && editor_menu != EDITOR_MENU_NONE) {
        int menu_x = editor_window.x + 5;
        int menu_y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H;
        BudoMenuItem file_items[6] = {
            {"New   Ctrl+N", 1, 0},
            {"Open... Ctrl+O", 1, 0},
            {"Save  Ctrl+S", 1, 0},
            {"Save As...", 1, 0},
            {"Export...", 1, 0},
            {"Close", 1, 0}
        };
        BudoMenuItem edit_items[8] = {
            {"Undo  Ctrl+Z", editor_undo_count > 0, 0},
            {"Redo  Ctrl+Y", editor_redo_count > 0, 0},
            {"Delete Line", 1, 0},
            {"Clear All", 1, 0},
            {"Cut  Ctrl+X", editor_selection_active, 0},
            {"Copy Ctrl+C", editor_selection_active, 0},
            {"Paste Ctrl+V", editor_clipboard != NULL, 0},
            {"Select All Ctrl+A", 1, 0}
        };
        BudoMenuItem search_items[4] = {
            {"Find... Ctrl+F", 1, 0},
            {"Replace...", 1, 0},
            {"Next  F3", editor_search_text[0] != '\0', 0},
            {"Previous", editor_search_text[0] != '\0', 0}
        };
        BudoMenuItem document_items[1] = {
            {editor_writer_ruler ? "Ruler" : "Ruler", 1,
             editor_writer_ruler}
        };

        if (editor_menu == EDITOR_MENU_FILE) {
            if (!editor_writer_mode) {
                file_items[4].label = "Close";
                draw_popup_menu(menu_x, menu_y, 156, file_items, 5);
            } else {
                draw_popup_menu(menu_x, menu_y, 156, file_items, 6);
            }
        } else if (editor_menu == EDITOR_MENU_EDIT) {
            draw_popup_menu(menu_x + 36, menu_y, 156,
                            edit_items, 8);
        } else if (editor_menu == EDITOR_MENU_SEARCH) {
            draw_popup_menu(menu_x + 72, menu_y, 156,
                            search_items, 4);
        } else if (editor_menu == EDITOR_MENU_DOCUMENT) {
            draw_popup_menu(menu_x + 108, menu_y, 156,
                            document_items, 1);
        }
    }

    if (editor_search_dialog_active()) {
        editor_draw_search_dialog();
    } else if (editor_dialog != EDITOR_DIALOG_NONE) {
        int dialog_w = editor_window.w > 390 ? 360 :
                       editor_window.w - 30;
        int dialog_x = editor_window.x +
                       (editor_window.w - dialog_w) / 2;
        int dialog_y = editor_window.y +
                       editor_window.h / 2 - 32;
        const char *label = "Input";

        if (editor_dialog == EDITOR_DIALOG_OPEN)
            label = "Open file";
        else if (editor_dialog == EDITOR_DIALOG_SAVE_AS)
            label = "Save file as";
        else if (editor_dialog == EDITOR_DIALOG_FIND ||
                 editor_dialog == EDITOR_DIALOG_REPLACE_FIND ||
                 editor_dialog == EDITOR_DIALOG_REPLACE_ALL_FIND)
            label = "Find";
        else if (editor_dialog == EDITOR_DIALOG_REPLACE_WITH ||
                 editor_dialog == EDITOR_DIALOG_REPLACE_ALL_WITH)
            label = "Replace with";

        fill_rect(dialog_x, dialog_y, dialog_w, 64, WINDOW_CHROME_COLOR);
        draw_bevel(dialog_x, dialog_y, dialog_w, 64, 1);
        draw_rect(dialog_x, dialog_y, dialog_w, 64, TEXT_COLOR);
        fill_rect(dialog_x + 1, dialog_y + 1,
                  dialog_w - 2, 14, TITLE_COLOR);
        draw_text(dialog_x + 5, dialog_y + 5,
                  label, TITLE_TEXT_COLOR, 20);
        draw_rect(dialog_x + 6, dialog_y + 24,
                  dialog_w - 12, 16, FOLDER_DARK);
        draw_text(dialog_x + 10, dialog_y + 29,
                  editor_dialog_input, TEXT_COLOR,
                  (dialog_w - 20) / 6);
        draw_text(dialog_x + 6, dialog_y + 48,
                  "Enter=OK  Esc=Cancel",
                  FOLDER_COLOR, 20);
    }

    fill_rect(editor_window.x + editor_window.w - 9,
              editor_window.y + editor_window.h - 9,
              7, 2, TEXT_COLOR);
    fill_rect(editor_window.x + editor_window.w - 5,
              editor_window.y + editor_window.h - 13,
              3, 2, TEXT_COLOR);
}

static void terminal_add_line(const char *text)
{
    size_t len;

    if (text == NULL) {
        text = "";
    }

    if (terminal_line_count >= TERMINAL_MAX_LINES) {
        terminal_history_trimmed();
        memmove(terminal_lines[0], terminal_lines[1],
                (TERMINAL_MAX_LINES - 1) * sizeof(terminal_lines[0]));
        terminal_line_count = TERMINAL_MAX_LINES - 1;
    }

    len = strlen(text);
    if (len >= TERMINAL_LINE_LEN) {
        len = TERMINAL_LINE_LEN - 1;
    }

    memcpy(terminal_lines[terminal_line_count], text, len);
    terminal_lines[terminal_line_count][len] = '\0';
    ++terminal_line_count;
}

static void terminal_add_wrapped_line(const char *text)
{
    const char *p = text;

    if (*p == '\0') {
        terminal_add_line("");
        return;
    }

    while (*p != '\0') {
        char line[TERMINAL_LINE_LEN];
        size_t len = strlen(p);

        if (len >= TERMINAL_LINE_LEN) {
            len = TERMINAL_LINE_LEN - 1;
        }

        memcpy(line, p, len);
        line[len] = '\0';
        terminal_add_line(line);
        p += len;
    }
}

static void terminal_read_capture(void)
{
    FILE *file = fopen(TERMINAL_CAPTURE_FILE, "rt");
    char line[TERMINAL_LINE_LEN * 2];

    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t len = strlen(line);

        while (len > 0 &&
               (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        terminal_add_wrapped_line(line);
    }

    fclose(file);
    (void)remove(TERMINAL_CAPTURE_FILE);
}

static int terminal_change_directory(const char *path)
{
    char saved[MAX_PATH];

    if (getcwd(saved, sizeof(saved)) == NULL) {
        return 0;
    }

    if (chdir(terminal_cwd) != 0 || chdir(path) != 0) {
        bw_restore_directory(saved);
        return 0;
    }

    if (getcwd(terminal_cwd, sizeof(terminal_cwd)) == NULL) {
        bw_restore_directory(saved);
        return 0;
    }

    normalize_slashes(terminal_cwd);
    bw_restore_directory(saved);
    return 1;
}

static int terminal_command_prefix(const char *command, const char *prefix)
{
    while (*prefix != '\0') {
        if (toupper((unsigned char)*command) !=
            toupper((unsigned char)*prefix)) {
            return 0;
        }
        ++command;
        ++prefix;
    }

    return 1;
}

static int terminal_is_space(char c)
{
    return c == ' ' || c == '\t';
}

static int terminal_prepare_command(const char *command,
                                    char *prepared,
                                    size_t prepared_size)
{
    const char *p = command;
    size_t out = 0;
    int is_dir;
    int paged = 0;

    while (terminal_is_space(*p)) {
        ++p;
    }

    is_dir = terminal_command_prefix(p, "DIR") &&
             (p[3] == '\0' || terminal_is_space(p[3]) ||
              p[3] == '/');

    if (!is_dir) {
        copy_text(prepared, prepared_size, command);
        return 0;
    }

    while (*command != '\0' && out + 1 < prepared_size) {
        if (*command == '/' &&
            (command[1] == 'P' || command[1] == 'p') &&
            (command[2] == '\0' || terminal_is_space(command[2]) ||
             command[2] == '/')) {
            paged = 1;
            command += 2;
            while (terminal_is_space(*command)) {
                ++command;
            }
            if (out > 0 && prepared[out - 1] != ' ' &&
                *command != '\0') {
                prepared[out++] = ' ';
            }
            continue;
        }

        prepared[out++] = *command++;
    }

    while (out > 0 && terminal_is_space(prepared[out - 1])) {
        --out;
    }
    prepared[out] = '\0';
    return paged;
}

static void terminal_run_external(const char *command)
{
    char saved[MAX_PATH];
    int old_stdin;
    int old_stdout;
    int old_stderr;
    int null_fd;
    int capture_fd;
    int result;
    int before_output;
    int paged;
    char prepared[TERMINAL_INPUT_LEN];

    if (getcwd(saved, sizeof(saved)) == NULL) {
        terminal_add_line("Unable to read current directory.");
        return;
    }

    if (chdir(terminal_cwd) != 0) {
        terminal_add_line("Unable to enter terminal directory.");
        return;
    }

    paged = terminal_prepare_command(command, prepared, sizeof(prepared));
    before_output = terminal_line_count;

    capture_fd = open(TERMINAL_CAPTURE_FILE,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      0666);
    if (capture_fd < 0) {
        bw_restore_directory(saved);
        terminal_add_line("Unable to capture command output.");
        return;
    }

    null_fd = open("/dev/null", O_RDONLY);
    old_stdin = dup(0);
    old_stdout = dup(1);
    old_stderr = dup(2);
    if (null_fd < 0 || old_stdin < 0 ||
        old_stdout < 0 || old_stderr < 0) {
        if (null_fd >= 0) close(null_fd);
        if (old_stdin >= 0) close(old_stdin);
        if (old_stdout >= 0) close(old_stdout);
        if (old_stderr >= 0) close(old_stderr);
        close(capture_fd);
        bw_restore_directory(saved);
        terminal_add_line("Unable to redirect command I/O.");
        return;
    }

    fflush(stdout);
    fflush(stderr);
    (void)dup2(null_fd, 0);
    (void)dup2(capture_fd, 1);
    (void)dup2(capture_fd, 2);
    close(null_fd);
    close(capture_fd);

    result = system(prepared);

    fflush(stdout);
    fflush(stderr);
    (void)dup2(old_stdin, 0);
    (void)dup2(old_stdout, 1);
    (void)dup2(old_stderr, 2);
    close(old_stdin);
    close(old_stdout);
    close(old_stderr);

    bw_restore_directory(saved);

    terminal_read_capture();

    terminal_paging = 0;
    terminal_page_top = 0;
    terminal_page_end = 0;

    if (paged &&
        terminal_line_count - before_output > terminal_visible_rows()) {
        terminal_paging = 1;
        terminal_page_top = before_output;
        terminal_page_end = terminal_line_count;
    }

    if (result != 0 && terminal_line_count == before_output) {
        char status[48];
        snprintf(status, sizeof(status),
                 "Command returned status %d.", result);
        terminal_add_line(status);
    }
}

static void terminal_store_history(const char *command)
{
    if (*command == '\0') {
        return;
    }

    if (terminal_history_count > 0 &&
        strcmp(terminal_history[terminal_history_count - 1],
               command) == 0) {
        return;
    }

    if (terminal_history_count >= TERMINAL_HISTORY_MAX) {
        memmove(terminal_history[0], terminal_history[1],
                (TERMINAL_HISTORY_MAX - 1) *
                sizeof(terminal_history[0]));
        terminal_history_count = TERMINAL_HISTORY_MAX - 1;
    }

    {
        size_t len = strlen(command);

        if (len >= TERMINAL_INPUT_LEN) {
            len = TERMINAL_INPUT_LEN - 1;
        }

        memcpy(terminal_history[terminal_history_count],
               command, len);
        terminal_history[terminal_history_count][len] = '\0';
    }
    ++terminal_history_count;
}

static void terminal_reset_completion(void)
{
    terminal_completion_count = 0;
    terminal_completion_index = -1;
    terminal_completion_start = 0;
    terminal_completion_end = 0;
    terminal_completion_prefix[0] = '\0';
}

static void terminal_set_input(const char *text)
{
    size_t len = strlen(text);

    if (len >= TERMINAL_INPUT_LEN) {
        len = TERMINAL_INPUT_LEN - 1;
    }

    memcpy(terminal_input, text, len);
    terminal_input[len] = '\0';
    terminal_input_len = (int)len;
    terminal_cursor = terminal_input_len;
    terminal_reset_completion();
}

static int terminal_name_is_executable(const char *name)
{
    char path[MAX_PATH];
    struct stat info;
    return join_path(path, sizeof(path), terminal_cwd, name) &&
           stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

static int terminal_ci_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text == '\0' ||
            toupper((unsigned char)*text) !=
            toupper((unsigned char)*prefix)) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static int terminal_completion_compare(const void *left,
                                       const void *right)
{
    const char *a = (const char *)left;
    const char *b = (const char *)right;
    int a_exec = terminal_name_is_executable(a);
    int b_exec = terminal_name_is_executable(b);

    if (a_exec != b_exec) {
        return b_exec - a_exec;
    }

    while (*a != '\0' && *b != '\0') {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);

        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }

    return toupper((unsigned char)*a) -
           toupper((unsigned char)*b);
}

static int terminal_completion_is_cd(int token_start)
{
    char command[16];
    int i = 0;
    int p = 0;

    while (p < token_start &&
           isspace((unsigned char)terminal_input[p])) {
        ++p;
    }

    while (p < token_start &&
           !isspace((unsigned char)terminal_input[p]) &&
           i < (int)sizeof(command) - 1) {
        command[i++] = terminal_input[p++];
    }
    command[i] = '\0';

    return (terminal_command_prefix(command, "CD") &&
            command[2] == '\0') ||
           (terminal_command_prefix(command, "CHDIR") &&
            command[5] == '\0');
}

static void terminal_build_completion(void)
{
    struct ffblk entry;
    char token[MAX_PATH];
    char typed_path[MAX_PATH];
    char search_dir[MAX_PATH];
    char pattern[MAX_PATH];
    char prefix[MAX_NAME];
    const char *last_slash;
    int token_start = terminal_cursor;
    int token_len;
    int dirs_only;
    int done;

    terminal_reset_completion();

    while (token_start > 0 &&
           !isspace((unsigned char)terminal_input[token_start - 1])) {
        --token_start;
    }

    token_len = terminal_cursor - token_start;
    if (token_len >= (int)sizeof(token)) {
        token_len = (int)sizeof(token) - 1;
    }

    memcpy(token, terminal_input + token_start, (size_t)token_len);
    token[token_len] = '\0';

    last_slash = strrchr(token, '/');
    if (last_slash != NULL) {
        size_t path_len = (size_t)(last_slash - token + 1);
        size_t prefix_len = strlen(last_slash + 1);

        if (path_len >= sizeof(typed_path) ||
            prefix_len >= sizeof(prefix)) {
            return;
        }

        memcpy(typed_path, token, path_len);
        typed_path[path_len] = '\0';
        memcpy(prefix, last_slash + 1, prefix_len + 1);

        if ((path_len >= 2 && token[1] == ':') ||
            token[0] == '/') {
            if (!copy_text(search_dir, sizeof(search_dir), typed_path)) {
                return;
            }
        } else if (!join_path(search_dir, sizeof(search_dir),
                              terminal_cwd, typed_path)) {
            return;
        }
    } else {
        typed_path[0] = '\0';
        if (!copy_text(search_dir, sizeof(search_dir), terminal_cwd) ||
            !copy_text(prefix, sizeof(prefix), token)) {
            return;
        }
    }

    if (!join_path(pattern, sizeof(pattern), search_dir, "*.*")) {
        return;
    }

    dirs_only = terminal_completion_is_cd(token_start);

    done = findfirst(pattern, &entry,
                     FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIREC);
    while (!done && terminal_completion_count < TERMINAL_COMPLETION_MAX) {
        int is_dir = (entry.ff_attrib & FA_DIREC) != 0;

        if (strcmp(entry.ff_name, ".") != 0 &&
            strcmp(entry.ff_name, "..") != 0 &&
            terminal_ci_starts_with(entry.ff_name, prefix) &&
            (!dirs_only || is_dir)) {
            char *dest =
                terminal_completion[terminal_completion_count];
            size_t name_len = strlen(entry.ff_name);

            if (name_len >= MAX_NAME - 2) {
                name_len = MAX_NAME - 2;
            }

            memcpy(dest, entry.ff_name, name_len);
            if (is_dir && name_len + 1 < MAX_NAME) {
                dest[name_len++] = '/';
            }
            dest[name_len] = '\0';
            ++terminal_completion_count;
        }

        done = findnext(&entry);
    }

    if (terminal_completion_count > 1) {
        qsort(terminal_completion,
              (size_t)terminal_completion_count,
              sizeof(terminal_completion[0]),
              terminal_completion_compare);
    }

    terminal_completion_start = token_start;
    terminal_completion_end = terminal_cursor;
    (void)copy_text(terminal_completion_prefix,
                    sizeof(terminal_completion_prefix),
                    typed_path);
}

static void terminal_apply_completion(int direction)
{
    char replacement[MAX_PATH];
    char next[TERMINAL_INPUT_LEN];
    int replacement_len;
    int suffix_len;
    int new_len;

    if (terminal_completion_count == 0) {
        terminal_build_completion();
    }

    if (terminal_completion_count == 0) {
        return;
    }

    if (direction < 0) {
        if (terminal_completion_index < 0) {
            terminal_completion_index = terminal_completion_count - 1;
        } else {
            --terminal_completion_index;
            if (terminal_completion_index < 0) {
                terminal_completion_index =
                    terminal_completion_count - 1;
            }
        }
    } else {
        ++terminal_completion_index;
        if (terminal_completion_index >= terminal_completion_count) {
            terminal_completion_index = 0;
        }
    }

    {
        size_t prefix_len = strlen(terminal_completion_prefix);
        size_t match_len =
            strlen(terminal_completion[terminal_completion_index]);
        size_t copy_match;

        if (prefix_len >= sizeof(replacement)) {
            return;
        }

        memcpy(replacement, terminal_completion_prefix, prefix_len);
        copy_match = sizeof(replacement) - prefix_len - 1;
        if (match_len < copy_match) {
            copy_match = match_len;
        }

        memcpy(replacement + prefix_len,
               terminal_completion[terminal_completion_index],
               copy_match);
        replacement[prefix_len + copy_match] = '\0';
    }

    replacement_len = (int)strlen(replacement);
    suffix_len = terminal_input_len - terminal_completion_end;
    new_len = terminal_completion_start +
              replacement_len + suffix_len;

    if (new_len >= TERMINAL_INPUT_LEN) {
        return;
    }

    memcpy(next, terminal_input,
           (size_t)terminal_completion_start);
    memcpy(next + terminal_completion_start,
           replacement, (size_t)replacement_len);
    memcpy(next + terminal_completion_start + replacement_len,
           terminal_input + terminal_completion_end,
           (size_t)suffix_len + 1);

    memcpy(terminal_input, next, (size_t)new_len + 1);
    terminal_input_len = new_len;
    terminal_cursor = terminal_completion_start + replacement_len;
    terminal_completion_end = terminal_cursor;
}

#include "terminal_session.h"

static void terminal_execute(void)
{
    char command[TERMINAL_INPUT_LEN * 2];
    char prompt[MAX_PATH + TERMINAL_INPUT_LEN * 2 + 2];
    const char *p;

    if (!bw_text_encode(command, sizeof(command), terminal_input)) {
        terminal_add_wrapped_line("Command is too long");
        return;
    }
    snprintf(prompt, sizeof(prompt), "%s>%s", terminal_cwd, command);
    terminal_add_wrapped_line(prompt);
    terminal_store_history(terminal_input);

    terminal_input[0] = '\0';
    terminal_input_len = 0;
    terminal_cursor = 0;
    terminal_history_pos = -1;
    terminal_scroll = 0;
    terminal_paging = 0;
    terminal_page_top = 0;
    terminal_page_end = 0;

    p = command;
    while (isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p == '\0') {
        return;
    }

    if (terminal_command_prefix(p, "CLS") &&
        p[3] == '\0') {
        terminal_line_count = 0;
        return;
    }

    if (terminal_command_prefix(p, "EXIT") &&
        p[4] == '\0') {
        close_terminal();
        return;
    }

    if (terminal_command_prefix(p, "CD") &&
        (p[2] == '\0' || isspace((unsigned char)p[2]) ||
         p[2] == '.' || p[2] == '/')) {
        const char *path = p + 2;

        while (isspace((unsigned char)*path)) {
            ++path;
        }

        if (*path == '\0') {
            terminal_add_line(terminal_cwd);
        } else if (!terminal_change_directory(path)) {
            terminal_add_line("Invalid directory.");
        }
        return;
    }

    if (terminal_command_prefix(p, "CHDIR") &&
        (p[5] == '\0' || isspace((unsigned char)p[5]))) {
        const char *path = p + 5;

        while (isspace((unsigned char)*path)) {
            ++path;
        }

        if (*path == '\0') {
            terminal_add_line(terminal_cwd);
        } else if (!terminal_change_directory(path)) {
            terminal_add_line("Invalid directory.");
        }
        return;
    }

    terminal_run_external(p);
}

static int terminal_visible_rows(void)
{
    int rows = (terminal_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 18) / 9;
    return rows > 1 ? rows - 1 : 1;
}

static void draw_terminal_window(void)
{
    terminal_palette_load();
    int min_x = window_min_button_x(&terminal_window);
    int max_x = window_max_button_x(&terminal_window);
    int close_x = window_close_button_x(&terminal_window);
    int text_x = terminal_window.x + 7;
    int text_y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    int cols = (terminal_window.w - 30) / 6;
    if (session_fd < 0) session_resize();
    int rows = session_fd < 0 ? session_rows - 1 : terminal_visible_rows();
    int start = terminal_paging ?
                terminal_page_top :
                terminal_line_count - rows - terminal_scroll;
    int i;
    char prompt[MAX_PATH + TERMINAL_INPUT_LEN + 2];
    int prompt_len;
    int visible_start = 0;
    int cursor_position;

    if (!terminal_window.open || terminal_window.minimized) {
        return;
    }

    if (start < 0) start = 0;

    draw_window_chrome(&terminal_window);
    draw_text(terminal_window.x + 7, terminal_window.y + 6,
              terminal_title, window_title_text_color(), (terminal_window.w - 64) / 6);
    draw_window_button(min_x, terminal_window.y + WINDOW_BORDER, 0);
    draw_window_button(max_x, terminal_window.y + WINDOW_BORDER, 1);
    draw_window_button(close_x, terminal_window.y + WINDOW_BORDER, 2);

    fill_rect_rgb(terminal_window.x + WINDOW_BORDER,
              terminal_window.y + WINDOW_TITLE_H + 1,
              terminal_window.w - WINDOW_BORDER * 2,
              terminal_window.h - WINDOW_TITLE_H - WINDOW_BORDER - 1,
              budo_palette_rgb(terminal_background));
    draw_bevel(terminal_window.x + WINDOW_BORDER - 1,
               terminal_window.y + WINDOW_TITLE_H,
               terminal_window.w - (WINDOW_BORDER - 1) * 2,
               terminal_window.h - WINDOW_TITLE_H - WINDOW_BORDER + 1, 0);

    if (session_fd >= 0) { session_draw(); return; }
    for (i = 0; i < rows &&
                start + i < terminal_line_count; ++i) {
        for (int col = 0; col < session_cols; ++col) {
            int selected = terminal_cell_selected(start + i, col);
            if (selected) fill_rect(text_x + col * 6, text_y + i * 9, 6, 9, TITLE_COLOR);
            char text[2] = {(char)terminal_selection_char(start + i, col), 0};
            terminal_rgb_text(text_x + col * 6, text_y + i * 9, text,
                budo_palette_rgb(selected ? 4 : terminal_foreground), 1);
        }
    }

    desktop_scroll_draw(APP_TERMINAL, 0);
    if (terminal_paging) {
        terminal_rgb_text(text_x,
                  terminal_window.y + terminal_window.h - 13,
                  "-- More --  Space: page  Enter: line  Esc: quit",
                  budo_palette_rgb(terminal_foreground), cols);
        desktop_file_menu_draw(APP_TERMINAL);
        return;
    }

    snprintf(prompt, sizeof(prompt), "%s>%s",
             terminal_cwd, terminal_input);
    prompt_len = (int)strlen(prompt);
    cursor_position = (int)strlen(terminal_cwd) + 1 + terminal_cursor;

    if (prompt_len > cols) {
        visible_start = cursor_position - cols + 1;
        if (visible_start < 0) visible_start = 0;
        if (visible_start > prompt_len - cols) {
            visible_start = prompt_len - cols;
        }
    }

    for (int col = 0; col < cols && prompt[visible_start + col]; ++col) {
        int selected = terminal_cell_selected(terminal_line_count + session_rows - 1, visible_start + col);
        if (selected) fill_rect(text_x + col * 6, text_y + (session_rows - 1) * 9, 6, 9, TITLE_COLOR);
        char text[2] = {prompt[visible_start + col], 0};
        terminal_rgb_text(text_x + col * 6, text_y + (session_rows - 1) * 9, text,
            budo_palette_rgb(selected ? 4 : terminal_foreground), 1);
    }

    desktop_file_menu_draw(APP_TERMINAL);
}

static void open_terminal(void)
{
    if (terminal_cwd[0] == '\0') {
        if (!copy_text(terminal_cwd, sizeof(terminal_cwd), bw_user_directory())) {
            strcpy(terminal_cwd, "/");
        }
    }

    if (terminal_line_count == 0) {
        terminal_add_line("BUDOSTACK command terminal");
        terminal_add_line("Up/Down: history  PgUp/PgDn: scrollback");
        terminal_add_line("");
    }

    terminal_window.open = 1;
    terminal_window.minimized = 0;
    active_window = APP_TERMINAL;
    if (!session_start()) terminal_add_line("Unable to start BUDOSTACK PTY; using command capture.");
    bw_set_event_filter(session_forward_event);
}

static void minimize_terminal(void)
{
    session_reset_input();
    window_minimize_state(&terminal_window);
    desktop_focus_visible();
}

static void close_terminal(void)
{
    session_stop();
    window_close_state(&terminal_window);
    terminal_paging = 0;
    terminal_scroll = 0;
    desktop_focus_visible();
}

static void toggle_maximize_terminal(void)
{
    window_toggle_maximize_state(&terminal_window);
}

static int terminal_mouse_down(int x, int y)
{
    int frame_action;

    if (!window_contains(&terminal_window, x, y)) {
        return 0;
    }

    active_window = APP_TERMINAL;
    frame_action = window_frame_mouse_down(&terminal_window, x, y);

    if (frame_action == WINDOW_FRAME_MINIMIZE) {
        minimize_terminal();
    } else if (frame_action == WINDOW_FRAME_MAXIMIZE) {
        toggle_maximize_terminal();
    } else if (frame_action == WINDOW_FRAME_CLOSE) {
        close_terminal();
    }

    return 1;
}

static void terminal_history_move(int direction)
{
    if (terminal_history_count == 0) {
        return;
    }

    if (direction < 0) {
        if (terminal_history_pos < 0) {
            terminal_history_pos = terminal_history_count - 1;
        } else if (terminal_history_pos > 0) {
            --terminal_history_pos;
        }
    } else {
        if (terminal_history_pos < 0) {
            return;
        }

        if (terminal_history_pos + 1 < terminal_history_count) {
            ++terminal_history_pos;
        } else {
            terminal_history_pos = -1;
            terminal_set_input("");
            return;
        }
    }

    terminal_set_input(terminal_history[terminal_history_pos]);
}

static int terminal_handle_key(int key)
{
    if (terminal_clipboard_key(key)) return 1;
    if (session_key(key)) return 1;
    if (terminal_paging) {
        int rows = terminal_visible_rows();

        if (key == 27) {
            terminal_paging = 0;
            terminal_scroll = 0;
            return 1;
        }

        if (key == 13) {
            ++terminal_page_top;
        } else if (key == ' ') {
            terminal_page_top += rows;
        } else {
            return 1;
        }

        if (terminal_page_top + rows >= terminal_page_end) {
            terminal_paging = 0;
            terminal_scroll = 0;
        }
        return 1;
    }

    if (key == 9) {
        terminal_apply_completion(1);
        return 1;
    }

    if (key == 0) {
        int extended = getch();
        if (terminal_selection_scan(extended)) return 1;

        if (extended == 201 || extended == 202) {
            terminal_scroll += extended == 201 ? 3 : -3;
            int limit = terminal_line_count - terminal_visible_rows();
            if (limit < 0) limit = 0;
            if (terminal_scroll < 0) terminal_scroll = 0;
            if (terminal_scroll > limit) terminal_scroll = limit;
        } else if (extended == 15) {
            terminal_apply_completion(-1);
        } else if (extended == 72) {
            terminal_reset_completion();
            terminal_history_move(-1);
        } else if (extended == 80) {
            terminal_reset_completion();
            terminal_history_move(1);
        } else if (extended == 75 && terminal_cursor > 0) {
            terminal_reset_completion();
            --terminal_cursor;
        } else if (extended == 77 &&
                   terminal_cursor < terminal_input_len) {
            terminal_reset_completion();
            ++terminal_cursor;
        } else if (extended == 71) {
            terminal_reset_completion();
            terminal_cursor = 0;
        } else if (extended == 79) {
            terminal_reset_completion();
            terminal_cursor = terminal_input_len;
        } else if (extended == 83 &&
                   terminal_cursor < terminal_input_len) {
            terminal_reset_completion();
            memmove(&terminal_input[terminal_cursor],
                    &terminal_input[terminal_cursor + 1],
                    (size_t)(terminal_input_len - terminal_cursor));
            --terminal_input_len;
        } else if (extended == 73) {
            int rows = terminal_visible_rows();
            terminal_reset_completion();
            terminal_scroll += rows;
            if (terminal_scroll > terminal_line_count) {
                terminal_scroll = terminal_line_count;
            }
        } else if (extended == 81) {
            int rows = terminal_visible_rows();
            terminal_reset_completion();
            terminal_scroll -= rows;
            if (terminal_scroll < 0) terminal_scroll = 0;
        }
        return 1;
    }

    if (key == 13) {
        terminal_reset_completion();
        terminal_execute();
        return 1;
    }

    if (key == 8) {
        terminal_reset_completion();
        if (terminal_cursor > 0) {
            memmove(&terminal_input[terminal_cursor - 1],
                    &terminal_input[terminal_cursor],
                    (size_t)(terminal_input_len - terminal_cursor + 1));
            --terminal_cursor;
            --terminal_input_len;
        }
        terminal_history_pos = -1;
        return 1;
    }

    if (key == 27) {
        terminal_set_input("");
        terminal_history_pos = -1;
        return 1;
    }

    if (key >= 32 && key != 127 &&
        terminal_input_len < TERMINAL_INPUT_LEN - 1) {
        terminal_reset_completion();
        memmove(&terminal_input[terminal_cursor + 1],
                &terminal_input[terminal_cursor],
                (size_t)(terminal_input_len - terminal_cursor + 1));
        terminal_input[terminal_cursor] = (char)key;
        ++terminal_cursor;
        ++terminal_input_len;
        terminal_history_pos = -1;
        terminal_scroll = 0;
        return 1;
    }

    return 0;
}

static int bwa_draw_page = 0;

static void bwa_draw_explorer(void)
{
    draw_file_explorer_window(bwa_draw_page);
}

static void bwa_draw_editor(void)
{
    if (editor_window.open && !editor_window.minimized && !editor_search_dialog_active()) {
        int y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H +
                ((editor_writer_mode && editor_writer_ruler) ? EDITOR_RULER_H : 0) + 5;
        bwa_pointer_region(editor_text_x(), y, editor_visible_cols() * 6,
                            editor_visible_rows() * TEXT_CELL_HEIGHT, BUDO_CURSOR_TEXT, NULL);
    }
    draw_editor_window();
}

static int bwa_open_editor_file(const char *path)
{
    if (path == NULL) return 0;
    if (!builtin_new_instance(BWA_HOST_APP_EDITOR)) return 0;
    editor_request_action(2, path);
    return 1;
}

static void bwa_draw_terminal(void)
{
    if (terminal_window.open && !terminal_window.minimized && !terminal_paging)
        bwa_pointer_region(terminal_window.x + 7,
                            terminal_window.y + terminal_window.h - 19,
                            terminal_window.w - 30, 12, BUDO_CURSOR_TEXT, NULL);
    draw_terminal_window();
}

static BwaAppDefinition bwa_builtin_apps[] = {
    {
        BWA_HOST_APP_EXPLORER,
        "explorer",
        "File Explorer",
        BWA_FLAG_NONE,
        {NULL, bwa_draw_explorer, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
        NULL, NULL, NULL
    },
    {
        BWA_HOST_APP_EDITOR,
        "editor",
        "Editor",
        BWA_FLAG_NONE,
        {NULL, bwa_draw_editor, NULL, NULL, NULL, NULL, bwa_open_editor_file, NULL, NULL},
        NULL, NULL, NULL
    },
    {
        BWA_HOST_APP_TERMINAL,
        "terminal",
        "Terminal",
        BWA_FLAG_NONE,
        {NULL, bwa_draw_terminal, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
        NULL, NULL, NULL
    }
};

static int bwa_builtin_app_count(void)
{
    return (int)(sizeof(bwa_builtin_apps) /
                 sizeof(bwa_builtin_apps[0]));
}

static BwaAppDefinition *bwa_find_builtin_app(int runtime_id)
{
    int i;

    for (i = 0; i < bwa_builtin_app_count(); ++i) {
        if (runtime_id > 0 && runtime_id < APP_BWA_BASE &&
            bwa_builtin_apps[i].runtime_id == (runtime_id - 1) % 3 + 1) {
            return &bwa_builtin_apps[i];
        }
    }

    return NULL;
}

static void bwa_draw_builtin_app(int runtime_id)
{
    BwaAppDefinition *app = bwa_find_builtin_app(runtime_id);

    if (app != NULL && app->callbacks.draw != NULL) {
        ui_draw_owner = runtime_id;
        app->callbacks.draw();
        ui_draw_owner = APP_NONE;
        ui_draw_scope = 0;
    }
}

static int bwa_open_reader_file(const char *path)
{
    if (!path || !builtin_new_instance(BWA_HOST_APP_READER)) return 0;
    if (editor_load_file(path)) return 1;
    editor_close_now();
    return 0;
}

static int bwa_launch_host_app(int app_id)
{
    if (app_id == BWA_HOST_APP_HELP) return bwa_open_reader_file("../../documents/help.txt");
    return builtin_new_instance(app_id);
}

static unsigned char bwa_get_system_color(int role)
{
    return role >= 0 && role < BUDO_SYS_COLOR_COUNT ? ui_color_indices[role] : TEXT_COLOR;
}

static int bwa_get_system_metric(int metric)
{
    switch (metric) {
        case BUDO_SYS_METRIC_WINDOW_BORDER:  return WINDOW_BORDER;
        case BUDO_SYS_METRIC_TITLE_HEIGHT:   return WINDOW_TITLE_H;
        case BUDO_SYS_METRIC_TITLE_TEXT_Y:   return 6;
        case BUDO_SYS_METRIC_TITLE_BUTTON_W: return 14;
        case BUDO_SYS_METRIC_TITLE_BUTTON_H: return 14;
        case BUDO_SYS_METRIC_MENU_HEIGHT:    return EDITOR_MENU_H;
        case BUDO_SYS_METRIC_STATUS_HEIGHT:  return EDITOR_STATUS_H;
        case BUDO_SYS_METRIC_CHAR_WIDTH:     return TEXT_CELL_WIDTH;
        case BUDO_SYS_METRIC_LINE_HEIGHT:    return TEXT_CELL_HEIGHT;
        default:                             return 0;
    }
}

static void bwa_draw_standard_window(int x, int y, int w, int h,
                                     const char *title,
                                     unsigned int button_flags)
{
    AppWindow window;
    int button_x;

    memset(&window, 0, sizeof(window));
    window.x = x;
    window.y = y;
    window.w = w;
    window.h = h;
    window.open = 1;
    BwaLoadedApp *owner = bwa_find_external_app(ui_draw_owner);
    if (owner) window.maximized = owner->window.maximized;

    draw_window_chrome(&window);
    if (title != NULL) {
        draw_text(x + 7, y + 6, title, window_title_text_color(),
                  (w - 62) / 6);
    }

    button_x = x + w - WINDOW_BORDER - 14;
    if (button_flags & BUDO_WINDOW_BUTTON_CLOSE) {
        draw_window_button(button_x, y + WINDOW_BORDER, 2);
        button_x -= 16;
    }
    if (button_flags & BUDO_WINDOW_BUTTON_MAXIMIZE) {
        draw_window_button(button_x, y + WINDOW_BORDER, 1);
        button_x -= 16;
    }
    if (button_flags & BUDO_WINDOW_BUTTON_MINIMIZE) {
        draw_window_button(button_x, y + WINDOW_BORDER, 0);
    }
}

static void bwa_draw_button_state(int x, int y, int w, int h,
                                  const char *label, unsigned int state)
{
    int command = label && label[0] && !(state & BUDO_BUTTON_IMMEDIATE);
    int hover = point_in_rect(ui_pointer_x, ui_pointer_y, x, y, w, h) &&
                (ui_draw_scope == ui_scope() &&
                 (ui_draw_scope || ui_draw_owner == desktop_point_owner(ui_pointer_x, ui_pointer_y)));
    int disabled = (state & BUDO_BUTTON_DISABLED) != 0;
    UiRegion candidate = {.x=x, .y=y, .w=w, .h=h, .owner=ui_draw_owner, .scope=ui_draw_scope};
    snprintf(candidate.tip, sizeof(candidate.tip), "%s", label ? label : "");
    int depressed = !disabled && ((state & BUDO_BUTTON_PRESSED) != 0 ||
                    (hover && ui_captured && ui_same_control(&candidate, &ui_capture)));
    hover = hover && label && label[0];
    unsigned char face = disabled ? 3U : depressed ? CONTROL_PRESSED_COLOR :
                         hover ? CONTROL_HOVER_COLOR : WINDOW_CHROME_COLOR;
    unsigned char ink = !disabled && (depressed || hover) ? TITLE_TEXT_COLOR : TEXT_COLOR;
    if (label && label[0]) ui_register(x, y, w, h, BUDO_CURSOR_ARROW,
                                      label, command, state);
    fill_rect(x, y, w, h, face);
    if (disabled) draw_rect(x, y, w, h, WINDOW_SHADOW_COLOR);
    else draw_bevel(x, y, w, h, !depressed);
    if (state & BUDO_BUTTON_DEFAULT) {
        draw_rect(x + 1, y + 1, w - 2, h - 2, ink);
        draw_rect(x + 2, y + 2, w - 4, h - 4, ink);
    }
    if (state & BUDO_BUTTON_FOCUSED) {
        for (int i = 3; i < w - 3; i += 2) {
            put_pixel(x + i, y + 3, ink);
            put_pixel(x + i, y + h - 4, ink);
        }
        for (int i = 3; i < h - 3; i += 2) {
            put_pixel(x + 3, y + i, ink);
            put_pixel(x + w - 4, y + i, ink);
        }
    }
    if (!label) return;
    int cols = w > 6 ? (w - 6) / 6 : 0;
    int shown = (int)strlen(label);
    if (shown > cols) shown = cols;
    draw_text(x + (w - shown * 6) / 2 + depressed,
              y + (h - 7) / 2 + depressed, label,
              ink, cols);
}

static void bwa_draw_standard_button(int x, int y, int w, int h,
                                     const char *label, int pressed)
{
    bwa_draw_button_state(x, y, w, h, label, pressed ? BUDO_BUTTON_PRESSED : 0U);
}

static void bwa_draw_sunken_panel(int x, int y, int w, int h,
                                  unsigned char fill_color)
{
    fill_rect(x, y, w, h, fill_color);
    draw_bevel(x, y, w, h, 0);
}

static int bwa_window_create(int x, int y, int w, int h,
                             const char *title,
                             unsigned int button_flags)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL || w < 120 || h < 80) return 0;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w < 120 || h < 80) return 0;

    memset(&app->window, 0, sizeof(app->window));
    app->window.x = x;
    app->window.y = y;
    app->window.w = w;
    app->window.h = h;
    app->window.restore_x = x;
    app->window.restore_y = y;
    app->window.restore_w = w;
    app->window.restore_h = h;
    app->window.open = 1;
    app->managed_window = 1;
    app->window_buttons = button_flags;
    app->window_min_w = 120;
    app->window_min_h = 80;

    snprintf(app->window_title, sizeof(app->window_title), "%s",
             title != NULL ? title : app->definition.name);
    return 1;
}

static int bwa_window_get_rect(int *x, int *y, int *w, int *h)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL || !app->managed_window) return 0;
    if (x != NULL) *x = app->window.x;
    if (y != NULL) *y = app->window.y;
    if (w != NULL) *w = app->window.w;
    if (h != NULL) *h = app->window.h;
    return 1;
}

static int bwa_window_get_client_rect(int *x, int *y, int *w, int *h)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL || !app->managed_window) return 0;
    if (x != NULL) *x = app->window.x + WINDOW_BORDER;
    if (y != NULL) *y = app->window.y + WINDOW_TITLE_H + 1;
    if (w != NULL) *w = app->window.w - WINDOW_BORDER * 2;
    if (h != NULL) *h = app->window.h - WINDOW_TITLE_H -
                         WINDOW_BORDER - 1;
    return 1;
}

static int bwa_window_is_maximized(void)
{
    BwaLoadedApp *app = bwa_callback_app;

    return app != NULL && app->managed_window ?
           app->window.maximized : 0;
}

static int bwa_window_close(void)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL) return 0;
    if (app->definition.request_close && !app->definition.request_close()) return 0;
    if (app->definition.callbacks.close) app->definition.callbacks.close();

    app->open = 0;
    if (app->managed_window) {
        window_close_state(&app->window);
    }

    desktop_focus_visible();
    return 1;
}

static int bwa_window_set_min_size(int min_w, int min_h)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL || !app->managed_window ||
        min_w < 120 || min_h < 80) {
        return 0;
    }

    app->window_min_w = min_w;
    app->window_min_h = min_h;
    if (app->window.w < min_w) app->window.w = min_w;
    if (app->window.h < min_h) app->window.h = min_h;
    return 1;
}

static int bwa_window_set_title(const char *title)
{
    BwaLoadedApp *app = bwa_callback_app;

    if (app == NULL || !app->managed_window || title == NULL) {
        return 0;
    }

    snprintf(app->window_title, sizeof(app->window_title), "%s", title);
    return 1;
}

static int bwa_file_read_all_impl(const char *path, unsigned char *buffer, unsigned int capacity, unsigned int *size_out);
static int bwa_file_read_all(const char *path, unsigned char *buffer, unsigned int capacity, unsigned int *size_out)
{
    int previous = ui_busy_begin();
    int result = bwa_file_read_all_impl(path, buffer, capacity, size_out);
    ui_busy_end(previous);
    return result;
}

static int bwa_file_read_all_impl(const char *path,
                             unsigned char *buffer,
                             unsigned int capacity,
                             unsigned int *size_out)
{
    FILE *file;
    size_t size;

    if (path == NULL || buffer == NULL || size_out == NULL ||
        capacity == 0U) {
        return 0;
    }

    char user_path[MAX_PATH];
    if (!bw_user_path(user_path, sizeof(user_path), path)) return 0;
    file = fopen(user_path, "rb");
    if (file == NULL) return 0;

    size = fread(buffer, 1, capacity, file);
    if (ferror(file) || (size == capacity && fgetc(file) != EOF)) {
        fclose(file);
        return 0;
    }

    fclose(file);
    *size_out = (unsigned int)size;
    return 1;
}

static int bwa_file_write_all_impl(const char *path, const unsigned char *buffer, unsigned int size);
static int bwa_file_write_all(const char *path, const unsigned char *buffer, unsigned int size)
{
    int previous = ui_busy_begin();
    int result = bwa_file_write_all_impl(path, buffer, size);
    ui_busy_end(previous);
    return result;
}

static int bwa_file_write_all_impl(const char *path,
                              const unsigned char *buffer,
                              unsigned int size)
{
    FILE *file;
    size_t written;

    if (path == NULL || buffer == NULL) return 0;

    char user_path[MAX_PATH];
    if (!bw_user_path(user_path, sizeof(user_path), path)) return 0;
    char temporary[MAX_PATH];
    file = desktop_file_open(user_path, temporary);
    if (file == NULL) return 0;

    written = fwrite(buffer, 1, size, file);
    if (written != size || ferror(file)) {
        fclose(file);
        unlink(temporary);
        return 0;
    }

    return desktop_file_finish(file, user_path, temporary);
}

static void *bwa_memory_alloc(unsigned int size)
{
    if (size == 0U) return NULL;
    return malloc((size_t)size);
}

static void bwa_memory_free(void *ptr)
{
    free(ptr);
}

static const BwaHostApi bwa_host_api = {
    BWA_ABI_MAJOR,
    BWA_ABI_MINOR,
    fill_rect,
    draw_rect,
    draw_text,
    point_in_rect,
    bwa_launch_host_app,
    bwa_get_file_association_count,
    bwa_get_file_association,
    bwa_set_file_association,
    bwa_get_file_app_count,
    bwa_get_file_app,
    bwa_close_active_app,
    bwa_get_system_color,
    bwa_get_system_metric,
    bwa_draw_standard_window,
    bwa_draw_standard_button,
    bwa_draw_sunken_panel,
    bwa_window_create,
    bwa_window_get_rect,
    bwa_window_get_client_rect,
    bwa_window_is_maximized,
    bwa_window_close,
    bwa_window_set_min_size,
    bwa_window_set_title,
    bwa_file_read_all,
    bwa_file_write_all,
    bwa_memory_alloc,
    bwa_memory_free,
    fill_rect_rgb,
    bwa_file_dialog,
    bwa_confirm_dialog,
    mouse_get_state,
    keyboard_modifiers,
    bwa_draw_button_state,
    bwa_pointer_region,
    bwa_get_time_ms,
    bw_get_keyboard_layout,
    bw_set_keyboard_layout,
    bwa_delete_file_association,
    bwa_open_reader_file,
    bwa_set_system_color,
    bwa_reset_system_colors
};

static BwaLoadedApp *bwa_find_external_app(int runtime_id)
{
    int i;

    for (i = 0; i < bwa_instance_count; ++i) {
        if (bwa_instances[i].definition.runtime_id == runtime_id) {
            return &bwa_instances[i];
        }
    }

    return NULL;
}

static int bwa_has_external_app_id(const char *app_id)
{
    int i;

    for (i = 0; i < bwa_external_app_count; ++i) {
        const char *id = bwa_external_apps[i].definition.app_id;

        if (id != NULL && strcmp(id, app_id) == 0) {
            return 1;
        }
    }

    return 0;
}

static BwaLoadedApp *bwa_find_external_app_id(const char *app_id)
{
    int i;

    if (app_id == NULL) return NULL;

    for (i = 0; i < bwa_external_app_count; ++i) {
        const char *id = bwa_external_apps[i].definition.app_id;

        if (id != NULL && strcmp(id, app_id) == 0) {
            return &bwa_external_apps[i];
        }
    }

    return NULL;
}

static int bwa_get_file_app_count(void)
{
    int count = 1;
    int i;

    for (i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];

        if (app->definition.callbacks.open_file != NULL &&
            strcmp(app->definition.app_id, "editor") != 0) {
            ++count;
        }
    }

    return count;
}

static int bwa_get_file_app(int index,
                            char *app_id,
                            unsigned int app_id_size,
                            char *name,
                            unsigned int name_size)
{
    int i;
    int current = 1;

    if (index < 0 || app_id == NULL || app_id_size == 0 ||
        name == NULL || name_size == 0) {
        return 0;
    }

    if (index == 0) {
        snprintf(app_id, app_id_size, "%s", "editor");
        snprintf(name, name_size, "%s", "Editor");
        return 1;
    }

    for (i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];

        if (app->definition.callbacks.open_file != NULL &&
            strcmp(app->definition.app_id, "editor") != 0) {
            if (current == index) {
                snprintf(app_id, app_id_size, "%s",
                         app->definition.app_id);
                snprintf(name, name_size, "%s",
                         app->definition.name);
                return 1;
            }
            ++current;
        }
    }

    return 0;
}

static int bwa_close_active_app(void)
{
    BwaLoadedApp *app = bwa_find_external_app(active_window);
    BwaLoadedApp *previous = bwa_callback_app;
    int closed;
    if (app == NULL) return 0;
    bwa_callback_app = app;
    closed = bwa_window_close();
    bwa_callback_app = previous;
    return closed;
}

static int open_associated_file(const char *path)
{
    const char *dot;
    int index;
    const char *app_id;
    BwaLoadedApp *app;

    if (path == NULL) return 0;

    dot = strrchr(path, '.');
    if (dot == NULL) return 0;

    index = find_file_association(dot);
    if (index < 0) return 0;

    app_id = file_associations[index].app_id;
    if (app_id[0] == '\0') return 0;

    if (strcmp(app_id, "editor") == 0) {
        return bwa_open_editor_file(path);
    }
    if (strcmp(app_id, "reader") == 0) return bwa_open_reader_file(path);

    app = bwa_find_external_app_id(app_id);
    if (app == NULL || app->definition.callbacks.open_file == NULL) {
        return 0;
    }

    app = bwa_new_instance(app);
    if (!app) return 0;
    bwa_callback_app = app;
    if (!app->definition.callbacks.open_file(path)) {
        if (app->definition.callbacks.close) app->definition.callbacks.close();
        bwa_callback_app = NULL;
        return 0;
    }
    bwa_callback_app = NULL;

    app->open = 1;
    if (app->managed_window) {
        app->window.open = 1;
        app->window.minimized = 0;
    }
    active_window = app->definition.runtime_id;
    return 1;
}

static void bwa_log_load_failure(const char *path,
                                 const char *stage,
                                 const char *detail)
{
    FILE *file = fopen(bw_state_file("modules.log"), "at");

    if (file == NULL) return;

    fprintf(file, "%s | %s", path != NULL ? path : "(null)",
            stage != NULL ? stage : "(unknown)");
    if (detail != NULL && detail[0] != '\0') {
        fprintf(file, " | %s", detail);
    }
    fputc('\n', file);
    fclose(file);
}

static int bwa_register_external(const char *path)
{
    void *handle;
    union {
        void *object;
        BwaEntryPoint entry;
    } symbol;
    BwaAppDefinition definition;
    BwaLoadedApp *loaded;

    if (bwa_external_app_count >= BWA_MAX_EXTERNAL) {
        return 0;
    }

    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        const char *error = dlerror();
        bwa_log_load_failure(path, "dlopen",
                             error != NULL ? error : "unknown error");
        return 0;
    }

    symbol.object = dlsym(handle, "bwa_entry");
    if (symbol.object == NULL) {
        const char *error = dlerror();
        bwa_log_load_failure(path, "dlsym",
                             error != NULL ? error : "missing _bwa_entry");
        (void)dlclose(handle);
        return 0;
    }

    memset(&definition, 0, sizeof(definition));
    if (!symbol.entry(&bwa_host_api, &definition)) {
        bwa_log_load_failure(path, "bwa_entry",
                             "application rejected host ABI or initialization");
        (void)dlclose(handle);
        return 0;
    }
    if (definition.app_id == NULL ||
        definition.name == NULL ||
        (definition.callbacks.open == NULL &&
         definition.callbacks.draw == NULL)) {
        bwa_log_load_failure(path, "definition",
                             "invalid application definition");
        (void)dlclose(handle);
        return 0;
    }

    loaded = &bwa_external_apps[bwa_external_app_count];
    memset(loaded, 0, sizeof(*loaded));
    loaded->handle = handle;
    loaded->definition = definition;
    loaded->definition.runtime_id =
        APP_BWA_BASE + bwa_external_app_count;
    loaded->desktop_slot = 2 + bwa_external_app_count;
    loaded->open = 0;
    if (!copy_text(loaded->path, sizeof(loaded->path), path)) {
        (void)dlclose(handle);
        memset(loaded, 0, sizeof(*loaded));
        return 0;
    }
    ++bwa_external_app_count;
    return 1;
}

/* dlopen of the same pathname shares module globals. A private image gives
 * each legacy callback-based BWA its own data segment without changing ABI. */
static BwaLoadedApp *bwa_new_instance(BwaLoadedApp *program)
{
    int slot;
    for (slot = 0; slot < bwa_instance_count; ++slot)
        if (!bwa_instances[slot].open) break;
    if (slot == BWA_MAX_INSTANCES) {
        fprintf(stderr, "BUDOWIN: native window limit reached\n");
        return NULL;
    }
    BwaLoadedApp *app = &bwa_instances[slot];
    if (app->handle) {
        (void)dlclose(app->handle);
        memset(app, 0, sizeof(*app));
    }
    char temporary[MAX_PATH];
    if (!copy_text(temporary, sizeof(temporary), bw_state_file("instance-XXXXXX"))) return NULL;
    int output = mkstemp(temporary);
    int input = open(program->path, O_RDONLY);
    int ok = output >= 0 && input >= 0;
    unsigned char buffer[16384];
    while (ok) {
        ssize_t n = read(input, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = 0; break; }
        if (!n) break;
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t written = write(output, buffer + offset, (size_t)(n - offset));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = 0; break; }
            offset += written;
        }
    }
    if (!ok) perror("BUDOWIN: private module image");
    if (input >= 0) close(input);
    if (output >= 0 && close(output) != 0) ok = 0;
    void *handle = ok ? dlopen(temporary, RTLD_NOW | RTLD_LOCAL) : NULL;
    if (output >= 0 && unlink(temporary) != 0) perror("BUDOWIN: remove module image");
    if (!handle) {
        bwa_log_load_failure(program->path, "instance", ok ? dlerror() : "copy failed");
        return NULL;
    }
    union { void *object; BwaEntryPoint entry; } symbol;
    symbol.object = dlsym(handle, "bwa_entry");
    BwaAppDefinition definition;
    memset(&definition, 0, sizeof(definition));
    if (!symbol.object || !symbol.entry(&bwa_host_api, &definition) ||
        !definition.app_id || !definition.name) {
        bwa_log_load_failure(program->path, "instance", "invalid entry or definition");
        dlclose(handle);
        return NULL;
    }
    app->handle = handle;
    app->definition = definition;
    app->definition.runtime_id = APP_BWA_BASE + BWA_MAX_EXTERNAL + slot;
    snprintf(app->path, sizeof(app->path), "%s", program->path);
    if (slot == bwa_instance_count) ++bwa_instance_count;
    return app;
}

static void bwa_load_external_apps(void)
{
    char apps_path[MAX_PATH];
    char pattern[MAX_PATH];
    struct ffblk entry;
    int done;

    bwa_external_app_count = 0;
    memset(bwa_external_apps, 0, sizeof(bwa_external_apps));

    if (!join_path(apps_path, sizeof(apps_path),
                   home_path, "APPS") ||
        !join_path(pattern, sizeof(pattern),
                   apps_path, "*.BWA")) {
        return;
    }

    done = findfirst(pattern, &entry, FA_RDONLY | FA_HIDDEN | FA_SYSTEM);
    while (!done && bwa_external_app_count < BWA_MAX_EXTERNAL) {
        char full_path[MAX_PATH];

        if (!(entry.ff_attrib & FA_DIREC) &&
            join_path(full_path, sizeof(full_path),
                      apps_path, entry.ff_name)) {
            (void)bwa_register_external(full_path);
        }

        done = findnext(&entry);
    }
}

static void draw_bwa_app_icon(int x, int y, BwaLoadedApp *app)
{
    char label[14];
    int len;
    int center_x = x + DESKTOP_ICON_W / 2;
    int label_width;

    snprintf(label, sizeof(label), "%.13s", app->definition.name);
    len = (int)strlen(label);
    label_width = len > 0 ? len * 6 - 1 : 0;

    if (app->pcx_icon_loaded) {
        draw_pcx_icon(x, y, app->pcx_icon);
        draw_text(center_x - label_width / 2,
                  y + DESKTOP_ICON_H + 3,
                  label, DESKTOP_TEXT_COLOR, len);
        return;
    }

    if (app->definition.callbacks.draw_icon != NULL) {
        app->definition.callbacks.draw_icon(x, y);
        center_x = x + ICON_W / 2;
        draw_text(center_x - label_width / 2,
                  y + DESKTOP_ICON_H + 3,
                  label, DESKTOP_TEXT_COLOR, len);
        return;
    }

    center_x = x + ICON_W / 2;
    draw_rect(x + 3, y + 2, ICON_W - 6, ICON_H - 5, DESKTOP_TEXT_COLOR);
    draw_rect(x + 6, y + 5, ICON_W - 12, ICON_H - 11, FOLDER_DARK);
    draw_text(center_x - label_width / 2,
              y + DESKTOP_ICON_H + 3, label, DESKTOP_TEXT_COLOR, len);
}

static void bwa_draw_external_app(BwaLoadedApp *app)
{
    if (app == NULL || !app->open ||
        (app->definition.flags & BWA_FLAG_LAUNCHER)) {
        return;
    }

    if (app->managed_window && app->window.minimized) return;
    ui_draw_owner = app->definition.runtime_id;
    if (app->managed_window) {
        bwa_draw_standard_window(app->window.x, app->window.y,
                                 app->window.w, app->window.h,
                                 app->window_title,
                                 app->window_buttons);
    }

    if (app->definition.callbacks.draw != NULL) {
        bwa_callback_app = app;
        app->definition.callbacks.draw();
        bwa_callback_app = NULL;
    }
    ui_draw_owner = APP_NONE;
}

static int desktop_running_apps(int *ids, AppWindow **windows, const char **names)
{
    int count = 0;
    const char *labels[] = {"File Explorer", "Editor", "Terminal"};
    for (int id = 1; id <= BUILTIN_INSTANCE_MAX * 3; ++id) {
        AppWindow *window = builtin_window(id);
        if (!window || !window->open) continue;
        ids[count] = id;
        windows[count] = window;
        int kind = (id - 1) % 3, slot = (id - 1) / 3;
        names[count++] = kind == 2 ? terminal_instances[slot]->v_terminal_title :
                        kind == 1 && editor_instances[slot]->v_editor_read_only ? "Reader" :
                        kind == 0 && explorer_instances[slot]->v_recycle_bin ? "Recycle Bin" : labels[kind];
    }
    for (int i = 0; i < bwa_instance_count; ++i) {
        BwaLoadedApp *app = &bwa_instances[i];
        if (!app->open) continue;
        ids[count] = app->definition.runtime_id;
        windows[count] = &app->window;
        names[count++] = app->definition.name;
    }
    return count;
}

#define DESKTOP_RUNNING_MAX (BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES)
static unsigned long desktop_z_order[APP_BWA_BASE + BWA_MAX_EXTERNAL + BWA_MAX_INSTANCES];
static unsigned long desktop_z_sequence;
static int desktop_last_active;

static int desktop_window_order(int *ids, AppWindow **windows, const char **names)
{
    if (active_window != desktop_last_active) {
        if (active_window > 0 && active_window < APP_BWA_BASE && (active_window - 1) % 3 == 0) {
            builtin_select(active_window);
            if (recycle_bin) { (void)recycle_load(); explorer_state->v_page = 0; }
        }
        if (desktop_last_active > 0 && desktop_last_active < APP_BWA_BASE &&
            (desktop_last_active - 1) % 3 == 2 && builtin_window(desktop_last_active)) {
            TerminalState *saved = terminal_state;
            int saved_slot = terminal_instance_slot;
            builtin_select(desktop_last_active);
            session_reset_input();
            terminal_state = saved;
            terminal_instance_slot = saved_slot;
        }
        if (active_window > 0 && active_window < (int)(sizeof(desktop_z_order) / sizeof(desktop_z_order[0])))
            desktop_z_order[active_window] = ++desktop_z_sequence;
        desktop_last_active = active_window;
    }
    int count = desktop_running_apps(ids, windows, names);
    for (int i = 1; i < count; ++i) {
        int id = ids[i], j = i;
        AppWindow *window = windows[i];
        const char *name = names[i];
        while (j > 0 && desktop_z_order[ids[j - 1]] > desktop_z_order[id]) {
            ids[j] = ids[j - 1];
            windows[j] = windows[j - 1];
            names[j] = names[j - 1];
            --j;
        }
        ids[j] = id;
        windows[j] = window;
        names[j] = name;
    }
    return count;
}

static void desktop_focus_visible(void)
{
    int ids[DESKTOP_RUNNING_MAX];
    AppWindow *windows[DESKTOP_RUNNING_MAX];
    const char *names[DESKTOP_RUNNING_MAX];
    if (active_window == APP_NONE) return;
    int count = desktop_window_order(ids, windows, names);
    for (int i = 0; i < count; ++i)
        if (ids[i] == active_window && !windows[i]->minimized) return;
    active_window = APP_NONE;
    for (int i = count - 1; i >= 0; --i) {
        if (windows[i]->minimized) continue;
        active_window = ids[i];
        builtin_select(active_window);
        break;
    }
}

static void desktop_switch_app(int reverse)
{
    int ids[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    AppWindow *windows[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    const char *names[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    int count = desktop_running_apps(ids, windows, names), current = -1;
    if (!count) return;
    for (int i = 0; i < count; ++i) if (ids[i] == active_window) current = i;
    current = (current + (reverse ? count - 1 : 1)) % count;
    windows[current]->minimized = 0;
    active_window = ids[current];
    builtin_select(active_window);
    app_switch_until = bw_clock() + CLOCKS_PER_SEC * 2;
}

static int desktop_switch_key(int key, unsigned int modifiers)
{
    if (key != 9 || !(modifiers & KEYMOD_ALT)) return 0;
    desktop_switch_app((modifiers & KEYMOD_SHIFT) != 0);
    return 1;
}

static int desktop_minimized_click(int x, int y)
{
    int ids[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    AppWindow *windows[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    const char *names[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    int count = desktop_running_apps(ids, windows, names), slot = 0;
    for (int i = 0; i < count; ++i) {
        if (!windows[i]->minimized) continue;
        int bx = 4 + (slot % 5) * 126, by = SCREEN_HEIGHT - 20 - (slot / 5) * 22;
        ++slot;
        if (point_in_rect(x, y, bx, by, 122, 20)) {
            windows[i]->minimized = 0;
            active_window = ids[i];
            builtin_select(active_window);
            return 1;
        }
    }
    return 0;
}

static void desktop_running_draw(void)
{
    int ids[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    AppWindow *windows[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    const char *names[BUILTIN_INSTANCE_MAX * 3 + BWA_MAX_INSTANCES];
    int count = desktop_running_apps(ids, windows, names), slot = 0;
    for (int i = 0; i < count; ++i) {
        if (!windows[i]->minimized) continue;
        int x = 4 + (slot % 5) * 126, y = SCREEN_HEIGHT - 20 - (slot / 5) * 22;
        ++slot;
        int owner = ui_draw_owner;
        ui_draw_owner = UI_OVERLAY_OWNER;
        bwa_draw_standard_button(x, y, 122, 20, "", 0);
        ui_draw_owner = owner;
        draw_text_elided(x + 4, y + 6, names[i], TEXT_COLOR, 19);
    }
    if (app_switch_until && count) {
        int shown = count < 22 ? count : 22;
        int selected = 0;
        for (int i = 0; i < count; ++i) if (ids[i] == active_window) selected = i;
        int first = selected - shown / 2;
        if (first < 0) first = 0;
        if (first + shown > count) first = count - shown;
        int height = 26 + shown * 18, y = (SCREEN_HEIGHT - height) / 2;
        fill_rect(208, y, 224, height, WINDOW_CHROME_COLOR);
        draw_bevel(208, y, 224, height, 1);
        draw_text(218, y + 7, "Running applications", TEXT_COLOR, 32);
        for (int i = first; i < first + shown; ++i) {
            int row = i - first;
            if (ids[i] == active_window) fill_rect(214, y + 22 + row * 18, 212, 18, TITLE_COLOR);
            draw_text_elided(220, y + 27 + row * 18, names[i], ids[i] == active_window ? TITLE_TEXT_COLOR : TEXT_COLOR, 33);
        }
    }
}

static void draw_desktop(int page)
{
    int explorer_x;
    int explorer_y;
    int editor_x;
    int editor_y;

    ui_region_count = 0;
    ui_draw_scope = 0;
    ui_draw_owner = APP_NONE;
    if (desktop_background_loaded) {
        memcpy(framebuffer, desktop_background, sizeof(framebuffer));
        memcpy(rgb_framebuffer, desktop_background_rgb, sizeof(rgb_framebuffer));
        memset(rgb_mask, 1, sizeof(rgb_mask));
    } else {
        memset(framebuffer, DESKTOP_COLOR, sizeof(framebuffer));
        memset(rgb_mask, 0, sizeof(rgb_mask));
    }
    fill_rect(0, 0, SCREEN_WIDTH, 21, WINDOW_CHROME_COLOR);
    fill_rect(0, 20, SCREEN_WIDTH, 1, WINDOW_SHADOW_COLOR);
    draw_text_centered(7, "BUDOWIN by BUDOSTACK", TEXT_COLOR);
    time_t now = time(NULL);
    struct tm date;
    if (localtime_r(&now, &date)) {
        char clock_text[32];
        static const char *weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        strftime(clock_text, sizeof(clock_text), "%Y-%m-%d %H:%M:%S", &date);
        draw_text(6, 7, weekdays[date.tm_wday], TEXT_COLOR, 9);
        draw_text(SCREEN_WIDTH - 120, 7, clock_text, TEXT_COLOR, 19);
    }
    if (desktop_folder[0]) {
        const char *name = strrchr(desktop_folder, '/');
        draw_text(110, 28, name ? name + 1 : desktop_folder, DESKTOP_TEXT_COLOR, 70);
        bwa_draw_standard_button(12, 24, 84, 18, "Up / Desktop", 0);
    }

    for (int i = 0; i < shortcut_count; ++i) {
        if (strcmp(shortcut_details[i].parent, desktop_folder)) continue;
        int x, y;
        desktop_slot_position(shortcut_slots[i], &x, &y);
        if (desktop_icon_dragging && desktop_drag_slots[DESKTOP_SHORTCUT_BASE + i] >= 0) {
            x += desktop_drag_offset_x;
            y += desktop_drag_offset_y;
        }
        if (shortcut_details[i].icon_loaded) {
            budo_selection_icon_draw(&bwa_host_api, x, y, DESKTOP_ICON_W, DESKTOP_ICON_H, shortcut_selection[i], 0);
            for (int row = 0; row < DESKTOP_ICON_H; ++row)
                for (int col = 0; col < DESKTOP_ICON_W; ++col) {
                    int pixel = row * DESKTOP_ICON_W + col;
                    if (shortcut_details[i].pixels[pixel] != PCX_TRANSPARENT)
                        put_rgb_pixel(x + col, y + row, shortcut_details[i].rgb[pixel]);
                }
            int chars = (int)strlen(desktop_shortcuts[i].name);
            if (chars > 12) chars = 12;
            draw_text(x + 16 - (chars * 6 - 1) / 2, y + DESKTOP_ICON_H + 2,
                      desktop_shortcuts[i].name, DESKTOP_TEXT_COLOR, chars);
            bwa_pointer_region(x - 20, y - 2, 72, 50, BUDO_CURSOR_ARROW, desktop_shortcuts[i].name);
        } else {
            draw_labeled_icon_color(x, y, desktop_shortcuts[i].name, desktop_shortcuts[i].type,
                              shortcut_selection[i], desktop_shortcuts[i].path,
                              DESKTOP_TEXT_COLOR);
        }
        if (desktop_shortcuts[i].type == TYPE_FILE) draw_text(x, y + 16, "->", TEXT_COLOR, 2);
        budo_selection_icon_draw(&bwa_host_api, x, y, DESKTOP_ICON_W,
                                 DESKTOP_ICON_H, 0,
                                 desktop_select.focus == DESKTOP_SHORTCUT_BASE + i);
    }
    desktop_slot_position(explorer_desktop_slot, &explorer_x, &explorer_y);
    desktop_slot_position(editor_desktop_slot, &editor_x, &editor_y);

    if (desktop_icon_dragging && desktop_drag_slots[0] >= 0) {
        explorer_x += desktop_drag_offset_x;
        explorer_y += desktop_drag_offset_y;
    }
    if (desktop_icon_dragging && desktop_drag_slots[1] >= 0) {
        editor_x += desktop_drag_offset_x;
        editor_y += desktop_drag_offset_y;
    }

    if (!desktop_folder[0] && !bwa_has_external_app_id("explorer") &&
        1) {
        budo_selection_icon_draw(&bwa_host_api, explorer_x, explorer_y,
                                 DESKTOP_ICON_W, DESKTOP_ICON_H,
                                 desktop_select.selected[0], 0);
        draw_explorer_app_icon(explorer_x, explorer_y);
        budo_selection_icon_draw(&bwa_host_api, explorer_x, explorer_y,
                                 DESKTOP_ICON_W, DESKTOP_ICON_H, 0,
                                 desktop_select.focus == 0);
    }
    if (!desktop_folder[0] && !bwa_has_external_app_id("editor") &&
        1) {
        budo_selection_icon_draw(&bwa_host_api, editor_x, editor_y,
                                 DESKTOP_ICON_W, DESKTOP_ICON_H,
                                 desktop_select.selected[1], 0);
        draw_editor_app_icon(editor_x, editor_y);
        budo_selection_icon_draw(&bwa_host_api, editor_x, editor_y,
                                 DESKTOP_ICON_W, DESKTOP_ICON_H, 0,
                                 desktop_select.focus == 1);
    }

    {
        int i;

        for (i = 0; i < bwa_external_app_count; ++i) {
            BwaLoadedApp *app = &bwa_external_apps[i];

            if (!desktop_folder[0] && 1) {
                int app_x;
                int app_y;

                desktop_slot_position(app->desktop_slot, &app_x, &app_y);

                if (desktop_icon_dragging && desktop_drag_slots[2 + i] >= 0) {
                    app_x += desktop_drag_offset_x;
                    app_y += desktop_drag_offset_y;
                }

                budo_selection_icon_draw(&bwa_host_api, app_x, app_y,
                                         DESKTOP_ICON_W, DESKTOP_ICON_H,
                                         desktop_select.selected[2 + i], 0);
                draw_bwa_app_icon(app_x, app_y, app);
                budo_selection_icon_draw(&bwa_host_api, app_x, app_y,
                                         DESKTOP_ICON_W, DESKTOP_ICON_H, 0,
                                         desktop_select.focus == 2 + i);
            }
        }
    }

    budo_selection_drag_draw(&bwa_host_api, &desktop_select);
    bwa_draw_page = page;

    ExplorerState *saved_explorer = explorer_state;
    EditorState *saved_editor = editor_state;
    TerminalState *saved_terminal = terminal_state;
    int saved_terminal_slot = terminal_instance_slot;
    int ids[DESKTOP_RUNNING_MAX];
    AppWindow *windows[DESKTOP_RUNNING_MAX];
    const char *names[DESKTOP_RUNNING_MAX];
    int count = desktop_window_order(ids, windows, names);
    for (int i = 0; i < count; ++i) {
        int id = ids[i];
        if (windows[i]->minimized || id == active_window) continue;
        if (id < APP_BWA_BASE) {
            builtin_select(id);
            if ((id - 1) % 3 == 0) bwa_draw_page = explorer_state->v_page;
            bwa_draw_builtin_app(id);
        } else bwa_draw_external_app(bwa_find_external_app(id));
    }
    builtin_select(active_window);
    if (active_window > 0 && active_window < APP_BWA_BASE) {
        if ((active_window - 1) % 3 == 0) bwa_draw_page = explorer_state->v_page;
        bwa_draw_builtin_app(active_window);
    } else bwa_draw_external_app(bwa_find_external_app(active_window));
    explorer_state = saved_explorer;
    editor_state = saved_editor;
    terminal_state = saved_terminal;
    terminal_instance_slot = saved_terminal_slot;
    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
        ui_region_count = 0;
        ui_draw_scope = ui_scope();
        editor_draw_file_dialog();
    }
    if (context_menu) {
        int explorer = context_menu == 1;
        int selected = explorer ? explorer_selected_item >= 0 : desktop_select.focus >= 0;
        BudoMenuItem entries[] = {
            {"Open", selected, 0},
            {"Delete...", explorer ? explorer_selected_item >= 0 : desktop_has_selected_shortcuts(), 0},
            {"New File...", 1, 0},
            {"New Folder...", 1, 0},
            {"Up", explorer ? 1 : desktop_folder[0] != 0, 0},
            {explorer ? "Rename..." : "Choose Icon...", explorer ? selected : desktop_select.focus >= DESKTOP_SHORTCUT_BASE, 0},
            {explorer ? "Create Shortcut" : "Move to Folder...", explorer ? selected : desktop_has_selected_shortcuts(), 0}
        };
        budo_menu_items_draw(&bwa_host_api, context_x, context_y, 156, entries, 7);
    }
    if (explorer_shortcut_dragging && explorer_drag_shortcut >= 0 && explorer_drag_shortcut < shortcut_count) {
        DesktopItem *entry = &desktop_shortcuts[explorer_drag_shortcut];
        draw_labeled_icon(explorer_drag_x, explorer_drag_y, entry->name, entry->type, 1, entry->path);
    }
    if (confirm_kind) {
        ui_region_count = 0;
        ui_draw_scope = ui_scope();
        desktop_confirm_draw();
    }
    desktop_running_draw();
    ui_tooltip_draw();
}

static int copy_text(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(src);

    if (len >= dest_size) {
        return 0;
    }

    memcpy(dest, src, len + 1);
    return 1;
}

static int join_path(char *dest, size_t dest_size, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t total = dir_len + (needs_slash ? 1U : 0U) + name_len + 1U;

    if (total > dest_size) {
        return 0;
    }

    memcpy(dest, dir, dir_len);

    if (needs_slash) {
        dest[dir_len] = '/';
        ++dir_len;
    }

    memcpy(dest + dir_len, name, name_len + 1);
    return 1;
}

static int compare_names_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);

        if (ca != cb) {
            return ca - cb;
        }

        ++a;
        ++b;
    }

    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static int compare_items(const void *left, const void *right)
{
    const DesktopItem *a = (const DesktopItem *)left;
    const DesktopItem *b = (const DesktopItem *)right;

    if (a->type != b->type) {
        return a->type == TYPE_FOLDER ? -1 : 1;
    }

    return compare_names_ci(a->name, b->name);
}

static int is_root_path(void)
{
    return recycle_bin || strcmp(current_path, "/") == 0;
}

#include "recycle.h"

static int load_directory(const char *path)
{
    if (recycle_bin) return recycle_load();
    struct ffblk entry;
    char pattern[MAX_PATH];
    char resolved[MAX_PATH];
    int done;

    if (realpath(path, resolved)) {
        DIR *directory = opendir(resolved);
        if (!directory) {
            snprintf(explorer_error, sizeof(explorer_error), "Cannot open folder: %s", strerror(errno));
            explorer_status = explorer_error;
            return 0;
        }
        closedir(directory);
        path = resolved;
    } else {
        int virtual_folder = 0;
        for (int i = 0; i < shortcut_count; ++i)
            if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, path))
                virtual_folder = 1;
        if (!virtual_folder) {
            snprintf(explorer_error, sizeof(explorer_error), "Cannot open folder: %s", strerror(errno));
            explorer_status = explorer_error;
            return 0;
        }
    }
    if (!copy_text(current_path, sizeof(current_path), path)) {
        return 0;
    }

    if (!join_path(pattern, sizeof(pattern), current_path, "*.*")) {
        return 0;
    }

    item_count = 0;
    explorer_status = NULL;
    done = findfirst(pattern, &entry, FA_DIREC | FA_HIDDEN | FA_SYSTEM);

    while (!done && item_count < MAX_ITEMS) {
        char full_path[MAX_PATH];

        if (strcmp(entry.ff_name, ".") != 0 &&
            strcmp(entry.ff_name, "..") != 0 &&
            !(entry.ff_attrib & FA_LABEL) &&
            join_path(full_path, sizeof(full_path), current_path, entry.ff_name) &&
            copy_text(directory_items[item_count].name, sizeof(directory_items[item_count].name), entry.ff_name) &&
            copy_text(directory_items[item_count].path, sizeof(directory_items[item_count].path), full_path)) {
            directory_items[item_count].type =
                (entry.ff_attrib & FA_DIREC) ? TYPE_FOLDER : TYPE_FILE;
            ++item_count;
        }

        done = findnext(&entry);
    }

    explorer_shortcut_folder[0] = 0;
    for (int i = 0; i < shortcut_count; ++i)
        if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, current_path))
            (void)copy_text(explorer_shortcut_folder, sizeof(explorer_shortcut_folder), current_path);
    if (explorer_shortcut_folder[0]) {
        for (int i = 0; i < shortcut_count && item_count < MAX_ITEMS; ++i) {
            if (strcmp(shortcut_details[i].parent, current_path)) continue;
            int duplicate = 0;
            for (int j = 0; j < item_count; ++j)
                if (!strcmp(directory_items[j].path, desktop_shortcuts[i].path)) duplicate = 1;
            if (!duplicate) directory_items[item_count++] = desktop_shortcuts[i];
        }
    }
    qsort(directory_items, (size_t)item_count, sizeof(directory_items[0]), compare_items);
    explorer_up_selected = 0;
    budo_selection_clear(&explorer_select);
    explorer_folder_sync();
    return 1;
}

static int explorer_folder_expanded(const char *path)
{
    for (int i = 0; i < explorer_expanded_count; ++i)
        if (!strcmp(explorer_expanded[i], path)) return i;
    return -1;
}

static void explorer_folder_expand(const char *path)
{
    if (explorer_folder_expanded(path) >= 0) return;
    if (explorer_expanded_count == EXPLORER_TREE_EXPANDED_MAX) {
        explorer_status = "Folder expansion limit reached";
        return;
    }
    if (copy_text(explorer_expanded[explorer_expanded_count], MAX_PATH, path))
        ++explorer_expanded_count;
}

static int explorer_folder_compare(const void *left, const void *right)
{
    const ExplorerFolderRow *a = left, *b = right;
    return compare_names_ci(a->name, b->name);
}

static int explorer_folder_has_children(const char *path)
{
    for (int i = 0; i < shortcut_count; ++i) {
        const char *owner = shortcut_details[i].parent;
        if (!owner[0]) owner = bw_user_directory();
        if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(owner, path)) return 1;
    }
    DIR *directory = opendir(path);
    if (!directory) return 1; /* Keep inaccessible folders expandable to report errors. */
    struct dirent *entry;
    int found = 0;
    while (!found && (entry = readdir(directory))) {
        char child[MAX_PATH];
        struct stat info;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (join_path(child, sizeof(child), path, entry->d_name) &&
            stat(child, &info) == 0 && S_ISDIR(info.st_mode)) found = 1;
    }
    closedir(directory);
    return found;
}

static void explorer_folder_add(ExplorerFolderRow *children, int *count,
                                int capacity, const char *path, const char *name, int depth)
{
    for (int i = 0; i < *count; ++i)
        if (!strcmp(children[i].path, path)) return;
    if (*count >= capacity) {
        explorer_status = "Folder browser row limit reached";
        return;
    }
    ExplorerFolderRow *row = &children[*count];
    if (!copy_text(row->path, sizeof(row->path), path) ||
        !copy_text(row->name, sizeof(row->name), name)) return;
    row->depth = depth;
    row->expandable = 0;
    ++*count;
}

static void explorer_folder_rebuild(void)
{
    ExplorerFolderRow *children = malloc(sizeof(*children) * EXPLORER_TREE_MAX);
    if (!children) {
        perror("Folder browser");
        explorer_status = "Cannot allocate folder browser rows";
        return;
    }
    explorer_folder_count = 1;
    copy_text(explorer_folders[0].path, MAX_PATH, "/");
    copy_text(explorer_folders[0].name, MAX_NAME, "/");
    explorer_folders[0].depth = 0;
    for (int i = 0; i < explorer_folder_count; ++i) {
        ExplorerFolderRow *parent = &explorer_folders[i];
        if (explorer_folder_expanded(parent->path) < 0) continue;
        int count = 0;
        int capacity = EXPLORER_TREE_MAX - explorer_folder_count;
        DIR *directory = opendir(parent->path);
        if (directory) {
            struct dirent *entry;
            while ((entry = readdir(directory))) {
                char path[MAX_PATH];
                struct stat info;
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                if (join_path(path, sizeof(path), parent->path, entry->d_name) &&
                    stat(path, &info) == 0 && S_ISDIR(info.st_mode))
                    explorer_folder_add(children, &count, capacity, path, entry->d_name, parent->depth + 1);
            }
            closedir(directory);
        }
        /* Desktop folders can contain virtual folder shortcuts as well. */
        for (int j = 0; j < shortcut_count; ++j) {
            const char *owner = shortcut_details[j].parent;
            if (!owner[0]) owner = bw_user_directory();
            if (desktop_shortcuts[j].type == TYPE_FOLDER && !strcmp(owner, parent->path))
                explorer_folder_add(children, &count, capacity, desktop_shortcuts[j].path,
                                    desktop_shortcuts[j].name, parent->depth + 1);
        }
        qsort(children, (size_t)count, sizeof(*children), explorer_folder_compare);
        memmove(&explorer_folders[i + 1 + count], &explorer_folders[i + 1],
                (size_t)(explorer_folder_count - i - 1) * sizeof(*children));
        memcpy(&explorer_folders[i + 1], children, (size_t)count * sizeof(*children));
        explorer_folder_count += count;
    }
    free(children);
    explorer_folder_focus = -1;
    for (int i = 0; i < explorer_folder_count; ++i) {
        explorer_folders[i].expandable = explorer_folder_has_children(explorer_folders[i].path);
        if (!strcmp(explorer_folders[i].path, current_path)) explorer_folder_focus = i;
    }
}

static void explorer_folder_ensure_visible(void)
{
    int rows = explorer_folder_rows();
    if (explorer_folder_focus >= 0) {
        if (explorer_folder_focus < explorer_folder_top) explorer_folder_top = explorer_folder_focus;
        if (explorer_folder_focus >= explorer_folder_top + rows)
            explorer_folder_top = explorer_folder_focus - rows + 1;
    }
    int last = explorer_folder_count - rows;
    if (last < 0) last = 0;
    if (explorer_folder_top > last) explorer_folder_top = last;
    if (explorer_folder_top < 0) explorer_folder_top = 0;
}

static void explorer_folder_sync(void)
{
    if (recycle_bin) return;
    char ancestor[MAX_PATH];
    copy_text(ancestor, sizeof(ancestor), current_path);
    char *slash = strrchr(ancestor, '/');
    while (slash && slash != ancestor) {
        *slash = 0;
        explorer_folder_expand(ancestor);
        slash = strrchr(ancestor, '/');
    }
    if (strcmp(current_path, "/")) explorer_folder_expand("/");
    explorer_folder_rebuild();
    explorer_folder_ensure_visible();
}

static void explorer_folder_select(int index, int *page)
{
    if (index < 0 || index >= explorer_folder_count) return;
    if (!strcmp(explorer_folders[index].path, current_path)) return;
    char path[MAX_PATH];
    copy_text(path, sizeof(path), explorer_folders[index].path);
    if (load_directory(path)) *page = 0;
}

static void explorer_folder_toggle(int index, int *page)
{
    if (index < 0 || index >= explorer_folder_count) return;
    char path[MAX_PATH];
    copy_text(path, sizeof(path), explorer_folders[index].path);
    int expanded = explorer_folder_expanded(path);
    if (expanded < 0) {
        if (!explorer_folders[index].expandable) return;
        DIR *directory = opendir(path);
        if (!directory) {
            int virtual_folder = 0;
            for (int i = 0; i < shortcut_count; ++i)
                if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, path))
                    virtual_folder = 1;
            if (!virtual_folder) {
                snprintf(explorer_error, sizeof(explorer_error), "Cannot expand folder: %s", strerror(errno));
                explorer_status = explorer_error;
                return;
            }
        } else closedir(directory);
        explorer_folder_expand(path);
    } else {
        /* Collapsing the selected branch moves selection to its parent row. */
        if (explorer_folder_focus > index &&
            explorer_folders[explorer_folder_focus].depth > explorer_folders[index].depth) {
            int end = index + 1;
            while (end < explorer_folder_count && explorer_folders[end].depth > explorer_folders[index].depth) ++end;
            if (explorer_folder_focus < end) explorer_folder_select(index, page);
        }
        memmove(explorer_expanded[expanded], explorer_expanded[expanded + 1],
                (size_t)(explorer_expanded_count - expanded - 1) * MAX_PATH);
        --explorer_expanded_count;
    }
    explorer_folder_rebuild();
    explorer_folder_ensure_visible();
}

static int explorer_folder_indent(int index)
{
    return explorer_folders[index].depth * 10;
}

static int explorer_folder_extent(void)
{
    int extent = 0;
    for (int i = 0; i < explorer_folder_count; ++i) {
        const char *name = explorer_folders[i].name;
        int cells = 0;
        while (*name) {
            (void)bw_text_cell(&name);
            ++cells;
        }
        int end = explorer_folder_indent(i) + 12 + cells * TEXT_CELL_WIDTH + 3;
        if (end > extent) extent = end;
    }
    return extent;
}

static void explorer_folder_clamp_horizontal(void)
{
    int limit = explorer_folder_extent() - (explorer_folder_width() - BUDO_SCROLL_WIDTH);
    if (limit < 0) limit = 0;
    if (explorer_folder_left > limit) explorer_folder_left = limit;
    if (explorer_folder_left < 0) explorer_folder_left = 0;
}

static int explorer_divider_pointer(int x, int y, int event, int *page)
{
    if (event == BUDO_POINTER_UP) {
        int handled = explorer_divider_dragging;
        explorer_divider_dragging = 0;
        return handled;
    }
    if (explorer_divider_dragging && event == BUDO_POINTER_MOVE) {
        int width = x - explorer_window.x - 8 - explorer_divider_grab;
        int maximum = explorer_window.w - 40 - GRID_X_STEP;
        if (width < 96) width = 96;
        if (width > maximum) width = maximum;
        explorer_folder_preferred_width = width;
        explorer_folder_clamp_horizontal();
        clamp_page(page);
        return 1;
    }
    if (event != BUDO_POINTER_DOWN || active_window != APP_EXPLORER ||
        !explorer_window.open || explorer_window.minimized || explorer_rename_active ||
        editor_file_dialog != EDITOR_FILE_DIALOG_NONE || confirm_kind ||
        explorer_file_menu || explorer_view_menu || context_menu) return 0;
    int divider_x = explorer_window.x + 8 + explorer_folder_width();
    if (!point_in_rect(x, y, divider_x, explorer_folder_y() - 12, 8,
                       explorer_folder_rows() * 12 + 12 + BUDO_SCROLL_WIDTH)) return 0;
    explorer_divider_dragging = 1;
    explorer_divider_grab = x - divider_x;
    explorer_folder_last_click[0] = 0;
    return 1;
}

static void explorer_folder_text(int x, int y, const char *text, unsigned char ink,
                                 int left, int right)
{
    while (*text && x < right) {
        const unsigned char *glyph = glyph_for((char)bw_text_cell(&text));
        if (x + 5 > left) {
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (x + col >= left && x + col < right && (glyph[row] & (0x10 >> col)))
                        put_pixel(x + col, y + row, ink);
        }
        x += TEXT_CELL_WIDTH;
    }
}

static int explorer_folder_pointer(int x, int y, int *page)
{
    if (recycle_bin) return 0;
    if (!point_in_rect(x, y, explorer_window.x + 8, explorer_folder_y(),
                       explorer_folder_width() - BUDO_SCROLL_WIDTH, explorer_folder_rows() * 12)) return 0;
    explorer_folder_keyboard = 1;
    int index = explorer_folder_top + (y - explorer_folder_y()) / 12;
    if (index >= explorer_folder_count) return 1;
    int glyph_x = explorer_window.x + 8 + explorer_folder_indent(index) - explorer_folder_left;
    if (x >= glyph_x && x < glyph_x + 10 && explorer_folders[index].expandable) {
        explorer_folder_last_click[0] = 0;
        explorer_folder_toggle(index, page);
    } else {
        char path[MAX_PATH];
        copy_text(path, sizeof(path), explorer_folders[index].path);
        clock_t now = bw_clock();
        int double_click = !strcmp(explorer_folder_last_click, path) &&
            now - explorer_folder_click_time <= CLOCKS_PER_SEC / 2;
        explorer_folder_select(index, page);
        if (double_click) {
            for (int i = 0; i < explorer_folder_count; ++i)
                if (!strcmp(explorer_folders[i].path, path)) {
                    explorer_folder_toggle(i, page);
                    break;
                }
            explorer_folder_last_click[0] = 0;
        } else copy_text(explorer_folder_last_click, sizeof(explorer_folder_last_click), path);
        explorer_folder_click_time = now;
    }
    return 1;
}

static int explorer_folder_key(int key, int scan, int *page)
{
    if (recycle_bin) return 0;
    if (key == 9) {
        explorer_folder_keyboard = !explorer_folder_keyboard;
        explorer_folder_ensure_visible();
        return 1;
    }
    if (!explorer_folder_keyboard) return 0;
    int index = explorer_folder_focus;
    if (index < 0) index = 0;
    if (key == 13 || key == ' ') explorer_folder_toggle(index, page);
    else if (scan == 77) {
        if (explorer_folder_expanded(explorer_folders[index].path) < 0) explorer_folder_toggle(index, page);
        else if (index + 1 < explorer_folder_count && explorer_folders[index + 1].depth > explorer_folders[index].depth)
            explorer_folder_select(index + 1, page);
    } else if (scan == 75 || key == 8) {
        if (scan == 75 && explorer_folder_expanded(explorer_folders[index].path) >= 0)
            explorer_folder_toggle(index, page);
        else {
            int depth = explorer_folders[index].depth;
            while (index > 0 && explorer_folders[--index].depth >= depth) {}
            explorer_folder_select(index, page);
        }
    } else if (scan == 72 || scan == 80 || scan == 71 || scan == 79 || scan == 73 || scan == 81) {
        if (scan == 71) index = 0;
        else if (scan == 79) index = explorer_folder_count - 1;
        else index += scan == 72 ? -1 : scan == 80 ? 1 : scan == 73 ? -explorer_folder_rows() : explorer_folder_rows();
        if (index < 0) index = 0;
        if (index >= explorer_folder_count) index = explorer_folder_count - 1;
        explorer_folder_select(index, page);
    }
    /* File commands apply only when the content pane has keyboard focus. */
    return 1;
}

static void explorer_folder_draw(void)
{
    if (recycle_bin) return;
    static int previous_rows;
    if (previous_rows != explorer_folder_rows()) {
        explorer_folder_ensure_visible();
        previous_rows = explorer_folder_rows();
    }
    int x = explorer_window.x + 8, y = explorer_folder_y();
    int width = explorer_folder_width() - BUDO_SCROLL_WIDTH;
    explorer_folder_clamp_horizontal();
    draw_text(x, y - 11, "Folders", TEXT_COLOR, 7);
    int divider_x = x + explorer_folder_width();
    int divider_h = explorer_folder_rows() * 12 + 12 + BUDO_SCROLL_WIDTH;
    fill_rect(divider_x, y - 12, 8, divider_h, WINDOW_CHROME_COLOR);
    draw_rect(divider_x + 3, y - 12, 1, divider_h, WINDOW_SHADOW_COLOR);
    bwa_pointer_region(divider_x, y - 12, 8, divider_h, EXPLORER_DIVIDER_CURSOR, "Resize folder pane");
    for (int slot = 0; slot < explorer_folder_rows(); ++slot) {
        int index = explorer_folder_top + slot;
        if (index >= explorer_folder_count) break;
        int row_y = y + slot * 12;
        int indent = explorer_folder_indent(index) - explorer_folder_left;
        int selected = index == explorer_folder_focus;
        unsigned char ink = selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR;
        fill_rect(x, row_y, width, 12, selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_BG] : WINDOW_FACE_COLOR);
        if (explorer_folders[index].expandable)
            explorer_folder_text(x + indent, row_y + 2,
                                 explorer_folder_expanded(explorer_folders[index].path) >= 0 ? "-" : "+",
                                 ink, x, x + width);
        explorer_folder_text(x + indent + 12, row_y + 2, explorer_folders[index].name,
                             ink, x, x + width);
        bwa_pointer_region(x, row_y, width, 12, BUDO_CURSOR_ARROW, explorer_folders[index].path);
        if (selected && explorer_folder_keyboard) draw_rect(x, row_y, width, 12, ink);
    }
}

static int parent_path(char *dest, size_t dest_size)
{
    char temp[MAX_PATH];
    char *slash;

    if (is_root_path()) {
        return 0;
    }
    if (explorer_shortcut_folder[0]) {
        for (int i = 0; i < shortcut_count; ++i)
            if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, current_path)) {
                const char *parent = shortcut_details[i].parent;
                return copy_text(dest, dest_size, parent[0] ? parent : bw_user_directory());
            }
    }

    if (!copy_text(temp, sizeof(temp), current_path)) {
        return 0;
    }

    slash = strrchr(temp, '/');

    if (slash == NULL) {
        return 0;
    }

    if (slash == temp) {
        temp[1] = '\0';
    } else {
        *slash = '\0';
    }

    return copy_text(dest, dest_size, temp);
}

static int picker_types(const char **labels, int *values)
{
    int count = 1;
    labels[0] = "All files";
    values[0] = 0;
    if (picker_owner == APP_NONE && desktop_picker_action == 2) {
        labels[count] = "PCX (.pcx)";
        values[count++] = 3;
    } else if (picker_owner == APP_NONE && desktop_picker_action >= 4) {
        labels[count] = "Text (.txt)";
        values[count++] = 1;
        labels[count] = "RTF (.rtf)";
        values[count++] = 2;
        labels[count] = "PCX (.pcx)";
        values[count++] = 3;
    } else if (picker_owner == APP_EDITOR || picker_owner == APP_TERMINAL) {
        labels[count] = "Text (.txt/.md)";
        values[count++] = 1;
        if (picker_owner == APP_EDITOR) {
            labels[count] = "RTF (.rtf)";
            values[count++] = 2;
        }
    } else {
        BwaLoadedApp *app = bwa_find_external_app(picker_owner);
        if (app && strcmp(app->definition.app_id, "paint") == 0) {
            labels[count] = "PCX (.pcx)";
            values[count++] = 3;
        }
    }
    return count;
}

static int picker_file_matches(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (!picker_filter) return 1;
    if (!extension) return 0;
    if (picker_filter == 2) return compare_names_ci(extension, ".rtf") == 0;
    if (picker_filter == 3) return compare_names_ci(extension, ".pcx") == 0;
    return compare_names_ci(extension, ".txt") == 0 ||
           compare_names_ci(extension, ".md") == 0 ||
           compare_names_ci(extension, ".log") == 0 ||
           compare_names_ci(extension, ".csv") == 0;
}

static int editor_file_load_directory(const char *path)
{
    if (picker_owner == APP_NONE && (desktop_picker_action == 1 || desktop_picker_action == 3 || desktop_picker_action == 4)) {
        char root[MAX_PATH];
        (void)copy_text(root, sizeof(root), bw_state_file("Desktop"));
        const char *parent = !strcmp(path, root) ? "" : path;
        DesktopItem *folders = calloc(DESKTOP_SHORTCUT_MAX, sizeof(*folders));
        if (!folders) { editor_set_status("Not enough memory"); return 0; }
        int count = 0;
        for (int i = 0; i < shortcut_count; ++i)
            if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(shortcut_details[i].parent, parent))
                folders[count++] = desktop_shortcuts[i];
        unsigned char *marks = calloc((size_t)count + 1, 1);
        if (!marks) { free(folders); editor_set_status("Not enough memory"); return 0; }
        free(editor_file_items);
        editor_file_items = folders;
        editor_file_count = count;
        editor_file_top = 0;
        free(picker_selection.selected);
        budo_selection_init(&picker_selection, marks, NULL, (size_t)count);
        editor_file_last_click_time = 0;
        (void)copy_text(editor_file_path, sizeof(editor_file_path), path);
        return 1;
    }

    char directory[MAX_PATH];
    DesktopItem *next = NULL;
    int count = 0, capacity = 0;
    struct dirent *entry;
    DIR *dir;
    if (!copy_text(directory, sizeof(directory), path)) return 0;
    dir = opendir(directory);
    if (!dir) { editor_set_status("Unable to read directory"); return 0; }
    while ((entry = readdir(dir)) != NULL) {
        char full[MAX_PATH];
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            !join_path(full, sizeof(full), directory, entry->d_name) || stat(full, &info) != 0)
            continue;
        if (!S_ISDIR(info.st_mode) && (!S_ISREG(info.st_mode) || !picker_file_matches(entry->d_name)))
            continue;
        if (count == capacity) {
            int grown = capacity ? capacity * 2 : 32;
            DesktopItem *allocated = realloc(next, (size_t)grown * sizeof(*next));
            if (!allocated) {
                free(next);
                closedir(dir);
                editor_set_status("Not enough memory to list directory");
                return 0;
            }
            next = allocated;
            capacity = grown;
        }
        if (!copy_text(next[count].name, sizeof(next[count].name), entry->d_name) ||
            !copy_text(next[count].path, sizeof(next[count].path), full)) continue;
        next[count].type = S_ISDIR(info.st_mode) ? TYPE_FOLDER : TYPE_FILE;
        ++count;
    }
    closedir(dir);
    if (count > 1) qsort(next, (size_t)count, sizeof(*next), compare_items);
    unsigned char *next_selection = calloc((size_t)count + 1, 1);
    if (!next_selection) {
        free(next);
        editor_set_status("Not enough memory for file selection");
        return 0;
    }
    free(picker_selection.selected);
    budo_selection_init(&picker_selection, next_selection, NULL, (size_t)count);
    free(editor_file_items);
    editor_file_items = next;
    copy_text(editor_file_path, sizeof(editor_file_path), directory);
    editor_file_count = count;
    editor_file_top = 0;
    editor_file_selected = -1;
    editor_file_last_click_time = 0;
    return 1;
}

static int editor_file_parent(char *dest, size_t dest_size)
{
    if (picker_owner == APP_NONE && (desktop_picker_action == 1 || desktop_picker_action == 3 || desktop_picker_action == 4)) {
        char root[MAX_PATH];
        (void)copy_text(root, sizeof(root), bw_state_file("Desktop"));
        if (!strcmp(editor_file_path, root)) return 0;
        for (int i = 0; i < shortcut_count; ++i)
            if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, editor_file_path))
                return copy_text(dest, dest_size, shortcut_details[i].parent[0] ? shortcut_details[i].parent : root);
        return copy_text(dest, dest_size, root);
    }

    char temp[MAX_PATH];
    char *slash;

    if (strcmp(editor_file_path, "/") == 0) {
        return 0;
    }

    if (!copy_text(temp, sizeof(temp), editor_file_path)) {
        return 0;
    }

    slash = strrchr(temp, '/');
    if (slash == NULL) {
        return 0;
    }

    if (slash == temp) {
        temp[1] = '\0';
    } else {
        *slash = '\0';
    }

    return copy_text(dest, dest_size, temp);
}

static void editor_set_file_name(const char *name)
{
    size_t len = strlen(name);

    if (len >= sizeof(editor_file_name)) {
        len = sizeof(editor_file_name) - 1;
    }

    memcpy(editor_file_name, name, len);
    editor_file_name[len] = '\0';
    bw_text_decode(editor_file_name, editor_file_name);
    editor_file_name_len = (int)strlen(editor_file_name);
    picker_name_cursor = editor_file_name_len;
}

static char *editor_name_extension(char *name)
{
    char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    char *dot = strrchr(base, '.');
    return dot == base ? NULL : dot;
}

static void editor_replace_extension(char *name,
                                     size_t name_size,
                                     const char *extension)
{
    char *dot = editor_name_extension(name);
    size_t base_len = dot != NULL ?
                      (size_t)(dot - name) : strlen(name);
    size_t ext_len = strlen(extension);

    if (base_len + ext_len + 1 > name_size) {
        return;
    }

    memcpy(name + base_len, extension, ext_len + 1);
}

static void editor_begin_file_dialog(int mode)
{
    if (editor_read_only && mode != EDITOR_FILE_DIALOG_OPEN) return;
    char start_path[MAX_PATH];

    picker_owner = APP_EDITOR;
    picker_filter = mode == EDITOR_FILE_DIALOG_OPEN ? 0 : (editor_writer_mode ? 2 : 1);
    picker_filter_menu = picker_mkdir = 0;
    picker_overwrite = 0;
    picker_focus = 1;
    picker_name_selected = 1;
    editor_file_dialog = mode;
    editor_set_status("");
    editor_dialog = EDITOR_DIALOG_NONE;
    editor_menu = EDITOR_MENU_NONE;
    editor_set_file_name("");

    if (editor_path[0] != '\0') {
        char temp[MAX_PATH];
        char *slash;

        if (copy_text(temp, sizeof(temp), editor_path)) {
            slash = strrchr(temp, '/');
            if (slash != NULL) {
                char default_name[MAX_NAME];

                if (copy_text(default_name, sizeof(default_name),
                              slash + 1)) {
                    if (mode == EDITOR_FILE_DIALOG_SAVE_AS &&
                        editor_writer_mode) {
                        editor_replace_extension(default_name,
                                                 sizeof(default_name),
                                                 ".RTF");
                    } else if (mode == EDITOR_FILE_DIALOG_EXPORT) {
                        editor_replace_extension(default_name,
                                                 sizeof(default_name),
                                                 ".PS");
                    }
                    editor_set_file_name(default_name);
                }

                if (slash == temp) {
                    temp[1] = '\0';
                } else {
                    *slash = '\0';
                }
                if (editor_file_load_directory(temp)) {
                    return;
                }
            }
        }
    }

    if (mode == EDITOR_FILE_DIALOG_SAVE_AS &&
        editor_writer_mode &&
        editor_file_name[0] == '\0') {
        editor_set_file_name("DOCUMENT.RTF");
    } else if (mode == EDITOR_FILE_DIALOG_EXPORT &&
               editor_file_name[0] == '\0') {
        editor_set_file_name("DOCUMENT.PS");
    }

    if (copy_text(start_path, sizeof(start_path), current_path) &&
        editor_file_load_directory(start_path)) {
        return;
    }

    (void)editor_file_load_directory(bw_user_directory());
}

static int editor_file_accept(void)
{
    char full_path[MAX_PATH];
    char accepted_name[MAX_PATH];

    if (desktop_picker_action == 3 && picker_owner == APP_NONE)
        return desktop_picker_accept(editor_file_path);
    if (editor_file_name[0] == '\0' ||
        !bw_text_encode(accepted_name, sizeof(accepted_name), editor_file_name)) {
        editor_set_status("Choose a file name");
        return 0;
    }

    if (!picker_mkdir && editor_file_dialog != EDITOR_FILE_DIALOG_OPEN && picker_filter &&
        !editor_name_extension(accepted_name)) {
        const char *extension = picker_filter == 2 ? ".rtf" : picker_filter == 3 ? ".pcx" : ".txt";
        size_t len = strlen(accepted_name);
        if (len + strlen(extension) >= sizeof(accepted_name)) return 0;
        memcpy(accepted_name + len, extension, strlen(extension) + 1);
    }
    if (!picker_mkdir && picker_owner == APP_EDITOR && editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT)
        editor_replace_extension(accepted_name, sizeof(accepted_name), ".PS");
    if (!(accepted_name[0] == '/' ?
          copy_text(full_path, sizeof(full_path), accepted_name) :
          join_path(full_path, sizeof(full_path), editor_file_path, accepted_name))) {
        editor_set_status("File name too long");
        return 0;
    }

    if (picker_owner == APP_NONE && desktop_picker_action && !picker_mkdir)
        return desktop_picker_accept(full_path);
    if (picker_mkdir) {
        if (mkdir(full_path, 0777) != 0) { editor_set_status("Unable to create folder"); return 0; }
        picker_mkdir = 0;
        (void)editor_file_load_directory(full_path);
        editor_set_file_name(picker_previous_name);
        picker_name_selected = 1;
        editor_set_status("Folder created");
        return 0;
    }
    struct stat info;
    if (stat(full_path, &info) == 0 && S_ISDIR(info.st_mode)) {
        (void)editor_file_load_directory(full_path);
        editor_set_file_name("");
        return 0;
    }
    if (editor_file_dialog != EDITOR_FILE_DIALOG_OPEN && !picker_overwrite &&
        stat(full_path, &info) == 0) {
        desktop_confirm(picker_owner, BUDO_CONFIRM_OVERWRITE,
                        "Replace existing file?", accepted_name);
        confirm_is_picker = 1;
        return 0;
    }
    picker_overwrite = 0;
    if (picker_owner >= APP_BWA_BASE) {
        BwaLoadedApp *app = bwa_find_external_app(picker_owner);
        int ok = 0;
        int mode = editor_file_dialog;
        editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
        if (app && app->definition.file_selected) {
            bwa_callback_app = app;
            ok = app->definition.file_selected(mode, full_path);
            bwa_callback_app = NULL;
        }
        if (!ok) {
            editor_file_dialog = mode;
            editor_set_status("Unable to open or save file");
        }
        return ok;
    } else if (picker_owner == APP_TERMINAL) {
        char temporary[MAX_PATH];
        FILE *file = desktop_file_open(full_path, temporary);
        if (!file) { editor_set_status("Save failed"); return 0; }
        for (int i = 0; i < terminal_line_count; ++i)
            fprintf(file, "%s\n", terminal_lines[i]);
        if (session_fd >= 0) {
            for (int row = 0; row < session_rows; ++row) {
                int length = session_cols;
                while (length > 0 && session_cells[row][length - 1].ch == ' ') --length;
                if (!length && row > session_y) break;
                for (int col = 0; col < length; ++col) {
                    char encoded[8];
                    char cell[2] = {(char)session_cells[row][col].ch, 0};
                    if (bw_text_encode(encoded, sizeof(encoded), cell)) fputs(encoded, file);
                }
                fputc('\n', file);
            }
        }
        if (!desktop_file_finish(file, full_path, temporary)) {
            editor_set_status("Write failed; original file preserved");
            return 0;
        }
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_OPEN) {
        if (!editor_load_file(full_path)) return 0;
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS) {
        if (!editor_save_file(full_path)) return 0;
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT) {
        if (!editor_export_postscript(full_path)) return 0;
    } else {
        return 0;
    }

    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    if (picker_owner == APP_EDITOR && editor_pending_action) {
        int action = editor_pending_action;
        editor_pending_action = 0;
        editor_request_action(action, editor_pending_path);
    }
    return 1;
}

static void editor_file_dialog_click(int x, int y, clock_t now)
{
    int dialog_x, dialog_y, dialog_w, dialog_h;
    picker_geometry(&dialog_x, &dialog_y, &dialog_w, &dialog_h);
    int list_x = dialog_x + 10;
    int list_y = dialog_y + 38;
    int list_w = dialog_w - 36;
    int list_h = dialog_h - 102;
    int rows = list_h / 12;
    int ok_x = dialog_x + dialog_w - 132;
    int cancel_x = dialog_x + dialog_w - 68;
    int button_y = dialog_y + dialog_h - 24;

    if (picker_filter_menu) {
        const char *labels[4];
        int values[4];
        int count = picker_types(labels, values);
        int chosen = budo_menu_hit(x, y, dialog_x + 10,
                                   dialog_y + dialog_h - 34 - (count * 16 + 4), 144, count);
        picker_filter_menu = 0;
        if (chosen >= 0) {
            chosen = values[chosen];
            picker_filter = chosen;
            (void)editor_file_load_directory(editor_file_path);
            if (editor_file_dialog != EDITOR_FILE_DIALOG_OPEN && chosen) {
                editor_replace_extension(editor_file_name, sizeof(editor_file_name),
                                         chosen == 2 ? ".rtf" : chosen == 3 ? ".pcx" : ".txt");
                editor_file_name_len = (int)strlen(editor_file_name);
                picker_name_cursor = editor_file_name_len;
            }
        }
        return;
    }
    if (point_in_rect(x, y, dialog_x + 10, dialog_y + dialog_h - 34, 144, 14)) {
        picker_filter_menu = 1;
        return;
    }
    if (point_in_rect(x, y, dialog_x + dialog_w - 126, dialog_y + 18, 72, 14)) {
        if (picker_owner == APP_NONE && (desktop_picker_action == 1 || desktop_picker_action == 3)) {
            desktop_picker_action = 1;
            editor_set_file_name("New Folder");
            picker_focus = picker_name_selected = 1;
            return;
        }
        snprintf(picker_previous_name, sizeof(picker_previous_name), "%s", editor_file_name);
        picker_mkdir = 1;
        editor_set_file_name("New Folder");
        picker_focus = picker_name_selected = 1;
        return;
    }
    if (point_in_rect(x, y, dialog_x + dialog_w - 48,
                      dialog_y + 18, 38, 14)) {
        char parent[MAX_PATH];
        if (editor_file_parent(parent, sizeof(parent))) {
            (void)editor_file_load_directory(parent);
        }
        return;
    }

    if (point_in_rect(x, y, cancel_x, button_y, 58, 16)) {
        editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
        return;
    }

    if (point_in_rect(x, y, ok_x, button_y, 58, 16)) {
        (void)editor_file_accept();
        return;
    }

    if (point_in_rect(x, y, list_x, list_y, list_w, list_h)) {
        int row = (y - list_y) / 12;
        int index = editor_file_top + row;

        if (row < rows && index >= 0 && index < editor_file_count) {
            DesktopItem *item = &editor_file_items[index];

            if (editor_file_selected == index &&
                now - editor_file_last_click_time <= CLOCKS_PER_SEC / 2) {
                if (item->type == TYPE_FOLDER) {
                    (void)editor_file_load_directory(item->path);
                } else {
                    editor_set_file_name(item->name);
                    if (editor_file_dialog == EDITOR_FILE_DIALOG_OPEN) {
                        (void)editor_file_accept();
                    }
                }
            } else {
                budo_selection_click(&picker_selection, NULL, editor_file_count, index, 0, 0);
                editor_file_last_click_time = now;
                if (item->type == TYPE_FILE) {
                    editor_set_file_name(item->name);
                }
            }
        }
    }
}

static void picker_cancel(void)
{
    if (picker_mkdir) {
        picker_mkdir = 0;
        editor_set_file_name(picker_previous_name);
        return;
    }
    int owner = picker_owner;
    if (owner == APP_NONE) desktop_picker_action = 0;
    desktop_exit_requested = 0;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    editor_pending_action = 0;
    picker_overwrite = 0;
    if (owner >= APP_BWA_BASE) {
        BwaLoadedApp *app = bwa_find_external_app(owner);
        if (app && app->definition.confirm_result) {
            bwa_callback_app = app;
            app->definition.confirm_result(BUDO_CONFIRM_SAVE, BUDO_RESPONSE_CANCEL);
            bwa_callback_app = NULL;
        }
    }
}

static void editor_file_dialog_key(int key)
{
    int rows = (270 - 102) / 12;
    if (rows < 1) rows = 1;
    if (key == 27) {
        if (picker_filter_menu) picker_filter_menu = 0;
        else picker_cancel();
        return;
    }
    if (key == 9) {
        picker_focus = !picker_focus;
        picker_name_selected = picker_focus;
        return;
    }
    if (key == 1 && picker_focus) { picker_name_selected = 1; return; }
    if (key == 13) {
        if (!picker_focus && editor_file_selected >= 0 &&
            editor_file_selected < editor_file_count &&
            editor_file_items[editor_file_selected].type == TYPE_FOLDER) {
            (void)editor_file_load_directory(editor_file_items[editor_file_selected].path);
        } else (void)editor_file_accept();
        return;
    }
    if (key == 0) {
        int scan = getch();
        if (scan == 201 || scan == 202) {
            editor_file_top += scan == 201 ? -3 : 3;
            desktop_scroll_configure(picker_owner, 0);
            editor_file_top = picker_scroll.position;
            return;
        }
        if (picker_focus && (scan == 75 || scan == 77 || scan == 71 || scan == 79 || scan == 83)) {
            if (scan == 75 && picker_name_cursor > 0) --picker_name_cursor;
            if (scan == 77 && picker_name_cursor < editor_file_name_len) ++picker_name_cursor;
            if (scan == 71) picker_name_cursor = 0;
            if (scan == 79) picker_name_cursor = editor_file_name_len;
            if (scan == 83) {
                if (picker_name_selected) {
                    editor_file_name[0] = '\0';
                    editor_file_name_len = picker_name_cursor = 0;
                } else if (picker_name_cursor < editor_file_name_len) {
                    memmove(editor_file_name + picker_name_cursor,
                            editor_file_name + picker_name_cursor + 1,
                            (size_t)(editor_file_name_len - picker_name_cursor));
                    --editor_file_name_len;
                }
            }
            picker_name_selected = 0;
            return;
        }
        picker_focus = 0;
        if (scan == 72 || scan == 80 || scan == 73 || scan == 81 ||
            scan == 71 || scan == 79 || scan == 75 || scan == 77)
            (void)budo_selection_move(&picker_selection, NULL, editor_file_count,
                                      scan, 1, rows, 0);
        if (editor_file_selected >= 0) {
            if (editor_file_selected < editor_file_top) editor_file_top = editor_file_selected;
            if (editor_file_selected >= editor_file_top + rows) editor_file_top = editor_file_selected - rows + 1;
            if (editor_file_items[editor_file_selected].type == TYPE_FILE)
                editor_set_file_name(editor_file_items[editor_file_selected].name);
        }
        return;
    }
    if (!picker_focus && key == 8) {
        char parent[MAX_PATH];
        if (editor_file_parent(parent, sizeof(parent))) (void)editor_file_load_directory(parent);
        return;
    }
    if (key == 8 || (key >= 32 && key <= 255 && key != 127)) {
        picker_focus = 1;
        if (picker_name_selected) {
            editor_file_name[0] = '\0';
            editor_file_name_len = picker_name_cursor = 0;
            picker_name_selected = 0;
        }
        if (key == 8 && picker_name_cursor > 0) {
            memmove(editor_file_name + picker_name_cursor - 1,
                    editor_file_name + picker_name_cursor,
                    (size_t)(editor_file_name_len - picker_name_cursor + 1));
            --picker_name_cursor;
            --editor_file_name_len;
        } else if (key >= 32 && key <= 255 && key != 127 && editor_file_name_len < MAX_PATH - 1) {
            memmove(editor_file_name + picker_name_cursor + 1,
                    editor_file_name + picker_name_cursor,
                    (size_t)(editor_file_name_len - picker_name_cursor + 1));
            editor_file_name[picker_name_cursor++] = (char)key;
            ++editor_file_name_len;
        }
    }
}

static void editor_draw_file_dialog(void)
{
    int dialog_x, dialog_y, dialog_w, dialog_h;
    picker_geometry(&dialog_x, &dialog_y, &dialog_w, &dialog_h);
    int list_x = dialog_x + 10;
    int list_y = dialog_y + 38;
    int list_w = dialog_w - 36;
    int list_h = dialog_h - 102;
    int rows = list_h / 12;
    int row;
    int ok_x = dialog_x + dialog_w - 132;
    int cancel_x = dialog_x + dialog_w - 68;
    int button_y = dialog_y + dialog_h - 24;
    const char *title =
        picker_owner == APP_NONE ? (desktop_picker_action >= 4 ? "New File (name and type)" : desktop_picker_action == 1 ? "New Desktop Folder" : desktop_picker_action == 2 ? "Choose Icon (32 x 32 PCX)" : "Move to Folder (navigate, then Move)") :
        editor_file_dialog == EDITOR_FILE_DIALOG_OPEN ? "Open" :
        editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT ?
        "Export PostScript" : "Save As";
    const char *ok_text =
        picker_owner == APP_NONE ? (desktop_picker_action == 1 || desktop_picker_action >= 4 ? "Create" : desktop_picker_action == 2 ? "Choose" : "Move") :
        editor_file_dialog == EDITOR_FILE_DIALOG_OPEN ? "Open" :
        editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT ?
        "Export" : "Save";

    fill_rect(dialog_x + 3, dialog_y + 3, dialog_w, dialog_h, WINDOW_SHADOW_COLOR);
    fill_rect(dialog_x, dialog_y, dialog_w, dialog_h, WINDOW_CHROME_COLOR);
    draw_bevel(dialog_x, dialog_y, dialog_w, dialog_h, 1);
    draw_rect(dialog_x, dialog_y, dialog_w, dialog_h, TEXT_COLOR);
    fill_rect(dialog_x + 1, dialog_y + 1, dialog_w - 2, 15, TITLE_COLOR);
    draw_text(dialog_x + 6, dialog_y + 5, title, TITLE_TEXT_COLOR, (dialog_w - 12) / 6);

    draw_text(dialog_x + 8, dialog_y + 21,
              editor_file_path, TEXT_COLOR, (dialog_w - 140) / 6);
    bwa_draw_standard_button(dialog_x + dialog_w - 126, dialog_y + 18, 72, 14, "New Folder", 0);
    bwa_draw_standard_button(dialog_x + dialog_w - 48, dialog_y + 18, 38, 14, "Up", 0);

    fill_rect(list_x, list_y, list_w, list_h, WINDOW_FACE_COLOR);
    draw_bevel(list_x, list_y, list_w, list_h, 0);

    for (row = 0; row < rows; ++row) {
        int index = editor_file_top + row;
        int y = list_y + 3 + row * 12;

        if (index >= editor_file_count) break;

        if (index == editor_file_selected) {
            fill_rect(list_x + 2, list_y + 1 + row * 12,
                      list_w - 4, 11, TITLE_COLOR);
        }

        if (editor_file_items[index].type == TYPE_FOLDER) {
            draw_text(list_x + 5, y, "[Dir]", index == editor_file_selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR, 5);
            draw_text_elided(list_x + 41, y,
                      editor_file_items[index].name, index == editor_file_selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR, (list_w - 46) / 6);
        } else {
            draw_text_elided(list_x + 5, y,
                      editor_file_items[index].name, index == editor_file_selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR, (list_w - 10) / 6);
        }
        bwa_pointer_region(list_x + 2, list_y + 1 + row * 12, list_w - 4, 11,
                           BUDO_CURSOR_ARROW, editor_file_items[index].name);
    }

    desktop_scroll_configure(picker_owner, 0);
    budo_scroll_draw(&bwa_host_api, &picker_scroll);
    draw_text(dialog_x + 8, dialog_y + dialog_h - 48,
              "File name:", TEXT_COLOR, 10);
    fill_rect(dialog_x + 72, dialog_y + dialog_h - 53,
              dialog_w - 82, 16, WINDOW_FACE_COLOR);
    draw_bevel(dialog_x + 72, dialog_y + dialog_h - 53,
               dialog_w - 82, 16, 0);
    bwa_pointer_region(dialog_x + 72, dialog_y + dialog_h - 53,
                        dialog_w - 82, 16, BUDO_CURSOR_TEXT, NULL);
    {
        int cols = (dialog_w - 92) / 6;
        int offset = picker_name_cursor >= cols ? picker_name_cursor - cols + 1 : 0;
        if (picker_focus && picker_name_selected)
            fill_rect(dialog_x + 75, dialog_y + dialog_h - 51, dialog_w - 88, 12, TITLE_COLOR);
        draw_text(dialog_x + 77, dialog_y + dialog_h - 48, editor_file_name + offset,
                  picker_focus && picker_name_selected ? ui_color_indices[BUDO_SYS_COLOR_SELECTION_TEXT] : TEXT_COLOR, cols);
        if (picker_focus && !picker_name_selected && text_caret_visible)
            fill_rect(dialog_x + 77 + (picker_name_cursor - offset) * 6,
                      dialog_y + dialog_h - 49, 2, TEXT_CELL_HEIGHT, TEXT_COLOR);
    }
    {
        const char *types[4] = {"All files", "Text (.txt/.md)", "RTF (.rtf)", "PCX (.pcx)"};
        bwa_draw_standard_button(dialog_x + 10, dialog_y + dialog_h - 34,
                                 144, 14, types[picker_filter], 0);
        draw_text(dialog_x + 160, dialog_y + dialog_h - 31,
                  editor_status, TEXT_COLOR, (dialog_w - 170) / 6);
    }
    bwa_draw_button_state(ok_x, button_y, 58, 16, picker_mkdir ? "Create" : ok_text, BUDO_BUTTON_DEFAULT);
    bwa_draw_standard_button(cancel_x, button_y, 58, 16, "Cancel", 0);
    if (picker_filter_menu) {
        const char *types[4];
        int values[4];
        int count = picker_types(types, values);
        budo_menu_draw(&bwa_host_api, dialog_x + 10,
                       dialog_y + dialog_h - 34 - (count * 16 + 4), 144, types, count);
    }

}

static int executable_path(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

static int executable_file(const char *name)
{
    char path[MAX_PATH];
    return join_path(path, sizeof(path), current_path, name) && executable_path(path);
}

static int extension_matches(const char *name, const char *extension)
{
    const char *dot = strrchr(name, '.');

    return dot != NULL && compare_names_ci(dot, extension) == 0;
}

static int text_file(const char *name)
{
    static const char *extensions[] = {
        ".TXT", ".RTF", ".INI", ".CFG", ".LOG", ".NFO", ".DIZ", ".MD",
        ".CSV", ".TSV", ".INF", ".LST", ".DOC", ".ME"
    };
    int i;

    for (i = 0; i < (int)(sizeof(extensions) / sizeof(extensions[0])); ++i) {
        if (extension_matches(name, extensions[i])) return 1;
    }
    return 0;
}

static int code_file(const char *name)
{
    static const char *extensions[] = {
        ".C", ".H", ".CPP", ".HPP", ".CC", ".CXX", ".ASM", ".INC",
        ".PAS", ".BAS", ".JAVA", ".JS", ".PY", ".CSS", ".HTM", ".HTML",
        ".PHP", ".SQL", ".LUA", ".PL", ".RB", ".SH", ".XML", ".JSON",
        ".PCX"
    };
    int i;

    for (i = 0; i < (int)(sizeof(extensions) / sizeof(extensions[0])); ++i) {
        if (extension_matches(name, extensions[i])) return 1;
    }
    return 0;
}

static void add_default_association(const char *extension)
{
    FileAssociation *assoc;

    if (file_association_count >= FILE_ASSOC_MAX) return;

    assoc = &file_associations[file_association_count++];
    strncpy(assoc->extension, extension, FILE_EXT_LEN);
    assoc->extension[FILE_EXT_LEN] = '\0';
    strncpy(assoc->app_id, "editor", BWA_ID_LEN - 1);
    assoc->app_id[BWA_ID_LEN - 1] = '\0';
}

static int find_file_association(const char *extension)
{
    int i;

    for (i = 0; i < file_association_count; ++i) {
        if (compare_names_ci(file_associations[i].extension, extension) == 0) {
            return i;
        }
    }
    return -1;
}

static int save_file_associations(void)
{
    char path[MAX_PATH];
    FILE *file;
    char temporary[MAX_PATH];
    int i;

    if (!copy_text(path, sizeof(path), ASSOCIATION_FILE)) {
        return 0;
    }

    file = desktop_file_open(path, temporary);
    if (file == NULL) return 0;

    for (i = 0; i < file_association_count; ++i) {
        fprintf(file, "%s %s\n",
                file_associations[i].extension,
                file_associations[i].app_id[0] != '\0' ?
                file_associations[i].app_id : "-");
    }

    return desktop_file_finish(file, path, temporary);
}

static void load_file_associations(void)
{
    static const char *defaults[] = {
        ".TXT", ".RTF", ".INI", ".CFG", ".LOG", ".NFO", ".DIZ", ".MD",
        ".CSV", ".TSV", ".INF", ".LST", ".DOC", ".ME", ".PCX",
        ".C", ".H", ".CPP", ".HPP", ".CC", ".CXX", ".ASM", ".INC",
        ".PAS", ".BAS", ".JAVA", ".JS", ".PY", ".CSS", ".HTM", ".HTML",
        ".PHP", ".SQL", ".LUA", ".PL", ".RB", ".SH", ".XML", ".JSON"
    };
    char path[MAX_PATH];
    FILE *file;
    int i;

    file_association_count = 0;
    memset(file_associations, 0, sizeof(file_associations));

    for (i = 0; i < (int)(sizeof(defaults) / sizeof(defaults[0])); ++i) {
        add_default_association(defaults[i]);
    }

    {
        int pcx_index = find_file_association(".PCX");
        if (pcx_index >= 0) {
            (void)copy_text(file_associations[pcx_index].app_id,
                            sizeof(file_associations[pcx_index].app_id),
                            "paint");
        }
    }

    if (!copy_text(path, sizeof(path), ASSOCIATION_FILE)) return;

    file = fopen(path, "rt");
    if (file != NULL) {
        char ext[FILE_EXT_LEN + 1];
        char app_id[BWA_ID_LEN];

        file_association_count = 0;
        while (fscanf(file, "%8s %15s", ext, app_id) == 2) {
            int index = find_file_association(ext);
            if (index < 0 && ext[0] == '.' && file_association_count < FILE_ASSOC_MAX) {
                add_default_association(ext);
                index = file_association_count - 1;
            }

            if (index >= 0) {
                if (strcmp(app_id, "-") == 0) {
                    file_associations[index].app_id[0] = '\0';
                } else {
                    (void)copy_text(file_associations[index].app_id,
                                    sizeof(file_associations[index].app_id),
                                    app_id);
                }
            }
        }
        fclose(file);
    }
}

static int bwa_get_file_association_count(void)
{
    return file_association_count;
}

static int bwa_get_file_association(int index,
                                    char *extension,
                                    unsigned int extension_size,
                                    char *app_id,
                                    unsigned int app_id_size)
{
    if (index < 0 || index >= file_association_count ||
        extension == NULL || extension_size == 0 ||
        app_id == NULL || app_id_size == 0) {
        return 0;
    }

    snprintf(extension, extension_size, "%s",
             file_associations[index].extension);
    snprintf(app_id, app_id_size, "%s",
             file_associations[index].app_id);
    return 1;
}

static int bwa_set_file_association(const char *extension, const char *app_id)
{
    if (!extension || !app_id || extension[0] != '.' || !extension[1] ||
        strlen(extension) > FILE_EXT_LEN || strlen(app_id) >= BWA_ID_LEN) return 0;
    for (const char *p = extension + 1; *p; ++p)
        if (!isalnum((unsigned char)*p)) return 0;
    for (const char *p = app_id; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return 0;
    FileAssociation backup[FILE_ASSOC_MAX];
    int count = file_association_count;
    memcpy(backup, file_associations, sizeof(backup));
    int index = find_file_association(extension);
    if (index < 0) {
        if (count == FILE_ASSOC_MAX) return 0;
        add_default_association(extension);
        index = count;
    }
    for (size_t i = 0; i <= strlen(extension); ++i)
        file_associations[index].extension[i] = (char)toupper((unsigned char)extension[i]);
    (void)copy_text(file_associations[index].app_id, BWA_ID_LEN, app_id);
    if (save_file_associations()) return 1;
    file_association_count = count;
    memcpy(file_associations, backup, sizeof(backup));
    return 0;
}

static int bwa_delete_file_association(const char *extension)
{
    if (!extension) return 0;
    int index = find_file_association(extension);
    if (index < 0) return 0;
    FileAssociation removed = file_associations[index];
    memmove(file_associations + index, file_associations + index + 1,
            (size_t)(file_association_count - index - 1) * sizeof(removed));
    --file_association_count;
    if (save_file_associations()) return 1;
    memmove(file_associations + index + 1, file_associations + index,
            (size_t)(file_association_count - index) * sizeof(removed));
    file_associations[index] = removed;
    ++file_association_count;
    return 0;
}

static void normalize_slashes(char *path) { (void)path; }

static void terminal_load_resume_state(void)
{
    FILE *file = fopen(TERMINAL_STATE_FILE, "rt");
    char path[MAX_PATH];

    if (file == NULL) {
        return;
    }

    if (fgets(path, sizeof(path), file) != NULL) {
        size_t len = strlen(path);
        int x, y, w, h;
        int restore_x, restore_y, restore_w, restore_h;
        int minimized, maximized, scroll;

        while (len > 0 &&
               (path[len - 1] == '\n' || path[len - 1] == '\r')) {
            path[--len] = '\0';
        }

        if (len > 0 &&
            fscanf(file, "%d %d %d %d %d %d %d %d %d %d %d",
                   &x, &y, &w, &h,
                   &restore_x, &restore_y,
                   &restore_w, &restore_h,
                   &minimized, &maximized, &scroll) == 11) {
            (void)copy_text(terminal_cwd, sizeof(terminal_cwd), path);
            terminal_window.x = x;
            terminal_window.y = y;
            terminal_window.w = w;
            terminal_window.h = h;
            terminal_window.restore_x = restore_x;
            terminal_window.restore_y = restore_y;
            terminal_window.restore_w = restore_w;
            terminal_window.restore_h = restore_h;
            terminal_window.minimized = 0;
            terminal_window.maximized = maximized;
            terminal_window.open = 1;
            terminal_window.dragging = 0;
            terminal_window.resizing = 0;
            terminal_scroll = scroll;
            active_window = APP_TERMINAL;
        }
    }

    fclose(file);
    (void)remove(TERMINAL_STATE_FILE);
}

static int desktop_launch_program(const char *path, const char *directory)
{
    char executable[MAX_PATH], working_directory[MAX_PATH];
    if (!copy_text(executable, sizeof(executable), path) ||
        !copy_text(working_directory, sizeof(working_directory), directory)) return 0;
    if (!builtin_create_instance(BWA_HOST_APP_TERMINAL, 0)) return 0;
    snprintf(terminal_title, sizeof(terminal_title), "%.255s",
             strrchr(executable, '/') ? strrchr(executable, '/') + 1 : executable);
    (void)copy_text(terminal_cwd, sizeof(terminal_cwd), working_directory);
    if (!session_start_program(executable, working_directory)) {
        terminal_add_line("Unable to start program.");
        return 0;
    }
    return 1;
}

static int request_launch(const DesktopItem *item, int page)
{
    if (active_window == APP_EXPLORER && recycle_bin) return 0;
    (void)page;
    return desktop_launch_program(item->path, current_path);
}
static int load_initial_directory(int *page)
{
    FILE *file = fopen(STATE_FILE, "rt");
    char path[MAX_PATH];

    if (file != NULL) {
        if (fgets(path, sizeof(path), file) != NULL) {
            size_t len = strlen(path);
            int saved_page;
            int saved_only_executables;
            int open;
            int minimized;
            int maximized;
            int x;
            int y;
            int w;
            int h;
            int restore_x;
            int restore_y;
            int restore_w;
            int restore_h;

            while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
                path[--len] = '\0';
            }

            if (fscanf(file, "%d %d %d %d %d %d %d %d %d %d %d %d %d",
                       &saved_page,
                       &saved_only_executables,
                       &open,
                       &minimized,
                       &maximized,
                       &x,
                       &y,
                       &w,
                       &h,
                       &restore_x,
                       &restore_y,
                       &restore_w,
                       &restore_h) == 13) {
                *page = saved_page;
                only_executables = saved_only_executables;
                explorer_window.open = open;
                explorer_window.minimized = minimized;
                explorer_window.maximized = maximized;
                explorer_window.x = x;
                explorer_window.y = y;
                explorer_window.w = w;
                explorer_window.h = h;
                explorer_window.restore_x = restore_x;
                explorer_window.restore_y = restore_y;
                explorer_window.restore_w = restore_w;
                explorer_window.restore_h = restore_h;
                resume_explorer = open;
            } else {
                resume_explorer = 1;
            }

            explorer_window.dragging = 0;
            explorer_window.resizing = 0;

            fclose(file);
            (void)remove(STATE_FILE);

            if (len > 0 && load_directory(path)) {
                return 1;
            }
        } else {
            fclose(file);
            (void)remove(STATE_FILE);
        }
    }

    *page = 0;
    return load_directory(bw_user_directory());
}

static int visible_item_count(void)
{
    int i;
    int count = 0;

    for (i = 0; i < item_count; ++i) {
        if (directory_items[i].type == TYPE_FOLDER ||
            !only_executables ||
            executable_file(directory_items[i].name)) {
            ++count;
        }
    }

    return count;
}

static int visible_item_at(int position)
{
    int i;
    int visible = 0;

    for (i = 0; i < item_count; ++i) {
        if (directory_items[i].type == TYPE_FOLDER ||
            !only_executables ||
            executable_file(directory_items[i].name)) {
            if (visible == position) {
                return i;
            }
            ++visible;
        }
    }

    return -1;
}

static int explorer_order(int *order)
{
    int count = visible_item_count();
    for (int i = 0; i < count; ++i) order[i] = visible_item_at(i);
    return count;
}

static void explorer_clear_selection(void)
{
    explorer_up_selected = 0;
    budo_selection_clear(&explorer_select);
}

static void explorer_select_all_visible(void)
{
    int order[MAX_ITEMS];
    int count = explorer_order(order);
    budo_selection_all(&explorer_select, order, count);
}

static void explorer_select_item(int target, unsigned int modifiers)
{
    explorer_up_selected = 0;
    int order[MAX_ITEMS];
    int count = explorer_order(order);
    budo_selection_click(&explorer_select, order, count, target, modifiers, 0);
}

static unsigned int keyboard_modifiers(void) { return bw_modifiers(); }

static int explorer_path_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static int explorer_path_is_same_or_child(const char *path,
                                          const char *parent)
{
    size_t i = 0;

    while (parent[i] != '\0' && path[i] != '\0') {
        if (parent[i] != path[i]) {
            return 0;
        }
        ++i;
    }

    if (parent[i] != '\0') return 0;
    return path[i] == '\0' || path[i] == '/';
}

static int explorer_copy_file(const char *source, const char *dest)
{
    FILE *input = fopen(source, "rb");
    FILE *output;
    unsigned char buffer[8192];
    size_t count;
    int ok = 1;

    if (input == NULL) return 0;

    output = fopen(dest, "wb");
    if (output == NULL) {
        fclose(input);
        return 0;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            ok = 0;
            break;
        }
    }

    if (ferror(input)) ok = 0;
    if (fclose(output) != 0) ok = 0;
    fclose(input);

    if (!ok) (void)remove(dest);
    return ok;
}

static int explorer_copy_path(const char *source,
                              const char *dest,
                              int type)
{
    struct stat info;
    if (lstat(source, &info) != 0 || S_ISLNK(info.st_mode)) return 0;
    if (explorer_path_exists(dest)) return 0;

    if (type == TYPE_FILE) {
        return explorer_copy_file(source, dest);
    }

    if (mkdir(dest, 0777) != 0) return 0;

    {
        struct ffblk entry;
        char pattern[MAX_PATH];
        int done;

        if (!join_path(pattern, sizeof(pattern), source, "*.*")) {
            return 0;
        }

        done = findfirst(pattern, &entry,
                         FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIREC);
        while (!done) {
            if (strcmp(entry.ff_name, ".") != 0 &&
                strcmp(entry.ff_name, "..") != 0 &&
                !(entry.ff_attrib & FA_LABEL)) {
                char source_child[MAX_PATH];
                char dest_child[MAX_PATH];
                int child_type =
                    (entry.ff_attrib & FA_DIREC) ?
                    TYPE_FOLDER : TYPE_FILE;

                if (!join_path(source_child, sizeof(source_child),
                               source, entry.ff_name) ||
                    !join_path(dest_child, sizeof(dest_child),
                               dest, entry.ff_name) ||
                    !explorer_copy_path(source_child, dest_child,
                                        child_type)) {
                    return 0;
                }
            }
            done = findnext(&entry);
        }
    }

    return 1;
}

static int explorer_delete_path(const char *path, int type)
{
    struct stat info;
    (void)type;
    if (lstat(path, &info) != 0) { perror(path); return 0; }
    if (!S_ISDIR(info.st_mode)) {
        if (unlink(path) == 0) return 1;
        perror(path);
        return 0;
    }
    DIR *directory = opendir(path);
    if (!directory) { perror(path); return 0; }
    int ok = 1;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno) { perror(path); ok = 0; }
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[MAX_PATH];
        if (!join_path(child, sizeof(child), path, entry->d_name) ||
            !explorer_delete_path(child, TYPE_FILE)) { ok = 0; break; }
    }
    if (closedir(directory) != 0) { perror(path); ok = 0; }
    if (!ok) return 0;
    if (rmdir(path) == 0) return 1;
    perror(path);
    return 0;
}

static void explorer_stage_clipboard(int mode)
{
    if (recycle_bin) return;
    int i;

    explorer_clipboard_count = 0;
    explorer_clipboard_mode = EXPLORER_CLIP_NONE;

    for (i = 0; i < item_count &&
                explorer_clipboard_count < MAX_ITEMS; ++i) {
        ExplorerClipboardItem *clip;

        if (!explorer_selection[i]) continue;

        clip = &explorer_clipboard[explorer_clipboard_count];
        if (!copy_text(clip->name, sizeof(clip->name), directory_items[i].name) ||
            !copy_text(clip->path, sizeof(clip->path), directory_items[i].path)) {
            continue;
        }

        clip->type = directory_items[i].type;
        ++explorer_clipboard_count;
    }

    if (explorer_clipboard_count > 0) {
        explorer_clipboard_mode = mode;
    }
}

static int explorer_make_copy_name(const char *name,
                                   char *dest,
                                   size_t dest_size)
{
    char base[9];
    char ext[5];
    const char *dot = strrchr(name, '.');
    size_t base_len;
    size_t ext_len = 0;
    int suffix;

    if (dot != NULL && dot != name) {
        base_len = (size_t)(dot - name);
        ext_len = strlen(dot);
        if (ext_len > 4) ext_len = 4;
        memcpy(ext, dot, ext_len);
        ext[ext_len] = '\0';
    } else {
        base_len = strlen(name);
        ext[0] = '\0';
    }

    if (base_len > 8) base_len = 8;
    memcpy(base, name, base_len);
    base[base_len] = '\0';

    for (suffix = 1; suffix <= 999; ++suffix) {
        char suffix_text[5];
        size_t suffix_len;
        size_t keep;
        char candidate[MAX_NAME];
        int n;

        snprintf(suffix_text, sizeof(suffix_text), "~%d", suffix);
        suffix_len = strlen(suffix_text);
        keep = suffix_len < 8 ? 8 - suffix_len : 0;
        if (keep > base_len) keep = base_len;

        n = snprintf(candidate, sizeof(candidate),
                     "%.*s%s%s",
                     (int)keep, base,
                     suffix_text, ext);
        if (n <= 0 || (size_t)n >= sizeof(candidate)) {
            return 0;
        }

        {
            char full[MAX_PATH];
            if (!join_path(full, sizeof(full),
                           current_path, candidate)) {
                return 0;
            }
            if (!explorer_path_exists(full)) {
                return copy_text(dest, dest_size, candidate);
            }
        }
    }

    return 0;
}

static int explorer_paste_clipboard_impl(void);
static int explorer_paste_clipboard(void)
{
    if (recycle_bin) return 0;
    int previous = ui_busy_begin();
    int result = explorer_paste_clipboard_impl();
    ui_busy_end(previous);
    return result;
}

static int explorer_paste_clipboard_impl(void)
{
    char refresh_path[MAX_PATH];
    int i;
    int changed = 0;
    int keep = 0;

    if (explorer_clipboard_mode == EXPLORER_CLIP_NONE ||
        explorer_clipboard_count <= 0 ||
        !copy_text(refresh_path, sizeof(refresh_path), current_path)) {
        return 0;
    }

    for (i = 0; i < explorer_clipboard_count; ++i) {
        ExplorerClipboardItem *clip = &explorer_clipboard[i];
        char dest[MAX_PATH];
        int success = 0;

        if (!join_path(dest, sizeof(dest), current_path, clip->name)) {
            if (explorer_clipboard_mode == EXPLORER_CLIP_CUT) {
                if (keep != i) explorer_clipboard[keep] = *clip;
                ++keep;
            }
            continue;
        }

        if (clip->type == TYPE_FOLDER &&
            explorer_path_is_same_or_child(current_path, clip->path) &&
            compare_names_ci(current_path, clip->path) != 0) {
            if (explorer_clipboard_mode == EXPLORER_CLIP_CUT) {
                if (keep != i) explorer_clipboard[keep] = *clip;
                ++keep;
            }
            continue;
        }

        if (compare_names_ci(dest, clip->path) == 0 ||
            explorer_path_exists(dest)) {
            char copy_name[MAX_NAME];

            if (!explorer_make_copy_name(clip->name,
                                         copy_name,
                                         sizeof(copy_name)) ||
                !join_path(dest, sizeof(dest),
                           current_path, copy_name)) {
                if (explorer_clipboard_mode == EXPLORER_CLIP_CUT) {
                    if (keep != i) explorer_clipboard[keep] = *clip;
                    ++keep;
                }
                continue;
            }
        }

        if (explorer_clipboard_mode == EXPLORER_CLIP_CUT &&
            compare_names_ci(dest, clip->path) != 0 &&
            rename(clip->path, dest) == 0) {
            success = 1;
        } else {
            success = explorer_copy_path(clip->path, dest, clip->type);

            if (success &&
                explorer_clipboard_mode == EXPLORER_CLIP_CUT &&
                !explorer_delete_path(clip->path, clip->type)) {
                (void)explorer_delete_path(dest, clip->type);
                success = 0;
            }
        }

        if (success) {
            changed = 1;
        } else if (explorer_clipboard_mode == EXPLORER_CLIP_CUT) {
            if (keep != i) explorer_clipboard[keep] = *clip;
            ++keep;
        }
    }

    if (explorer_clipboard_mode == EXPLORER_CLIP_CUT) {
        explorer_clipboard_count = keep;
        if (keep == 0) {
            explorer_clipboard_mode = EXPLORER_CLIP_NONE;
        }
    }

    if (changed) (void)load_directory(refresh_path);
    return changed;
}

static int explorer_begin_rename(void)
{
    if (recycle_bin) return 0;
    int i;
    int selected = -1;
    int count = 0;

    for (i = 0; i < item_count; ++i) {
        if (explorer_selection[i]) {
            selected = i;
            ++count;
            if (count > 1) return 0;
        }
    }

    if (count != 1 || selected < 0) return 0;

    if (!copy_text(explorer_rename_input,
                   sizeof(explorer_rename_input),
                   directory_items[selected].name)) {
        return 0;
    }

    bw_text_decode(explorer_rename_input, explorer_rename_input);
    explorer_rename_item = selected;
    explorer_rename_len = (int)strlen(explorer_rename_input);
    explorer_rename_active = 1;
    text_caret_visible = 1;
    text_caret_last_toggle = bw_clock();
    return 1;
}

static void explorer_cancel_rename(void)
{
    explorer_rename_active = 0;
    explorer_creating_folder = 0;
    explorer_rename_item = -1;
    explorer_rename_input[0] = '\0';
    explorer_rename_len = 0;
}

static int explorer_rename_name_valid(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;

    if (name[0] == '\0' ||
        strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        return 0;
    }

    while (*p != '\0') {
        if (*p < 32 ||
            *p == '\\' || *p == '/' || *p == ':' ||
            *p == '*' || *p == '?' || *p == '"' ||
            *p == '<' || *p == '>' || *p == '|') {
            return 0;
        }
        ++p;
    }

    return 1;
}

static int explorer_commit_rename(void)
{
    if (recycle_bin) return 0;
    char encoded_name[MAX_NAME];
    if (!bw_text_encode(encoded_name, sizeof(encoded_name), explorer_rename_input)) return 0;
    if (explorer_creating_folder) {
        char path[MAX_PATH];
        if (!explorer_rename_name_valid(explorer_rename_input) ||
            !join_path(path, sizeof(path), current_path, encoded_name) ||
            mkdir(path, 0777) != 0) return 0;
        explorer_cancel_rename();
        explorer_creating_folder = 0;
        return load_directory(current_path);
    }

    char old_path[MAX_PATH];
    char new_path[MAX_PATH];
    int i;
    int renamed_index = -1;

    if (!explorer_rename_active ||
        explorer_rename_item < 0 ||
        explorer_rename_item >= item_count ||
        !explorer_rename_name_valid(explorer_rename_input) ||
        !copy_text(old_path, sizeof(old_path),
                   directory_items[explorer_rename_item].path) ||
        !join_path(new_path, sizeof(new_path),
                   current_path, encoded_name)) {
        return 0;
    }

    if (compare_names_ci(old_path, new_path) != 0 &&
        explorer_path_exists(new_path)) {
        return 0;
    }

    if (strcmp(old_path, new_path) != 0 &&
        rename(old_path, new_path) != 0) {
        return 0;
    }

    for (i = 0; i < explorer_clipboard_count; ++i) {
        if (compare_names_ci(explorer_clipboard[i].path,
                             old_path) == 0) {
            (void)copy_text(explorer_clipboard[i].path,
                            sizeof(explorer_clipboard[i].path),
                            new_path);
            (void)copy_text(explorer_clipboard[i].name,
                            sizeof(explorer_clipboard[i].name),
                            encoded_name);
        }
    }

    explorer_cancel_rename();

    if (!load_directory(current_path)) return 0;

    for (i = 0; i < item_count; ++i) {
        if (compare_names_ci(directory_items[i].path, new_path) == 0) {
            renamed_index = i;
            break;
        }
    }

    if (renamed_index >= 0) {
        explorer_selection[renamed_index] = 1;
        explorer_selected_item = renamed_index;
    }

    return 1;
}

static int explorer_edit_selection(void)
{
    int i;
    int selected = -1;
    int count = 0;

    for (i = 0; i < item_count; ++i) {
        if (explorer_selection[i]) {
            selected = i;
            ++count;
            if (count > 1) return 0;
        }
    }

    if (count != 1 ||
        selected < 0 ||
        directory_items[selected].type != TYPE_FILE) {
        return 0;
    }

    return bwa_open_editor_file(directory_items[selected].path);
}

static int explorer_delete_selection(void)
{
    if (recycle_bin) return 0;
    char refresh_path[MAX_PATH];
    int i;
    int changed = 0;
    int failed = 0;

    if (!copy_text(refresh_path, sizeof(refresh_path), current_path)) {
        return 0;
    }

    if (explorer_shortcut_folder[0]) {
        memset(shortcut_selection, 0, DESKTOP_SHORTCUT_MAX);
        for (i = 0; i < item_count; ++i)
            if (explorer_selection[i])
                for (int j = 0; j < shortcut_count; ++j)
                    if (!strcmp(shortcut_details[j].parent, current_path) &&
                        !strcmp(desktop_shortcuts[j].path, directory_items[i].path)) shortcut_selection[j] = 1;
        changed = shortcuts_delete_selected();
        (void)load_directory(refresh_path);
        explorer_status = changed ? "Shortcuts removed; target files preserved." : "No shortcut removed.";
        return changed;
    }
    int selected = 0;
    for (i = 0; i < item_count; ++i) {
        if (explorer_selection[i]) {
            ++selected;
            if (recycle_put(directory_items[i].path)) changed = 1;
            else {
                snprintf(explorer_error, sizeof(explorer_error), "Delete failed: %.90s: %.40s",
                         directory_items[i].name, strerror(errno));
                failed = 1;
            }
        }
    }

    (void)load_directory(refresh_path);
    explorer_status = failed ? explorer_error : selected ? "Selected entries moved to Recycle Bin." : "No entries selected; nothing deleted.";
    return changed;
}

static int entries_per_page(void)
{
    int slots = explorer_slots();

    if (slots < 1) {
        slots = 1;
    }

    return is_root_path() ? slots : (slots > 1 ? slots - 1 : 1);
}

static int page_count(void)
{
    int count = visible_item_count() + !is_root_path();
    int rows = (count + explorer_cols() - 1) / explorer_cols();
    return rows > explorer_rows() ? rows - explorer_rows() + 1 : 1;
}

static int item_for_slot(int page, int slot)
{
    int index = page * explorer_cols() + slot;
    if (!is_root_path()) {
        if (!index) return -2;
        --index;
    }
    return index >= 0 && index < visible_item_count() ? visible_item_at(index) : -1;
}

static void clamp_page(int *page)
{
    int pages = page_count();

    if (*page >= pages) {
        *page = pages - 1;
    }

    if (*page < 0) {
        *page = 0;
    }
}

static void open_explorer(void)
{
    window_open_state(&explorer_window);
}

static void minimize_explorer(void)
{
    explorer_divider_dragging = 0;
    window_minimize_state(&explorer_window);
    desktop_focus_visible();
}

static void close_explorer(void)
{
    explorer_divider_dragging = 0;
    window_close_state(&explorer_window);
    desktop_focus_visible();
}

static void toggle_maximize_explorer(void)
{
    window_toggle_maximize_state(&explorer_window);
}

static void editor_save_current(void)
{
    if (editor_read_only) return;
    if (editor_path[0]) (void)editor_save_file(editor_path);
    else editor_begin_file_dialog(EDITOR_FILE_DIALOG_SAVE_AS);
}

static void desktop_file_menu_draw(int app)
{
    AppWindow *window = app == APP_EXPLORER ? &explorer_window : &terminal_window;
    int menu = app == APP_EXPLORER ? explorer_file_menu : terminal_file_menu;
    const char *explorer_items[8] = {"Open", "Up", "New Folder...", "Rename...", "Delete...", "Create Shortcut", "Close", "New File..."};
    const char *terminal_items[3] = {"Save Output...", "Clear Output", "Close"};
    fill_rect(window->x + 4, window->y + WINDOW_TITLE_H,
              window->w - 8, EDITOR_MENU_H, WINDOW_CHROME_COLOR);
    draw_menu_bar_item(window->x + 5, window->y + WINDOW_TITLE_H, 36, app == APP_EXPLORER && recycle_bin ? "Close" : "File", menu);
    if (app == APP_TERMINAL) terminal_ui_draw();
    if (app == APP_EXPLORER && recycle_bin) {
        bwa_draw_standard_button(window->x + 44, window->y + WINDOW_TITLE_H, 60, EDITOR_MENU_H, "Restore", 0);
        bwa_draw_standard_button(window->x + 108, window->y + WINDOW_TITLE_H, 60, EDITOR_MENU_H, "Empty", 0);
        return;
    }
    if (app == APP_EXPLORER) {
        draw_menu_bar_item(window->x + 44, window->y + WINDOW_TITLE_H, 36, "View", explorer_view_menu);
        if (explorer_view_menu) {
            BudoMenuItem views[] = {{"Icons", 1, !explorer_list_view}, {"List / Details", 1, explorer_list_view}};
            budo_menu_items_draw(&bwa_host_api, window->x + 44, window->y + WINDOW_TITLE_H + EDITOR_MENU_H, 156, views, 2);
        }
    }
    if (menu) budo_menu_draw(&bwa_host_api, window->x + 5,
                             window->y + WINDOW_TITLE_H + EDITOR_MENU_H,
                             156, app == APP_EXPLORER ? explorer_items : terminal_items,
                             app == APP_EXPLORER ? 8 : 3);
}

static int desktop_file_menu_click(int x, int y, int *page)
{
    AppWindow *window;
    int *menu;
    int app = active_window;
    if (app != APP_EXPLORER && app != APP_TERMINAL) return 0;
    window = app == APP_EXPLORER ? &explorer_window : &terminal_window;
    menu = app == APP_EXPLORER ? &explorer_file_menu : &terminal_file_menu;
    if (app == APP_EXPLORER && recycle_bin) {
        if (point_in_rect(x, y, window->x + 44, window->y + WINDOW_TITLE_H, 60, EDITOR_MENU_H)) return recycle_restore(), 1;
        if (point_in_rect(x, y, window->x + 108, window->y + WINDOW_TITLE_H, 60, EDITOR_MENU_H)) {
            (void)desktop_confirm(APP_EXPLORER, 4, "Empty Recycle Bin?", "Permanently delete all recycled files and folders?");
            return 1;
        }
        if (point_in_rect(x, y, window->x + 5, window->y + WINDOW_TITLE_H, 36, EDITOR_MENU_H)) return close_explorer(), 1;
        return 0;
    }
    if (app == APP_TERMINAL && terminal_ui_click(x, y)) return 1;
    if (app == APP_EXPLORER && explorer_view_menu) {
        int item = budo_menu_hit(x, y, window->x + 44, window->y + WINDOW_TITLE_H + EDITOR_MENU_H, 156, 2);
        explorer_view_menu = 0;
        if (item >= 0) {
            explorer_list_view = item == 1;
            *page = 0;
            FILE *file = fopen(bw_state_file("explorer-view.state"), "w");
            if (file) {
                int ok = fprintf(file, "%d\n", explorer_list_view) > 0;
                if (fclose(file) != 0) ok = 0;
                if (!ok) perror("Save Explorer view");
            } else perror("Save Explorer view");
        }
        return 1;
    }
    if (app == APP_EXPLORER && point_in_rect(x, y, window->x + 44, window->y + WINDOW_TITLE_H, 36, EDITOR_MENU_H)) {
        explorer_view_menu = 1;
        *menu = 0;
        return 1;
    }
    if (*menu) {
        int item = budo_menu_hit(x,y,window->x+5,
                                 window->y+WINDOW_TITLE_H+EDITOR_MENU_H,
                                 156, app == APP_EXPLORER ? 8 : 3);
        *menu = 0;
        if (item < 0) return 1;
        if (app == APP_TERMINAL) {
            if (item == 0) {
                editor_begin_file_dialog(EDITOR_FILE_DIALOG_SAVE_AS);
                picker_owner = APP_TERMINAL;
                editor_set_file_name("TERMINAL.TXT");
                (void)editor_file_load_directory(terminal_cwd);
            } else if (item == 1) {
                terminal_clear_selection();
                terminal_line_count = 0;
                terminal_scroll = 0;
                terminal_paging = 0;
                if (session_fd >= 0) session_clear();
            } else close_terminal();
        } else if (item == 0 && explorer_selected_item >= 0) {
            DesktopItem *entry = &directory_items[explorer_selected_item];
            if (entry->type == TYPE_FOLDER) {
                if (load_directory(entry->path)) *page = 0;
            } else if (recycle_bin) (void)bwa_open_reader_file(entry->path);
            else (void)open_associated_file(entry->path);
        } else if (item == 1) {
            char parent[MAX_PATH];
            if (parent_path(parent, sizeof(parent)) && load_directory(parent)) *page = 0;
        } else if (item == 2) {
            explorer_creating_folder = 1;
            explorer_rename_active = 1;
            explorer_rename_input[0] = '\0';
            explorer_rename_len = 0;
        } else if (item == 3) {
            explorer_creating_folder = 0;
            (void)explorer_begin_rename();
        } else if (item == 4) {
            (void)desktop_confirm(APP_EXPLORER, 3, "Delete selected files?",
                                  "Move the selected entries to Recycle Bin?");
        } else if (item == 5) (void)shortcuts_create();
        else if (item == 6) close_explorer();
        else if (item == 7) desktop_begin_picker(5);
        return 1;
    }
    if (point_in_rect(x,y,window->x+5,window->y+WINDOW_TITLE_H,36,EDITOR_MENU_H)) {
        *menu = 1;
        return 1;
    }
    return 0;
}

static uint64_t editor_document_hash(void)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < editor_line_count; ++i) {
        const unsigned char *p = (const unsigned char *)editor_lines[i];
        do {
            hash = (hash ^ *p) * UINT64_C(1099511628211);
        } while (*p++);
    }
    hash = (hash ^ (unsigned int)editor_writer_mode) * UINT64_C(1099511628211);
    if (editor_writer_mode) {
        hash = (hash ^ (unsigned int)editor_writer_left_indent) * UINT64_C(1099511628211);
        hash = (hash ^ (unsigned int)editor_writer_first_indent) * UINT64_C(1099511628211);
        hash = (hash ^ (unsigned int)editor_writer_right_indent) * UINT64_C(1099511628211);
        for (int i = 0; i < EDITOR_WRITER_TABS; ++i)
            hash = (hash ^ (unsigned int)editor_writer_tabs[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static int editor_modified(void)
{
    return !editor_read_only && editor_document_hash() != editor_saved_hash;
}

static void editor_mark_saved(void)
{
    editor_saved_hash = editor_document_hash();
}

static AppWindow *picker_window(void)
{
    BwaLoadedApp *app = bwa_find_external_app(picker_owner);
    if (app && app->managed_window) return &app->window;
    return picker_owner == APP_TERMINAL ? &terminal_window : &editor_window;
}

static void picker_geometry(int *x, int *y, int *w, int *h)
{
    AppWindow *window = picker_window();
    *w = 440;
    *h = 270;
    *x = window->x + (window->w - *w) / 2;
    *y = window->y + (window->h - *h) / 2;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x + *w > SCREEN_WIDTH) *x = SCREEN_WIDTH - *w;
    if (*y + *h > SCREEN_HEIGHT) *y = SCREEN_HEIGHT - *h;
}

static int bwa_file_dialog(int mode, const char *initial)
{
    BwaLoadedApp *app = bwa_callback_app;
    char directory[MAX_PATH];
    char *slash;
    if (!app || !app->definition.file_selected ||
        (mode != BUDO_FILE_OPEN && mode != BUDO_FILE_SAVE_AS)) return 0;
    if (!copy_text(directory, sizeof(directory), initial ? initial : "")) return 0;
    picker_owner = app->definition.runtime_id;
    picker_filter = strcmp(app->definition.app_id, "paint") == 0 ? 3 : 0;
    picker_filter_menu = picker_mkdir = 0;
    picker_overwrite = 0;
    picker_focus = 1;
    picker_name_selected = 1;
    editor_file_dialog = mode;
    editor_set_status("");
    slash = strrchr(directory, '/');
    editor_set_file_name(slash ? slash + 1 : directory);
    if (slash) {
        if (slash == directory) slash[1] = '\0';
        else *slash = '\0';
    } else {
        copy_text(directory, sizeof(directory), bw_user_directory());
    }
    if (!editor_file_load_directory(directory))
        (void)editor_file_load_directory(bw_user_directory());
    return 1;
}

static int desktop_confirm(int owner, int kind, const char *title, const char *message)
{
    if (confirm_kind) return 0;
    confirm_owner = owner;
    confirm_kind = kind;
    confirm_is_picker = 0;
    snprintf(confirm_title, sizeof(confirm_title), "%s", title);
    snprintf(confirm_message, sizeof(confirm_message), "%s", message);
    return 1;
}

static int bwa_confirm_dialog(int kind, const char *title, const char *message)
{
    BwaLoadedApp *app = bwa_callback_app;
    if (!app || !app->definition.confirm_result ||
        (kind != BUDO_CONFIRM_SAVE && kind != BUDO_CONFIRM_OVERWRITE)) return 0;
    return desktop_confirm(app->definition.runtime_id, kind, title, message);
}

static void editor_request_action(int action, const char *path)
{
    if (editor_modified()) {
        editor_pending_action = action;
        snprintf(editor_pending_path, sizeof(editor_pending_path), "%s", path ? path : "");
        desktop_confirm(APP_EDITOR, BUDO_CONFIRM_SAVE, "Unsaved changes",
                        "Save your document before continuing?");
        return;
    }
    if (editor_read_only && action == 1) return;
    if (action == 1) editor_reset_document();
    else if (action == 2) {
        if (path && path[0]) (void)editor_load_file(path);
        else editor_begin_file_dialog(EDITOR_FILE_DIALOG_OPEN);
    } else if (action == 3) editor_close_now();
}

static void desktop_confirm_result(int response)
{
    int kind = confirm_kind;
    int from_picker = confirm_is_picker;
    if (response == BUDO_RESPONSE_CANCEL) desktop_exit_requested = 0;
    int owner = confirm_owner;
    builtin_select(owner);
    confirm_kind = 0;
    if (kind == 4 && owner == APP_EXPLORER) {
        if (response == BUDO_RESPONSE_SAVE) (void)recycle_empty();
        return;
    }
    if (kind == 3 && owner == APP_EXPLORER) {
        if (response == BUDO_RESPONSE_SAVE) (void)explorer_delete_selection();
        return;
    }
    if (kind == BUDO_CONFIRM_OVERWRITE && from_picker) {
        if (response == BUDO_RESPONSE_SAVE) {
            picker_overwrite = 1;
            (void)editor_file_accept();
        }
        return;
    }
    if (owner == APP_EDITOR) {
        int action = editor_pending_action;
        if (response == BUDO_RESPONSE_CANCEL) {
            editor_pending_action = 0;
            return;
        }
        if (response == BUDO_RESPONSE_SAVE) {
            if (!editor_path[0]) {
                editor_begin_file_dialog(EDITOR_FILE_DIALOG_SAVE_AS);
                return;
            }
            if (!editor_save_file(editor_path)) {
                editor_pending_action = 0;
                return;
            }
        }
        editor_pending_action = 0;
        /* Discard grants only the pending transition, not a save mark. */
        if (action == 1) editor_reset_document();
        else if (action == 2) {
            if (editor_pending_path[0]) (void)editor_load_file(editor_pending_path);
            else editor_begin_file_dialog(EDITOR_FILE_DIALOG_OPEN);
        } else if (action == 3) {
            if (response == BUDO_RESPONSE_DISCARD) editor_reset_document();
            editor_close_now();
        }
    } else {
        BwaLoadedApp *app = bwa_find_external_app(owner);
        if (app && app->definition.confirm_result) {
            bwa_callback_app = app;
            app->definition.confirm_result(kind, response);
            bwa_callback_app = NULL;
        }
    }
}

static void desktop_confirm_draw(void)
{
    int x = 130, y = 185, w = 380;
    int count = confirm_kind == BUDO_CONFIRM_SAVE ? 3 : 2;
    const char *labels[3] = {"Save", "Discard", "Cancel"};
    if (count == 2) { labels[0] = confirm_kind == 4 ? "Empty" : confirm_kind == 3 ? "Delete" : "Replace"; labels[1] = "Cancel"; }
    fill_rect(x + 3, y + 3, w, 100, WINDOW_SHADOW_COLOR);
    bwa_draw_standard_window(x, y, w, 100, confirm_title, 0);
    draw_text(x + 10, y + 33, confirm_message, TEXT_COLOR, 60);
    for (int i = 0; i < count; ++i)
        bwa_draw_standard_button(x + w - 12 - (count - i) * 82, y + 70,
                                 76, 20, labels[i], 0);
}

static void desktop_confirm_click(int x, int y)
{
    int count = confirm_kind == BUDO_CONFIRM_SAVE ? 3 : 2;
    for (int i = 0; i < count; ++i) {
        if (point_in_rect(x, y, 130 + 380 - 12 - (count - i) * 82,
                          255, 76, 20)) {
            desktop_confirm_result(i == count - 1 ? BUDO_RESPONSE_CANCEL :
                                   i == 0 ? BUDO_RESPONSE_SAVE : BUDO_RESPONSE_DISCARD);
            return;
        }
    }
}

static void scrollbar_configure(BudoScrollbar *bar, int x, int y, int length,
                                 int horizontal, int total, int page, int position)
{
    bar->x = x;
    bar->y = y;
    bar->length = length > 32 ? length : 33;
    bar->horizontal = horizontal;
    bar->total = total;
    bar->page = page > 0 ? page : 1;
    bar->position = position;
    budo_scroll_clamp(bar);
}

static void editor_set_top_visual_row(int row)
{
    int line = 0;
    if (row < 0) row = 0;
    while (line + 1 < editor_line_count) {
        int rows = editor_wrapped_rows_for_line(line, editor_visible_cols());
        if (row < rows) break;
        row -= rows;
        ++line;
    }
    editor_top_line = line;
    editor_top_segment = row;
}

static void desktop_scroll_configure(int app, int page)
{
    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
        int x,y,w,h;
        picker_geometry(&x,&y,&w,&h);
        scrollbar_configure(&picker_scroll, x + w - 26, y + 38, h - 102,
                            0, editor_file_count, (h - 102) / 12, editor_file_top);
    }
    if (app == APP_EDITOR) {
        int total = 0, top = editor_top_segment, longest = 1;
        int ruler = editor_writer_mode && editor_writer_ruler ? EDITOR_RULER_H : 0;
        for (int i = 0; i < editor_line_count; ++i) {
            int rows = editor_wrapped_rows_for_line(i, editor_visible_cols());
            int len = (int)strlen(editor_lines[i]);
            total += rows;
            if (i < editor_top_line) top += rows;
            if (len > longest) longest = len;
        }
        scrollbar_configure(&editor_vscroll,
            editor_window.x + editor_window.w - 20,
            editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + ruler + 4,
            editor_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - ruler - EDITOR_STATUS_H - 6 -
            (editor_wrap_enabled() ? 0 : 16),
            0, total, editor_visible_rows(), top);
        scrollbar_configure(&editor_hscroll, editor_text_x(),
            editor_window.y + editor_window.h - EDITOR_STATUS_H - 18,
            editor_window.x + editor_window.w - 20 - editor_text_x(),
            1, editor_wrap_enabled() ? 0 : longest,
            editor_visible_cols(), editor_left_col);
    } else if (app == APP_EXPLORER) {
        scrollbar_configure(&explorer_folder_hscroll,
            explorer_window.x + 8, explorer_folder_y() + explorer_folder_rows() * 12,
            explorer_folder_width() - BUDO_SCROLL_WIDTH,
            1, explorer_folder_extent(), explorer_folder_width() - BUDO_SCROLL_WIDTH, explorer_folder_left);
        explorer_folder_left = explorer_folder_hscroll.position;
        scrollbar_configure(&explorer_folder_scroll,
            explorer_window.x + 8 + explorer_folder_width() - BUDO_SCROLL_WIDTH,
            explorer_folder_y(), explorer_folder_rows() * 12,
            0, explorer_folder_count, explorer_folder_rows(), explorer_folder_top);
        explorer_folder_top = explorer_folder_scroll.position;
        scrollbar_configure(&explorer_scroll,
            explorer_window.x + explorer_window.w - 20,
            explorer_client_y(), explorer_client_h() - 12,
            0, page_count() + explorer_rows() - 1, explorer_rows(), page);
    } else if (app == APP_TERMINAL) {
        int rows = session_fd >= 0 ? session_rows : terminal_visible_rows();
        int top = session_fd >= 0 ? terminal_line_count - terminal_scroll :
            terminal_paging ? terminal_page_top : terminal_line_count - rows - terminal_scroll;
        scrollbar_configure(&terminal_scrollbar,
            terminal_window.x + terminal_window.w - 20,
            terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 4,
            terminal_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 20,
            0, terminal_line_count + (session_fd >= 0 ? rows : 0), rows, top);
    }
}

static void desktop_scroll_draw(int app, int page)
{
    desktop_scroll_configure(app, page);
    if (app == APP_EDITOR) {
        budo_scroll_draw(&bwa_host_api, &editor_vscroll);
        if (!editor_wrap_enabled()) budo_scroll_draw(&bwa_host_api, &editor_hscroll);
    } else if (app == APP_EXPLORER) {
        if (!recycle_bin) {
            budo_scroll_draw(&bwa_host_api, &explorer_folder_scroll);
            budo_scroll_draw(&bwa_host_api, &explorer_folder_hscroll);
            fill_rect(explorer_folder_scroll.x, explorer_folder_hscroll.y,
                      BUDO_SCROLL_WIDTH, BUDO_SCROLL_WIDTH, WINDOW_CHROME_COLOR);
        }
        budo_scroll_draw(&bwa_host_api, &explorer_scroll);
    } else if (app == APP_TERMINAL) {
        budo_scroll_draw(&bwa_host_api, &terminal_scrollbar);
    }
}

static int desktop_scroll_pointer(int x, int y, int event, int *page)
{
    int app = active_window;
    int handled = 0;
    if (!recycle_bin && explorer_divider_pointer(x, y, event, page)) return 1;
    if (event == BUDO_POINTER_UP) {
        BudoScrollbar *bars[] = {&editor_vscroll, &editor_hscroll,
                                &explorer_scroll, &explorer_folder_scroll, &explorer_folder_hscroll,
                                &terminal_scrollbar, &picker_scroll};
        for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i)
            handled |= budo_scroll_pointer_at(bars[i], x, y, event, 0);
        return handled;
    }
    desktop_scroll_configure(app, *page);
    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
        handled = budo_scroll_pointer_host(&bwa_host_api, &picker_scroll, x, y, event);
        editor_file_top = picker_scroll.position;
    } else if (app == APP_EDITOR && editor_dialog == EDITOR_DIALOG_NONE &&
               editor_menu == EDITOR_MENU_NONE) {
        handled = budo_scroll_pointer_host(&bwa_host_api, &editor_vscroll, x, y, event);
        if (handled) editor_set_top_visual_row(editor_vscroll.position);
        if (!editor_wrap_enabled() && budo_scroll_pointer_host(&bwa_host_api, &editor_hscroll, x, y, event)) {
            editor_left_col = editor_hscroll.position;
            handled = 1;
        }
    } else if (app == APP_EXPLORER && !explorer_rename_active) {
        if (!recycle_bin) {
            handled = budo_scroll_pointer_host(&bwa_host_api, &explorer_folder_hscroll, x, y, event);
            explorer_folder_left = explorer_folder_hscroll.position;
            if (handled) return 1;
            handled = budo_scroll_pointer_host(&bwa_host_api, &explorer_folder_scroll, x, y, event);
            explorer_folder_top = explorer_folder_scroll.position;
            if (handled) return 1;
        }
        handled = budo_scroll_pointer_host(&bwa_host_api, &explorer_scroll, x, y, event);
        *page = explorer_scroll.position;
    } else if (app == APP_TERMINAL) {
        handled = budo_scroll_pointer_host(&bwa_host_api, &terminal_scrollbar, x, y, event);
        if (handled) {
            if (terminal_paging) terminal_page_top = terminal_scrollbar.position;
            else terminal_scroll = terminal_line_count - (session_fd >= 0 ? 0 : terminal_visible_rows()) - terminal_scrollbar.position;
            if (terminal_scroll < 0) terminal_scroll = 0;
        }
    }
    return handled;
}

static void open_editor(void)
{
    if (!editor_saved_hash) editor_mark_saved();
    window_open_state(&editor_window);
    active_window = APP_EDITOR;
}

/* Launchers create a fresh state. Ordinary focus/restore never calls here. */
static int builtin_create_instance(int kind, int start_terminal)
{
    int slot;
    int reader = kind == BWA_HOST_APP_READER;
    int bin = kind == BWA_HOST_APP_RECYCLE_BIN;
    if (reader) kind = BWA_HOST_APP_EDITOR;
    if (bin) kind = BWA_HOST_APP_EXPLORER;
    if (kind < BWA_HOST_APP_EXPLORER || kind > BWA_HOST_APP_TERMINAL) return 0;
    for (slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        AppWindow *window = builtin_window(kind + slot * 3);
        if (!window || !window->open) break;
    }
    if (slot == BUILTIN_INSTANCE_MAX) {
        fprintf(stderr, "BUDOWIN: application window limit reached\n");
        return 0;
    }
    if (kind == BWA_HOST_APP_EXPLORER) {
        ExplorerState *state = explorer_instances[slot];
        if (!state) state = malloc(sizeof(*state));
        if (!state) { perror("BUDOWIN: Explorer instance"); return 0; }
        *state = explorer_defaults;
        explorer_instances[slot] = explorer_state = state;
        state->runtime_id = kind + slot * 3;
        explorer_select.selected = explorer_selection;
        explorer_select.snapshot = explorer_drag_snapshot;
        recycle_bin = bin;
        if (!(bin ? recycle_load() : load_directory(bw_user_directory()))) return 0;
        if (bin) explorer_list_view = 1;
        open_explorer();
        active_window = APP_EXPLORER;
    } else if (kind == BWA_HOST_APP_EDITOR) {
        EditorState *state = editor_instances[slot];
        if (state) {
            editor_state = state;
            editor_history_reset();
        } else state = malloc(sizeof(*state));
        if (!state) { perror("BUDOWIN: Editor instance"); return 0; }
        *state = editor_defaults;
        editor_instances[slot] = editor_state = state;
        state->runtime_id = kind + slot * 3;
        load_editor_settings();
        if (reader) {
            editor_read_only = 1;
            editor_writer_mode = 1;
            editor_writer_ruler = 0;
            editor_writer_left_indent = editor_writer_first_indent = editor_writer_right_indent = 0;
        }
        open_editor();
    } else {
        TerminalState *state = terminal_instances[slot];
        int reused = state != NULL;
        if (!state) state = malloc(sizeof(*state));
        if (!state) { perror("BUDOWIN: Terminal instance"); return 0; }
        terminal_instances[slot] = terminal_state = state;
        terminal_instance_slot = slot;
        if (reused) session_stop();
        *state = terminal_defaults;
        state->runtime_id = kind + slot * 3;
        session_context = session_defaults;
        graphics_context = (GraphicsState){0};
        terminalui_context = terminalui_defaults;
        terminal_palette_load();
        if (start_terminal) open_terminal();
        else {
            window_open_state(&terminal_window);
            active_window = APP_TERMINAL;
            bw_set_event_filter(session_forward_event);
        }
    }
    AppWindow *window = builtin_window(kind + slot * 3);
    int offset = (slot % 6) * 12;
    window->x += offset;
    window->y += offset;
    return 1;
}

static int builtin_new_instance(int kind)
{
    return builtin_create_instance(kind, 1);
}

static int builtin_poll_terminals(void)
{
    TerminalState *saved = terminal_state;
    int saved_slot = terminal_instance_slot, changed = 0;
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (!terminal_instances[slot] || !terminal_instances[slot]->v_terminal_window.open) continue;
        builtin_select(3 + slot * 3);
        if (session_poll()) changed = 1;
    }
    terminal_state = saved;
    terminal_instance_slot = saved_slot;
    return changed;
}

static void minimize_editor(void)
{
    editor_menu = EDITOR_MENU_NONE;
    editor_dialog = EDITOR_DIALOG_NONE;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    window_minimize_state(&editor_window);
    desktop_focus_visible();
}

static void editor_close_now(void)
{
    editor_menu = EDITOR_MENU_NONE;
    editor_dialog = EDITOR_DIALOG_NONE;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    window_close_state(&editor_window);
    desktop_focus_visible();
}

static void close_editor(void)
{
    editor_request_action(3, NULL);
}

static int desktop_request_exit(void)
{
    if (confirm_kind || editor_file_dialog != EDITOR_FILE_DIALOG_NONE) return 0;
    EditorState *saved = editor_state;
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (!editor_instances[slot]) continue;
        editor_state = editor_instances[slot];
        if (editor_window.open && editor_modified()) {
            editor_window.minimized = 0;
            active_window = APP_EDITOR;
            editor_request_action(3, NULL);
            return 0;
        }
    }
    editor_state = saved;
    for (int i = 0; i < bwa_instance_count; ++i) {
        BwaLoadedApp *app = &bwa_instances[i];
        if (app->open && app->definition.request_close) {
            bwa_callback_app = app;
            int allow = app->definition.request_close();
            bwa_callback_app = NULL;
            if (!allow) {
                active_window = app->definition.runtime_id;
                app->window.minimized = 0;
                return 0;
            }
        }
    }
    return 1;
}

static void toggle_maximize_editor(void)
{
    window_toggle_maximize_state(&editor_window);
    editor_ensure_cursor_visible();
}

static void explorer_rubber_update(int mouse_x, int mouse_y, int page)
{
    int max_x = explorer_client_x() + explorer_client_w() - 1;
    int max_y = explorer_client_y() + explorer_client_h() - 13;
    if (mouse_x < explorer_client_x()) mouse_x = explorer_client_x();
    if (mouse_x > max_x) mouse_x = max_x;
    if (mouse_y < explorer_client_y()) mouse_y = explorer_client_y();
    if (mouse_y > max_y) mouse_y = max_y;
    BudoSelectionRect rects[MAX_ITEMS];
    int count = 0;
    for (int slot = 0; slot < explorer_slots() && count < MAX_ITEMS; ++slot) {
        int index = item_for_slot(page, slot), x, y;
        if (index < 0) continue;
        slot_position(slot, &x, &y);
        rects[count++] = explorer_list_view ? (BudoSelectionRect){index, x, y, explorer_client_w(), 12} :
                         (BudoSelectionRect){index, x - 20, y - 2, 72, 42};
    }
    budo_selection_drag_update(&explorer_select, mouse_x, mouse_y, rects, count);
}

static void shortcut_open(int index, int page)
{
    (void)page;
    DesktopItem *entry = &desktop_shortcuts[index];
    if (entry->type == TYPE_FOLDER) {
        if (builtin_new_instance(BWA_HOST_APP_EXPLORER) && load_directory(entry->path)) {
            open_explorer();
            active_window = APP_EXPLORER;
            budo_selection_clear(&desktop_select);
        }
        return;
    }
    if (executable_path(entry->path)) {
        char directory[MAX_PATH];
        if (!copy_text(directory, sizeof(directory), entry->path)) return;
        char *slash = strrchr(directory, '/');
        if (slash) {
            if (slash == directory) slash[1] = '\0';
            else *slash = '\0';
        }
        shortcut_launch_requested = desktop_launch_program(entry->path, directory);
    } else (void)open_associated_file(entry->path);
}

static int shortcut_icon_load(int index)
{
    ShortcutDetails *details = &shortcut_details[index];
    details->icon_loaded = 0;
    if (!details->icon[0]) return 1;
    if (!load_pcx_image(details->icon, DESKTOP_ICON_W, DESKTOP_ICON_H, details->pixels, details->rgb)) {
        fprintf(stderr, "Unable to load shortcut icon: %s\n", details->icon);
        return 0;
    }
    uint32_t transparent = details->rgb[0];
    for (int i = 0; i < DESKTOP_ICON_W * DESKTOP_ICON_H; ++i)
        if (details->rgb[i] == transparent) details->pixels[i] = PCX_TRANSPARENT;
    details->icon_loaded = 1;
    return 1;
}

static int shortcuts_save(void)
{
    char path[MAX_PATH], temporary[MAX_PATH];
    if (!copy_text(path, sizeof(path), bw_state_file("shortcuts.state")) ||
        !copy_text(temporary, sizeof(temporary), bw_state_file("shortcuts.tmp"))) return 0;
    FILE *file = fopen(temporary, "wb");
    if (!file) { perror("Save desktop shortcuts"); return 0; }
    int ok = fprintf(file, "BUDOWIN shortcuts 2\n%d\n", shortcut_count) > 0;
    for (int i = 0; i < shortcut_count && ok; ++i) {
        char target[MAX_PATH] = {0}, parent[MAX_PATH] = {0}, icon[MAX_PATH] = {0};
        ok = copy_text(target, sizeof(target), desktop_shortcuts[i].path) &&
             copy_text(parent, sizeof(parent), shortcut_details[i].parent) &&
             copy_text(icon, sizeof(icon), shortcut_details[i].icon);
        if (ok) ok = fprintf(file, "%d %d\n", shortcut_slots[i], desktop_shortcuts[i].type) > 0 &&
                     fwrite(target, 1, MAX_PATH, file) == MAX_PATH &&
                     fwrite(parent, 1, MAX_PATH, file) == MAX_PATH &&
                     fwrite(icon, 1, MAX_PATH, file) == MAX_PATH;
    }
    if (fclose(file) != 0) ok = 0;
    if (ok && rename(temporary, path) == 0) return 1;
    perror("Save desktop shortcuts");
    (void)remove(temporary);
    return 0;
}

static void shortcuts_load(void)
{
    FILE *file = fopen(bw_state_file("shortcuts.state"), "rb");
    if (!file) return;
    char header[64];
    int count, version = 0;
    shortcut_count = 0;
    memset(shortcut_details, 0, sizeof(shortcut_details));
    if (fgets(header, sizeof(header), file)) {
        if (!strcmp(header, "BUDOWIN shortcuts 1\n")) version = 1;
        if (!strcmp(header, "BUDOWIN shortcuts 2\n")) version = 2;
    }
    if (!version || fscanf(file, "%d", &count) != 1 || fgetc(file) != '\n' || count < 0 || count > DESKTOP_SHORTCUT_MAX) {
        fprintf(stderr, "Invalid desktop shortcut state\n");
        fclose(file);
        return;
    }
    for (int i = 0; i < count; ++i) {
        DesktopItem entry = {{0}, {0}, TYPE_FILE};
        ShortcutDetails *details = &shortcut_details[shortcut_count];
        int slot, ok = fscanf(file, "%d", &slot) == 1;
        if (version == 2) ok = ok && fscanf(file, "%d", &entry.type) == 1;
        ok = ok && fgetc(file) == '\n' && fread(entry.path, 1, MAX_PATH, file) == MAX_PATH;
        if (ok && version == 2)
            ok = fread(details->parent, 1, MAX_PATH, file) == MAX_PATH &&
                 fread(details->icon, 1, MAX_PATH, file) == MAX_PATH;
        if (!ok || !memchr(entry.path, '\0', MAX_PATH) || entry.path[0] != '/' ||
            !memchr(details->parent, '\0', MAX_PATH) || !memchr(details->icon, '\0', MAX_PATH) ||
            (entry.type != TYPE_FILE && entry.type != TYPE_FOLDER) || slot < 0 || slot >= desktop_slot_count()) {
            fprintf(stderr, "Invalid desktop shortcut record\n");
            shortcut_count = 0;
            break;
        }
        const char *name = strrchr(entry.path, '/');
        if (!name || !copy_text(entry.name, sizeof(entry.name), name + 1)) continue;
        desktop_shortcuts[shortcut_count] = entry;
        shortcut_slots[shortcut_count] = slot;
        (void)shortcut_icon_load(shortcut_count++);
    }
    fclose(file);
}

static int desktop_free_slot(const char *parent, int ignore)
{
    for (int slot = 0; slot < desktop_slot_count(); ++slot) {
        int occupied = !parent[0] && (slot == explorer_desktop_slot || slot == editor_desktop_slot);
        if (!parent[0]) for (int j = 0; j < bwa_external_app_count; ++j)
            if (bwa_external_apps[j].desktop_slot == slot) occupied = 1;
        for (int j = 0; j < shortcut_count; ++j)
            if (j != ignore && !strcmp(shortcut_details[j].parent, parent) && shortcut_slots[j] == slot) occupied = 1;
        if (!occupied) return slot;
    }
    return -1;
}

static int shortcut_move_impl(int index, const char *parent, int persist)
{
    if (index < 0 || index >= shortcut_count) return 0;
    char ancestor[MAX_PATH];
    if (!copy_text(ancestor, sizeof(ancestor), parent)) return 0;
    for (int depth = 0; ancestor[0] && depth <= shortcut_count; ++depth) {
        if (!strcmp(ancestor, desktop_shortcuts[index].path)) return 0;
        int found = -1;
        for (int j = 0; j < shortcut_count; ++j)
            if (desktop_shortcuts[j].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[j].path, ancestor)) found = j;
        if (found < 0 || depth == shortcut_count) return 0;
        (void)copy_text(ancestor, sizeof(ancestor), shortcut_details[found].parent);
    }
    int slot = desktop_free_slot(parent, index);
    if (slot < 0) return 0;
    char previous[MAX_PATH];
    int previous_slot = shortcut_slots[index];
    (void)copy_text(previous, sizeof(previous), shortcut_details[index].parent);
    (void)copy_text(shortcut_details[index].parent, MAX_PATH, parent);
    shortcut_slots[index] = slot;
    if (!persist || shortcuts_save()) {
        if (persist) budo_selection_clear(&desktop_select);
        return 1;
    }
    (void)copy_text(shortcut_details[index].parent, MAX_PATH, previous);
    shortcut_slots[index] = previous_slot;
    return 0;
}

static int shortcut_move(int index, const char *parent)
{
    return shortcut_move_impl(index, parent, 1);
}

static const char *shortcut_drop_parent(int x, int y)
{
    int owner = desktop_point_owner(x, y);
    if (owner == APP_EXPLORER && explorer_shortcut_folder[0] &&
        point_in_rect(x, y, explorer_client_x(), explorer_client_y(),
                      explorer_client_w(), explorer_client_h() - BUDO_SCROLL_WIDTH))
        return explorer_shortcut_folder;
    if (owner != APP_NONE) return NULL;
    int folder = shortcut_hit(x, y);
    return folder >= 0 && desktop_shortcuts[folder].type == TYPE_FOLDER ?
        desktop_shortcuts[folder].path : desktop_folder;
}

static void explorer_shortcut_drag_begin(int item, int x, int y)
{
    if (recycle_bin) return;
    explorer_drag_shortcut = -1;
    explorer_shortcut_dragging = 0;
    if (item < 0 || item >= item_count || !explorer_shortcut_folder[0]) return;
    for (int i = 0; i < shortcut_count; ++i) {
        if (strcmp(shortcut_details[i].parent, current_path) ||
            strcmp(desktop_shortcuts[i].path, directory_items[item].path)) continue;
        explorer_drag_shortcut = i;
        explorer_drag_press_x = x;
        explorer_drag_press_y = y;
        explorer_drag_x = x - DESKTOP_ICON_W / 2;
        explorer_drag_y = y - DESKTOP_ICON_H / 2;
        return;
    }
}

static int explorer_shortcut_drag_update(int x, int y, int down)
{
    if (!down || explorer_drag_shortcut < 0) return 0;
    int dx = x - explorer_drag_press_x, dy = y - explorer_drag_press_y;
    if (!explorer_shortcut_dragging && dx <= 3 && dx >= -3 && dy <= 3 && dy >= -3) return 0;
    explorer_shortcut_dragging = 1;
    explorer_drag_x = x - DESKTOP_ICON_W / 2;
    explorer_drag_y = y - DESKTOP_ICON_H / 2;
    return 1;
}

static int explorer_shortcut_drag_drop(int x, int y)
{
    int index = explorer_drag_shortcut;
    int dragging = explorer_shortcut_dragging;
    explorer_drag_shortcut = -1;
    explorer_shortcut_dragging = 0;
    if (!dragging || index < 0 || index >= shortcut_count) return 0;
    const char *parent = shortcut_drop_parent(x, y);
    if (!parent || !strcmp(shortcut_details[index].parent, parent)) return 0;
    if (!shortcut_move(index, parent)) {
        explorer_status = "Unable to move shortcut; check free desktop slots and settings permissions.";
        return 0;
    }
    char refresh[MAX_PATH];
    if (copy_text(refresh, sizeof(refresh), current_path)) (void)load_directory(refresh);
    explorer_status = "Shortcut moved; target file preserved.";
    return 1;
}

static void desktop_folder_up(void)
{
    for (int i = 0; i < shortcut_count; ++i) {
        if (desktop_shortcuts[i].type == TYPE_FOLDER && !strcmp(desktop_shortcuts[i].path, desktop_folder)) {
            (void)copy_text(desktop_folder, sizeof(desktop_folder), shortcut_details[i].parent);
            budo_selection_clear(&desktop_select);
            return;
        }
    }
    desktop_folder[0] = '\0';
    budo_selection_clear(&desktop_select);
}

static void desktop_begin_picker(int action)
{
    if (recycle_bin && action == 5) return;
    desktop_picker_shortcut = desktop_select.focus - DESKTOP_SHORTCUT_BASE;
    editor_begin_file_dialog(action == 2 ? EDITOR_FILE_DIALOG_OPEN : EDITOR_FILE_DIALOG_SAVE_AS);
    picker_owner = APP_NONE;
    desktop_picker_action = action;
    picker_filter = action == 2 ? 3 : 0;
    char root[MAX_PATH];
    (void)copy_text(root, sizeof(root), bw_state_file("Desktop"));
    if (mkdir(root, 0700) != 0 && errno != EEXIST) { perror(root); picker_cancel(); return; }
    (void)editor_file_load_directory(action == 5 ? current_path : action == 2 ? bw_user_directory() : desktop_folder[0] ? desktop_folder : root);
    editor_set_file_name(action == 1 ? "New Folder" : action >= 4 ? "New File" : "");
    if (action >= 4) picker_filter = 1;
    picker_name_selected = 1;
    editor_set_status(action == 3 ? "Navigate into a folder, then press Move. Root = Desktop." : "");
}

/* O_EXCL prevents a new-file operation from replacing existing content. */
static int desktop_create_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { perror(path); editor_set_status("Unable to create file (name may already exist)"); return 0; }
    unsigned char pcx[899] = {0};
    const char *rtf = "{\\rtf1\\ansi\n}\n";
    const void *data = "";
    size_t length = 0;
    const char *ext = strrchr(path, '.');
    if (ext && compare_names_ci(ext, ".rtf") == 0) { data = rtf; length = strlen(rtf); }
    if (ext && compare_names_ci(ext, ".pcx") == 0) {
        pcx[0] = 10;
        pcx[1] = 5;
        pcx[2] = 1;
        pcx[3] = 8;
        pcx[65] = 1;
        pcx[66] = 2;
        pcx[68] = 1;
        pcx[128] = 4;
        pcx[129] = 4;
        pcx[130] = 12;
        for (int i = 0; i < 256; ++i) {
            unsigned int rgb = budo_palette_rgb(i);
            pcx[131 + i * 3] = (unsigned char)(rgb >> 16);
            pcx[132 + i * 3] = (unsigned char)(rgb >> 8);
            pcx[133 + i * 3] = (unsigned char)rgb;
        }
        data = pcx;
        length = sizeof(pcx);
    }
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, (const char *)data + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror(path); break; }
        offset += (size_t)n;
    }
    int ok = offset == length;
    if (close(fd) != 0) { perror(path); ok = 0; }
    if (!ok) { if (unlink(path) != 0) perror(path); editor_set_status("Unable to write new file"); }
    return ok;
}

static int desktop_picker_accept(const char *path)
{
    int action = desktop_picker_action;
    if (action == 5) {
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        if (!explorer_rename_name_valid(name) || !desktop_create_file(path)) return 0;
        (void)load_directory(current_path);
    } else if (action == 2) {
        int index = desktop_picker_shortcut;
        if (index < 0 || index >= shortcut_count) return 0;
        ShortcutDetails *previous = malloc(sizeof(*previous));
        if (!previous) { editor_set_status("Not enough memory"); return 0; }
        *previous = shortcut_details[index];
        int ok = copy_text(shortcut_details[index].icon, MAX_PATH, path) && shortcut_icon_load(index) && shortcuts_save();
        if (!ok) shortcut_details[index] = *previous;
        free(previous);
        if (!ok) { editor_set_status("Choose a valid 32 x 32 PCX; unable to save icon"); return 0; }
    } else if (action == 1 || action == 4) {
        char root[MAX_PATH], target[MAX_PATH], name[MAX_NAME];
        (void)path;
        (void)copy_text(root, sizeof(root), bw_state_file("Desktop"));
        const char *parent = !strcmp(editor_file_path, root) ? "" : editor_file_path;
        int slot = desktop_free_slot(parent, -1);
        const char *file_name = strrchr(path, '/');
        file_name = file_name ? file_name + 1 : path;
        if (!(action == 4 ? copy_text(name, sizeof(name), file_name) : bw_text_encode(name, sizeof(name), editor_file_name)) ||
            !explorer_rename_name_valid(name) ||
            !strcmp(name, ".") || !strcmp(name, "..") || slot < 0 || shortcut_count == DESKTOP_SHORTCUT_MAX ||
            !join_path(target, sizeof(target), parent[0] ? parent : root, name)) {
            editor_set_status("Invalid folder name or desktop is full"); return 0;
        }
        for (int i = 0; i < shortcut_count; ++i)
            if (!strcmp(target, desktop_shortcuts[i].path)) { editor_set_status("Folder already exists"); return 0; }
        if (action == 4) {
            if (!desktop_create_file(target)) return 0;
        } else if (mkdir(target, 0700) != 0) { perror(target); editor_set_status("Unable to create folder"); return 0; }
        int index = shortcut_count++;
        memset(&shortcut_details[index], 0, sizeof(shortcut_details[index]));
        (void)copy_text(shortcut_details[index].parent, MAX_PATH, parent);
        (void)copy_text(desktop_shortcuts[index].name, MAX_NAME, name);
        (void)copy_text(desktop_shortcuts[index].path, MAX_PATH, target);
        desktop_shortcuts[index].type = action == 4 ? TYPE_FILE : TYPE_FOLDER;
        shortcut_slots[index] = slot;
        if (!shortcuts_save()) {
            --shortcut_count;
            if ((action == 4 ? unlink(target) : rmdir(target)) != 0) perror(target);
            editor_set_status("Unable to save desktop item"); return 0;
        }
    } else if (action == 3) {
        char root[MAX_PATH];
        (void)copy_text(root, sizeof(root), bw_state_file("Desktop"));
        const char *parent = !strcmp(path, root) ? "" : path;
        unsigned char selected[DESKTOP_SHORTCUT_MAX];
        int slots[DESKTOP_SHORTCUT_MAX];
        ShortcutDetails *previous = malloc(sizeof(shortcut_details));
        if (!previous) { editor_set_status("Not enough memory"); return 0; }
        memcpy(previous, shortcut_details, sizeof(shortcut_details));
        memcpy(slots, shortcut_slots, sizeof(slots));
        memcpy(selected, shortcut_selection, sizeof(selected));
        int moved = 0, ok = 1;
        for (int i = 0; i < shortcut_count; ++i) {
            if (selected[i]) {
                if (!shortcut_move_impl(i, parent, 0)) { ok = 0; break; }
                moved = 1;
            }
        }
        if (!moved || !ok || !shortcuts_save()) {
            memcpy(shortcut_details, previous, sizeof(shortcut_details));
            memcpy(shortcut_slots, slots, sizeof(slots));
            free(previous);
            editor_set_status("Move failed: full folder, invalid destination, cycle, or save error");
            return 0;
        }
        free(previous);
        budo_selection_clear(&desktop_select);
    } else return 0;
    desktop_picker_action = 0;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    return 1;
}

static int shortcuts_create(void)
{
    int old_count = shortcut_count;
    for (int i = 0; i < item_count; ++i) {
        if (!explorer_selection[i] || directory_items[i].type != TYPE_FILE) continue;
        int duplicate = 0;
        for (int j = 0; j < shortcut_count; ++j)
            if (!strcmp(directory_items[i].path, desktop_shortcuts[j].path)) duplicate = 1;
        if (duplicate) continue;
        int slot = desktop_free_slot("", -1);
        if (slot < 0 || shortcut_count == DESKTOP_SHORTCUT_MAX) {
            fprintf(stderr, "Desktop has no free shortcut slots\n");
            break;
        }
        memset(&shortcut_details[shortcut_count], 0, sizeof(shortcut_details[shortcut_count]));
        desktop_shortcuts[shortcut_count] = directory_items[i];
        shortcut_slots[shortcut_count++] = slot;
    }
    if (shortcut_count == old_count) return 0;
    if (shortcuts_save()) return 1;
    shortcut_count = old_count;
    return 0;
}

static int shortcut_hit(int x, int y)
{
    for (int i = 0; i < shortcut_count; ++i) {
        if (strcmp(shortcut_details[i].parent, desktop_folder)) continue;
        int sx, sy;
        desktop_slot_position(shortcut_slots[i], &sx, &sy);
        if (point_in_rect(x, y, sx - 20, sy - 2, 72, 42)) return i;
    }
    return -1;
}

static int desktop_point_owner(int x, int y)
{
    int ids[DESKTOP_RUNNING_MAX];
    AppWindow *windows[DESKTOP_RUNNING_MAX];
    const char *names[DESKTOP_RUNNING_MAX];
    int count = desktop_window_order(ids, windows, names);
    for (int i = count - 1; i >= 0; --i) {
        BwaLoadedApp *native = bwa_find_external_app(ids[i]);
        if (native && !native->managed_window) continue;
        if (!windows[i]->minimized && window_contains(windows[i], x, y)) return ids[i];
    }
    return APP_NONE;
}

static int desktop_item_slot(int id)
{
    if (id == 0) return explorer_desktop_slot;
    if (id == 1) return editor_desktop_slot;
    if (id >= DESKTOP_SHORTCUT_BASE) {
        int index = id - DESKTOP_SHORTCUT_BASE;
        return index < shortcut_count ? shortcut_slots[index] : -1;
    }
    int index = id - 2;
    return index >= 0 && index < bwa_external_app_count ?
           bwa_external_apps[index].desktop_slot : -1;
}

static int desktop_item_visible(int id)
{
    if (id < DESKTOP_SHORTCUT_BASE && desktop_folder[0]) return 0;
    if (id == 0) return !bwa_has_external_app_id("explorer") &&
                        1;
    if (id == 1) return !bwa_has_external_app_id("editor") &&
                        1;
    if (id >= DESKTOP_SHORTCUT_BASE) return id - DESKTOP_SHORTCUT_BASE < shortcut_count &&
        !strcmp(shortcut_details[id - DESKTOP_SHORTCUT_BASE].parent, desktop_folder);
    int index = id - 2;
    if (index < 0 || index >= bwa_external_app_count) return 0;
    BwaLoadedApp *app = &bwa_external_apps[index];
    (void)app;
    return 1;
}

static int desktop_order(int *order)
{
    int count = 0;
    for (int slot = 0; slot < desktop_slot_count(); ++slot) {
        for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
            if (desktop_item_visible(id) && desktop_item_slot(id) == slot)
                order[count++] = id;
        }
    }
    return count;
}

static int desktop_hit(int x, int y)
{
    /* Shortcuts precede launcher drawing, so launchers win overlapping hits. */
    for (int id = 0; id < DESKTOP_SHORTCUT_BASE; ++id) {
        if (!desktop_item_visible(id)) continue;
        int sx, sy;
        desktop_slot_position(desktop_item_slot(id), &sx, &sy);
        if (point_in_rect(x, y, sx - 20, sy - 2, 72, 50)) return id;
    }
    int shortcut = shortcut_hit(x, y);
    return shortcut < 0 ? -1 : DESKTOP_SHORTCUT_BASE + shortcut;
}

static void desktop_open_item(int id, int *page)
{
    if (!desktop_item_visible(id)) return;
    if (id >= DESKTOP_SHORTCUT_BASE) {
        shortcut_open(id - DESKTOP_SHORTCUT_BASE, *page);
    } else if (id == 0) {
        if (builtin_new_instance(BWA_HOST_APP_EXPLORER)) *page = 0;
    } else if (id == 1) {
        (void)builtin_new_instance(BWA_HOST_APP_EDITOR);
    } else {
        BwaLoadedApp *program = &bwa_external_apps[id - 2];
        BwaLoadedApp *app = program;
        if (!(program->definition.flags & BWA_FLAG_LAUNCHER)) {
            app = bwa_new_instance(program);
            if (!app) return;
        }
        int opened = 1;
        if (app->definition.callbacks.open) {
            bwa_callback_app = app;
            opened = app->definition.callbacks.open();
            if (!opened && app->definition.callbacks.close) app->definition.callbacks.close();
            bwa_callback_app = NULL;
        }
        if (opened && !(app->definition.flags & BWA_FLAG_LAUNCHER)) {
            app->open = 1;
            if (app->managed_window) window_open_state(&app->window);
            active_window = app->definition.runtime_id;
        }

    }
}

static int desktop_has_selected_shortcuts(void)
{
    for (int i = 0; i < shortcut_count; ++i) {
        if (shortcut_selection[i]) return 1;
    }
    return 0;
}

static int shortcuts_delete_selected(void)
{
    if (!desktop_has_selected_shortcuts()) return 0;
    DesktopItem backup[DESKTOP_SHORTCUT_MAX];
    int slots[DESKTOP_SHORTCUT_MAX];
    ShortcutDetails *details = malloc(sizeof(shortcut_details));
    if (!details) { perror("Delete shortcuts"); return 0; }
    memcpy(details, shortcut_details, sizeof(shortcut_details));
    unsigned char relocated[DESKTOP_SHORTCUT_MAX] = {0};
    int count = shortcut_count, keep = 0;
    for (int i = 0; i < count; ++i) {
        if (!shortcut_selection[i] || desktop_shortcuts[i].type != TYPE_FOLDER) continue;
        /* Preserve contained shortcuts by moving them to the visible parent. */
        for (int j = 0; j < count; ++j)
            if (!strcmp(shortcut_details[j].parent, desktop_shortcuts[i].path)) {
                (void)copy_text(shortcut_details[j].parent, MAX_PATH, desktop_folder);
                relocated[j] = 1;
            }
    }
    memcpy(backup, desktop_shortcuts, sizeof(backup));
    memcpy(slots, shortcut_slots, sizeof(slots));
    for (int i = 0; i < count; ++i) {
        if (shortcut_selection[i]) continue;
        desktop_shortcuts[keep] = desktop_shortcuts[i];
        shortcut_details[keep] = shortcut_details[i];
        shortcut_slots[keep] = relocated[i] ? -1 : shortcut_slots[i];
        ++keep;
    }
    shortcut_count = keep;
    int saved = 1;
    for (int i = 0; i < keep; ++i) {
        if (shortcut_slots[i] >= 0) continue;
        shortcut_slots[i] = desktop_free_slot(shortcut_details[i].parent, i);
        if (shortcut_slots[i] < 0) {
            fprintf(stderr, "Cannot delete desktop folder: parent has no free slots\n");
            saved = 0;
            break;
        }
    }
    if (saved) saved = shortcuts_save();
    if (!saved) {
        shortcut_count = count;
        memcpy(desktop_shortcuts, backup, sizeof(backup));
        memcpy(shortcut_slots, slots, sizeof(slots));
        memcpy(shortcut_details, details, sizeof(shortcut_details));
    } else {
        budo_selection_clear(&desktop_select);
    }
    free(details);
    desktop_last_click = -1;
    return saved;
}

static void desktop_selection_release(void)
{
    if (desktop_icon_pressed && !desktop_icon_dragging && desktop_select.focus >= 0) {
        int id = desktop_select.focus;
        budo_selection_clear(&desktop_select);
        desktop_select.selected[id] = 1;
        desktop_select.focus = desktop_select.anchor = id;
    }
}

#include "desktop_drag.h"

static int desktop_selection_pointer(int x, int y, int buttons, int *page)
{
    if (buttons == 2 && y < 21) return 0;
    if (confirm_kind || editor_file_dialog != EDITOR_FILE_DIALOG_NONE) return 0;
    if (context_menu && buttons == 1) {
        int menu = context_menu;
        int choice = budo_menu_hit(x, y, context_x, context_y, 156, 7);
        context_menu = 0;
        desktop_last_click = -1;
        if (choice < 0) return 1;
        if (choice == 2) desktop_begin_picker(menu == 1 ? 5 : 4);
        else if (choice == 3) {
            if (menu == 1) {
                explorer_creating_folder = explorer_rename_active = 1;
                explorer_rename_input[0] = 0;
                explorer_rename_len = 0;
            } else desktop_begin_picker(1);
        } else if (menu == 1) {
            if (choice == 1 && explorer_selected_item >= 0)
                (void)desktop_confirm(APP_EXPLORER, 3, "Delete selected files?", "Move the selected entries to Recycle Bin?");
            else if (choice == 4) {
                char parent[MAX_PATH];
                if (parent_path(parent, sizeof(parent)) && load_directory(parent)) *page = 0;
            } else if (choice == 5) (void)explorer_begin_rename();
            else if (choice == 6) (void)shortcuts_create();
            else if (choice == 0 && explorer_selected_item >= 0) {
                DesktopItem *entry = &directory_items[explorer_selected_item];
                if (entry->type == TYPE_FOLDER) { if (load_directory(entry->path)) *page = 0; }
                else if (executable_path(entry->path)) shortcut_launch_requested = request_launch(entry, *page);
                else if (recycle_bin) (void)bwa_open_reader_file(entry->path);
                else (void)open_associated_file(entry->path);
            }
        } else {
            if (choice == 0 && desktop_select.focus >= 0) desktop_open_item(desktop_select.focus, page);
            else if (choice == 1) (void)shortcuts_delete_selected();
            else if (choice == 4) desktop_folder_up();
            else if (choice == 5 && desktop_select.focus >= DESKTOP_SHORTCUT_BASE) desktop_begin_picker(2);
            else if (choice == 6 && desktop_has_selected_shortcuts()) desktop_begin_picker(3);
        }
        return 1;
    }
    int owner = desktop_point_owner(x, y);
    if (owner == APP_NONE && buttons == 1 && desktop_folder[0] && point_in_rect(x, y, 12, 24, 84, 18)) {
        desktop_folder_up();
        return 1;
    }
    if (owner != APP_NONE && owner != APP_EXPLORER) return 0;
    unsigned int modifiers = keyboard_modifiers();
    if (owner == APP_EXPLORER) {
        if (buttons == 1) {
            if (!explorer_rename_active && point_to_slot(x, y) >= 0)
                active_window = APP_EXPLORER;
            return 0;
        }
        if (buttons != 2 || explorer_rename_active) return 0;
        if (recycle_bin) return 1;
        int slot = point_to_slot(x, y);
        int index = slot >= 0 ? item_for_slot(*page, slot) : -1;
        if (!point_in_rect(x, y, explorer_window.x + WINDOW_BORDER,
                           explorer_window.y + WINDOW_TITLE_H + EDITOR_MENU_H,
                           explorer_window.w - WINDOW_BORDER * 2,
                           explorer_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - WINDOW_BORDER)) return 0;
        explorer_folder_keyboard = 0;
        explorer_folder_last_click[0] = 0;
        int order[MAX_ITEMS];
        int count = explorer_order(order);
        budo_selection_click(&explorer_select, order, count, index, modifiers, 1);
        active_window = APP_EXPLORER;
        explorer_file_menu = explorer_view_menu = terminal_file_menu = 0;
        context_menu = 1;
    } else {
        int order[DESKTOP_SELECTION_MAX];
        int count = desktop_order(order);
        int id = desktop_hit(x, y);
        active_window = APP_NONE;
        explorer_file_menu = explorer_view_menu = terminal_file_menu = 0;
        if (id < 0) {
            context_menu = buttons == 2 ? 2 : 0;
            context_x = x < SCREEN_WIDTH - 156 ? x : SCREEN_WIDTH - 156;
            context_y = y < SCREEN_HEIGHT - 116 ? y : SCREEN_HEIGHT - 116;
            desktop_last_click = -1;
            if (buttons == 1) budo_selection_drag_begin(&desktop_select, x, y, modifiers);
            else budo_selection_click(&desktop_select, order, count, -1, modifiers, 1);
            return 1;
        }
        if (buttons != 1 || modifiers & (KEYMOD_CTRL | KEYMOD_SHIFT) || !desktop_select.selected[id])
            budo_selection_click(&desktop_select, order, count, id, modifiers, buttons == 2);
        else desktop_select.focus = id;
        if (buttons == 2) {
            context_menu = 2;
            desktop_last_click = -1;
        } else {
            clock_t now = bw_clock();
            int plain = !(modifiers & (KEYMOD_CTRL | KEYMOD_SHIFT));
            if (plain && desktop_last_click == id &&
                now - desktop_click_time <= CLOCKS_PER_SEC / 2) {
                desktop_last_click = -1;
                desktop_icon_pressed = 0;
                desktop_open_item(id, page);
                return 1;
            }
            desktop_last_click = plain ? id : -1;
            desktop_click_time = now;
            desktop_icon_pressed = plain;
            desktop_icon_dragging = 0;
            desktop_press_x = x;
            desktop_press_y = y;
            desktop_slot_position(desktop_item_slot(id), &desktop_drag_x, &desktop_drag_y);
            desktop_drag_origin_x = desktop_drag_x;
            desktop_drag_origin_y = desktop_drag_y;
            desktop_drag_offset_x = desktop_drag_offset_y = 0;
            for (int i = 0; i < DESKTOP_SELECTION_MAX; ++i)
                desktop_drag_slots[i] = desktop_select.selected[i] && desktop_item_visible(i) ? desktop_item_slot(i) : -1;
            desktop_drag_dx = x - desktop_drag_x;
            desktop_drag_dy = y - desktop_drag_y;
            if (id == 0) desktop_drag_app = APP_EXPLORER;
            else if (id == 1) desktop_drag_app = APP_EDITOR;
            else if (id >= DESKTOP_SHORTCUT_BASE)
                desktop_drag_app = APP_SHORTCUT_BASE + id - DESKTOP_SHORTCUT_BASE;
            else desktop_drag_app = bwa_external_apps[id - 2].definition.runtime_id;
        }
    }
    context_x = x < SCREEN_WIDTH - 156 ? x : SCREEN_WIDTH - 156;
    int height = 116;
    context_y = y < SCREEN_HEIGHT - height ? y : SCREEN_HEIGHT - height;
    return 1;
}

static void desktop_selection_drag_update(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
    int order[DESKTOP_SELECTION_MAX];
    int count = desktop_order(order), visible = 0;
    BudoSelectionRect rects[DESKTOP_SELECTION_MAX];
    for (int i = 0; i < count; ++i) {
        int sx, sy;
        desktop_slot_position(desktop_item_slot(order[i]), &sx, &sy);
        if (desktop_point_owner(sx + DESKTOP_ICON_W / 2, sy + DESKTOP_ICON_H / 2) != APP_NONE)
            continue;
        rects[visible++] = (BudoSelectionRect){order[i], sx - 20, sy - 2, 72, 50};
    }
    budo_selection_drag_update(&desktop_select, x, y, rects, visible);
}

static int desktop_selection_key(int key, int *page)
{
    int order[DESKTOP_SELECTION_MAX];
    int count = desktop_order(order);
    unsigned int modifiers = keyboard_modifiers();
    if (key == 8 && desktop_folder[0]) desktop_folder_up();
    else if (key == 1) budo_selection_all(&desktop_select, order, count);
    else if (key == ' ') budo_selection_space(&desktop_select, order, count, modifiers);
    else if (key == 13) {
        if (desktop_select.focus >= 0) desktop_open_item(desktop_select.focus, page);
    } else if (key == 27) {
        if (desktop_select.focus < 0 && !desktop_select.dragging) return 0;
        budo_selection_clear(&desktop_select);
    } else if (key == 0) {
        int scan = getch();
        if (scan == 83) (void)shortcuts_delete_selected();
        else if (scan == 75 || scan == 77 || scan == 72 || scan == 80 ||
                 scan == 71 || scan == 79 || scan == 73 || scan == 81)
            (void)budo_selection_move(&desktop_select, order, count, scan,
                                      DESKTOP_GRID_COLS, desktop_slot_count(), modifiers);
    } else return 0;
    desktop_last_click = -1;
    return 1;
}

static void explorer_navigate(int scan, int *page)
{
    int order[MAX_ITEMS];
    int count = explorer_order(order);
    if (!is_root_path() && scan == 72 && !(bw_modifiers() & KEYMOD_SHIFT) &&
        (explorer_up_selected || (explorer_selected_item >= 0 &&
         budo_selection_position(order, count, explorer_selected_item) < explorer_cols()))) {
        explorer_clear_selection();
        explorer_up_selected = 1;
        *page = 0;
        return;
    }
    if (explorer_up_selected) {
        explorer_up_selected = 0;
        if (count) explorer_select_item(order[0], 0);
        *page = 0;
        return;
    }
    int position = budo_selection_move(&explorer_select, order, count, scan,
                                       explorer_cols(), entries_per_page(), keyboard_modifiers());
    if (position >= 0) {
        int row = (position + !is_root_path()) / explorer_cols();
        if (row < *page) *page = row;
        if (row >= *page + explorer_rows()) *page = row - explorer_rows() + 1;
        explorer_up_selected = 0;
    }
}

#define page (explorer_state->v_page)
int main(int argc, char **argv)
{
    int mouse_x = SCREEN_WIDTH / 2;
    int mouse_y = SCREEN_HEIGHT / 2;
    int previous_mouse_x = mouse_x;
    int previous_mouse_y = mouse_y;
    int buttons = 0;
    int previous_buttons = 0;
    page = 0;
    int last_target = -99;
    int screen_dirty = 1;
    int exit_status = 0;
    time_t desktop_second = 0;
    int caret_dirty = 0;
    int caret_drawn = 0;
    int cursor_drawn = 0;
    clock_t last_click_time = 0;

    if (!bw_initialize(argc, argv)) return 1;

    if (getcwd(home_path, sizeof(home_path)) == NULL) {
        return 1;
    }

    normalize_slashes(home_path);
    (void)remove(bw_state_file("modules.log"));
    load_editor_settings();
    bwa_load_external_apps();
    {
        FILE *file = fopen(bw_state_file("explorer-view.state"), "r");
        int view;
        if (file) {
            if (fscanf(file, "%d", &view) == 1 && (view == 0 || view == 1)) explorer_list_view = view;
            fclose(file);
        }
    }
    bw_load_keyboard_layout();
    load_file_associations();
    load_pcx_assets();
    if (!cursor_pcx_icon_loaded) {
        int cy, cx;
        memset(cursor_pcx_icon, PCX_TRANSPARENT, sizeof(cursor_pcx_icon));
        for (cy = 0; cy < 18; ++cy) {
            for (cx = 0; cx <= cy / 2; ++cx) {
                cursor_pcx_icon[cy * MOUSE_CURSOR_W + cx] =
                    (cx == 0 || cx == cy / 2 || cy == 17) ? 1 : 4;
            }
        }
        cursor_pcx_icon_loaded = 1;
    }
    load_desktop_layout();
    shortcuts_load();
    desktop_repair_overlaps();
    for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) desktop_drag_slots[id] = -1;

    if (!load_initial_directory(&page)) {
        return 1;
    }

    if (resume_explorer) {
        explorer_window.open = 1;
        active_window = APP_EXPLORER;
        clamp_page(&page);
    }

    terminal_load_resume_state();
    terminal_palette_load();

    if (!set_graphics_mode()) {
        return 1;
    }

    set_classic_gui_palette();
    for (int i = 0; i < 16; ++i) bw_palette_rgb((unsigned int)i, budo_palette_rgb(i));
    ui_colors_load();

    if (!mouse_init()) {
        set_text_mode();
        return 1;
    }

    if (terminal_window.open) open_terminal();
    for (;;) {
        if (!bw_begin_frame()) break;
        builtin_select(active_window);
        if (builtin_poll_terminals()) { desktop_focus_visible(); screen_dirty = 1; }
        builtin_select(active_window);
        time_t second = time(NULL);
        if (second != desktop_second) { desktop_second = second; screen_dirty = 1; }
        if (app_switch_until && bw_clock() >= app_switch_until) {
            app_switch_until = 0;
            screen_dirty = 1;
        }
        mouse_get_state(&mouse_x, &mouse_y, &buttons);

        ui_pointer_x = mouse_x;
        ui_pointer_y = mouse_y;
        ui_pointer_buttons = buttons;
        if ((buttons & 1) && !(previous_buttons & 1) && ui_scope() == 0) {
            int owner = desktop_point_owner(mouse_x, mouse_y);
            if (owner != APP_NONE) {
                active_window = owner;
                builtin_select(owner);
            }
        }
        int down = (buttons & 1) && !(previous_buttons & 1);
        int up = !(buttons & 1) && (previous_buttons & 1);
        int text_pointer = terminal_text_pointer(mouse_x, mouse_y, buttons, previous_buttons);
        int graphics_pointer = text_pointer ? 1 : session_pointer(mouse_x, mouse_y, buttons, previous_buttons);
        if (text_pointer) screen_dirty = 1;
        int edge_capture = 0;
        int hit = ui_hit(mouse_x, mouse_y, 0);
        if (down && !graphics_pointer && ui_scope() == 0 &&
            (hit < 0 || ui_regions[hit].owner != UI_OVERLAY_OWNER)) {
            AppWindow *w = window_at_point(mouse_x, mouse_y);
            if (w && window_begin_resize(w, mouse_x, mouse_y)) {
                active_window = desktop_point_owner(mouse_x, mouse_y);
                ui_captured = 0;
                edge_capture = 1;
            }
        }
        int command_down = graphics_pointer || edge_capture ? 0 : ui_button_event(mouse_x, mouse_y, down, up);
        if (down || up || mouse_x != previous_mouse_x || mouse_y != previous_mouse_y)
            screen_dirty = 1;
        if (mouse_x != ui_tip_x || mouse_y != ui_tip_y || buttons ||
            ui_tip_scope != ui_scope() || ui_tip_owner != active_window) {
            ui_tip_scope = ui_scope();
            ui_tip_owner = active_window;
            ui_tip_x = mouse_x;
            ui_tip_y = mouse_y;
            ui_tip_since = bw_clock();
            if (ui_tip_visible) screen_dirty = 1;
            ui_tip_visible = 0;
        } else if (!ui_tip_visible && bw_clock() - ui_tip_since >= CLOCKS_PER_SEC * 3 / 5) {
            ui_tip_visible = 1;
            screen_dirty = 1;
        }
        if (command_down) {
            clock_t now = bw_clock();
            int handled = 0;
            if (!confirm_kind && editor_file_dialog == EDITOR_FILE_DIALOG_NONE &&
                terminal_color_dialog && active_window == APP_TERMINAL) {
                (void)terminal_ui_click(mouse_x, mouse_y);
                handled = 1;
                screen_dirty = 1;
            } else if (!confirm_kind && editor_file_dialog == EDITOR_FILE_DIALOG_NONE && desktop_minimized_click(mouse_x, mouse_y)) {
                handled = 1;
                screen_dirty = 1;
            } else if (!confirm_kind && editor_file_dialog == EDITOR_FILE_DIALOG_NONE &&
                desktop_selection_pointer(mouse_x, mouse_y, 1, &page)) {
                last_target = -99;
                last_click_time = 0;
                handled = 1;
                screen_dirty = 1;
            } else if (confirm_kind) {
                desktop_confirm_click(mouse_x, mouse_y);
                clamp_page(&page);
                handled = 1;
                screen_dirty = 1;
            } else if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
                if (!desktop_scroll_pointer(mouse_x, mouse_y, BUDO_POINTER_DOWN, &page))
                    editor_file_dialog_click(mouse_x, mouse_y, now);
                handled = 1;
                screen_dirty = 1;
            } else if (desktop_file_menu_click(mouse_x, mouse_y, &page)) {
                handled = 1;
                screen_dirty = 1;
            } else if (desktop_scroll_pointer(mouse_x, mouse_y, BUDO_POINTER_DOWN, &page)) {
                handled = 1;
                screen_dirty = 1;
            }

            if (shortcut_launch_requested) { shortcut_launch_requested = 0; screen_dirty = 1; }

            if (!handled && active_window == APP_TERMINAL &&
                window_contains(&terminal_window, mouse_x, mouse_y)) {
                handled = terminal_mouse_down(mouse_x, mouse_y);
                if (handled) {
                    screen_dirty = 1;
                }
            }

            if (!handled &&
                active_window == APP_EDITOR &&
                window_contains(&editor_window, mouse_x, mouse_y)) {
                int min_x = window_min_button_x(&editor_window);
                int max_x = window_max_button_x(&editor_window);
                int close_x = window_close_button_x(&editor_window);
                int control_x = min_x - 154;
                int wrap_x = control_x;
                int rows_x = control_x + 48;
                int writer_x = control_x + 96;
                int menu_y = editor_window.y + WINDOW_TITLE_H;

                if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
                    editor_file_dialog_click(mouse_x, mouse_y, now);
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_dialog != EDITOR_DIALOG_NONE) {
                    if (editor_search_dialog_active()) {
                        editor_search_click(mouse_x, mouse_y);
                        screen_dirty = 1;
                    }
                    handled = 1;
                } else if (reader_menu_click(mouse_x, mouse_y)) {
                    screen_dirty = handled = 1;
                } else if (!editor_writer_mode &&
                           point_in_rect(mouse_x, mouse_y,
                                         wrap_x, editor_window.y + 4,
                                         48, 12)) {
                    editor_word_wrap = !editor_word_wrap;
                    editor_left_col = 0;
                    editor_ensure_cursor_visible();
                    (void)save_editor_settings();
                    screen_dirty = 1;
                    handled = 1;
                } else if (!editor_writer_mode &&
                           point_in_rect(mouse_x, mouse_y,
                                         rows_x, editor_window.y + 4,
                                         48, 12)) {
                    editor_show_row_numbers =
                        !editor_show_row_numbers;
                    editor_ensure_cursor_visible();
                    (void)save_editor_settings();
                    screen_dirty = 1;
                    handled = 1;
                } else if (!editor_read_only && point_in_rect(mouse_x, mouse_y,
                                         writer_x, editor_window.y + 4,
                                         58, 12)) {
                    editor_writer_mode = !editor_writer_mode;
                    editor_left_col = 0;
                    editor_top_line = 0;
                    editor_top_segment = 0;
                    editor_preferred_visual_col = -1;
                    editor_ensure_cursor_visible();
                    (void)save_editor_settings();
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 5, menu_y,
                                         32, EDITOR_MENU_H)) {
                    editor_menu = EDITOR_MENU_FILE;
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 41, menu_y,
                                         32, EDITOR_MENU_H)) {
                    editor_menu = EDITOR_MENU_EDIT;
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 77, menu_y,
                                         48, EDITOR_MENU_H)) {
                    editor_menu = EDITOR_MENU_SEARCH;
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_writer_mode &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 113, menu_y,
                                         56, EDITOR_MENU_H)) {
                    editor_menu = EDITOR_MENU_DOCUMENT;
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_FILE &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 5,
                                         menu_y + EDITOR_MENU_H,
                                         156,
                                         editor_writer_mode ? 100 : 84)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 2)) / 16;
                    editor_menu = EDITOR_MENU_NONE;

                    if (item == 0) {
                        editor_request_action(1, NULL);
                    } else if (item == 1) {
                        editor_request_action(2, NULL);
                    } else if (item == 2) {
                        editor_save_current();
                    } else if (item == 3) {
                        editor_begin_file_dialog(
                            EDITOR_FILE_DIALOG_SAVE_AS);
                    } else if (editor_writer_mode && item == 4) {
                        editor_begin_file_dialog(
                            EDITOR_FILE_DIALOG_EXPORT);
                    } else if ((!editor_writer_mode && item == 4) ||
                               (editor_writer_mode && item == 5)) {
                        close_editor();
                    }
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_EDIT &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 41,
                                         menu_y + EDITOR_MENU_H,
                                         156, 132)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 2)) / 16;
                    editor_menu = EDITOR_MENU_NONE;
                    if (item == 0)
                        editor_history_restore(0);
                    else if (item == 1)
                        editor_history_restore(1);
                    else if (item == 2)
                        editor_delete_line();
                    else if (item == 3 && editor_history_before_edit()) {
                        editor_selection_clear();
                        memset(editor_lines, 0, sizeof(editor_lines));
                        editor_line_count = 1;
                        editor_cursor_line = 0;
                        editor_cursor_col = 0;
                        editor_top_line = 0;
                        editor_top_segment = 0;
                        editor_left_col = 0;
                        editor_set_status("Document cleared");
                    }
                    if (item == 4 || item == 5) editor_selection_copy(item == 4);
                    else if (item == 6) editor_paste();
                    else if (item == 7) {
                        editor_anchor_line = editor_anchor_col = 0;
                        editor_cursor_line = editor_line_count - 1;
                        editor_cursor_col = (int)strlen(editor_lines[editor_cursor_line]);
                        editor_selection_active = 1;
                        editor_ensure_cursor_visible();
                    }
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_SEARCH &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 77,
                                         menu_y + EDITOR_MENU_H,
                                         156, 68)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 2)) / 16;
                    editor_menu = EDITOR_MENU_NONE;
                    if (item == 0)
                        editor_begin_dialog(EDITOR_DIALOG_FIND, "");
                    else if (item == 1)
                        editor_begin_dialog(EDITOR_DIALOG_REPLACE_FIND, "");
                    else if (item == 2)
                        editor_search_action(0);
                    else if (item == 3)
                        editor_search_action(1);
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_DOCUMENT &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 113,
                                         menu_y + EDITOR_MENU_H,
                                         156, 20)) {
                    editor_writer_ruler = !editor_writer_ruler;
                    editor_menu = EDITOR_MENU_NONE;
                    editor_ensure_cursor_visible();
                    (void)save_editor_settings();
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu != EDITOR_MENU_NONE) {
                    editor_menu = EDITOR_MENU_NONE;
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_writer_mode &&
                           editor_writer_ruler &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_text_x(),
                                         editor_window.y + WINDOW_TITLE_H +
                                         EDITOR_MENU_H + 2,
                                         EDITOR_WRITER_PAGE_W,
                                         EDITOR_RULER_H)) {
                    int ruler_x = editor_text_x();
                    int col = (mouse_x - ruler_x + 3) / 6;
                    int left_col = editor_writer_left_indent;
                    int first_col = editor_writer_left_indent +
                                    editor_writer_first_indent;
                    int right_col =
                        EDITOR_WRITER_PAGE_COLS -
                        editor_writer_right_indent;

                    if (col >= left_col - 1 && col <= left_col + 1 &&
                        mouse_y >= editor_window.y + WINDOW_TITLE_H +
                                   EDITOR_MENU_H + 11) {
                        editor_ruler_drag = 1;
                    } else if (col >= first_col - 1 &&
                               col <= first_col + 1 &&
                               mouse_y < editor_window.y + WINDOW_TITLE_H +
                                         EDITOR_MENU_H + 11) {
                        editor_ruler_drag = 2;
                    } else if (col >= right_col - 1 &&
                               col <= right_col + 1 &&
                               mouse_y >= editor_window.y + WINDOW_TITLE_H +
                                          EDITOR_MENU_H + 11) {
                        editor_ruler_drag = 3;
                    } else {
                        editor_writer_toggle_tab(col);
                    }
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         editor_text_x(),
                                         editor_window.y + WINDOW_TITLE_H +
                                         EDITOR_MENU_H +
                                         ((editor_writer_mode &&
                                           editor_writer_ruler) ? EDITOR_RULER_H : 0) + 5,
                                         editor_window.x + editor_window.w -
                                         8 - editor_text_x(),
                                         editor_window.h - WINDOW_TITLE_H -
                                         EDITOR_MENU_H - EDITOR_STATUS_H -
                                         ((editor_writer_mode &&
                                           editor_writer_ruler) ? EDITOR_RULER_H : 0) - 8)) {
                    editor_text_click(mouse_x, mouse_y, now, bw_modifiers());
                    text_caret_visible = 1;
                    text_caret_last_toggle = now;
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                  close_x, editor_window.y + WINDOW_BORDER, 14, 14)) {
                    close_editor();
                    screen_dirty = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         max_x, editor_window.y + WINDOW_BORDER, 14, 14)) {
                    toggle_maximize_editor();
                    screen_dirty = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         min_x, editor_window.y + WINDOW_BORDER, 14, 14)) {
                    minimize_editor();
                    screen_dirty = 1;
                } else if (window_begin_resize(&editor_window, mouse_x, mouse_y)) {
                    screen_dirty = 1;
                } else if (point_in_rect(mouse_x, mouse_y, editor_window.x + 2,
                                         editor_window.y + 2, editor_window.w - 52, WINDOW_TITLE_H - 2)) {
                    window_begin_drag(&editor_window, mouse_x, mouse_y);
                }
                handled = 1;
            } else if (!handled && active_window == APP_EXPLORER &&
                       window_contains(&explorer_window, mouse_x, mouse_y)) {
                int min_x = window_min_button_x(&explorer_window);
                int max_x = window_max_button_x(&explorer_window);
                int close_x = window_close_button_x(&explorer_window);
                int filter_x = explorer_window.x + 110;
                int filter_y = explorer_window.y + 6;

                if (explorer_rename_active) {
                    handled = 1;
                } else if (!recycle_bin && point_in_rect(mouse_x, mouse_y,
                                  filter_x, filter_y, 108, 11)) {
                    only_executables = !only_executables;
                    page = 0;
                    explorer_clear_selection();
                    screen_dirty = 1;
                    last_target = -99;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         close_x, explorer_window.y + WINDOW_BORDER, 14, 14)) {
                    close_explorer();
                    screen_dirty = 1;
                    last_target = -99;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         max_x, explorer_window.y + WINDOW_BORDER, 14, 14)) {
                    toggle_maximize_explorer();
                    clamp_page(&page);
                    screen_dirty = 1;
                    last_target = -99;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         min_x, explorer_window.y + WINDOW_BORDER, 14, 14)) {
                    minimize_explorer();
                    screen_dirty = 1;
                    last_target = -99;
                } else if (window_begin_resize(&explorer_window, mouse_x, mouse_y)) {
                    screen_dirty = 1;
                } else if (point_in_rect(mouse_x, mouse_y, explorer_window.x + 2,
                                         explorer_window.y + 2, explorer_window.w - 52, WINDOW_TITLE_H - 2)) {
                    window_begin_drag(&explorer_window, mouse_x, mouse_y);
                } else if (explorer_folder_pointer(mouse_x, mouse_y, &page)) {
                    screen_dirty = 1;
                    last_target = -99;
                    last_click_time = 0;
                } else {
                    explorer_folder_keyboard = 0;
                    explorer_folder_last_click[0] = 0;
                    int slot = point_to_slot(mouse_x, mouse_y);
                    int target = slot >= 0 ? item_for_slot(page, slot) : -1;
                    unsigned int modifiers = keyboard_modifiers();
                    int ctrl_down = (modifiers & KEYMOD_CTRL) != 0;
                    int shift_down = (modifiers & KEYMOD_SHIFT) != 0;
                    int plain_double =
                        !ctrl_down && !shift_down &&
                        target >= -2 && target != -1 &&
                        target == last_target &&
                        now - last_click_time <= CLOCKS_PER_SEC / 2;

                    if (plain_double) {
                        if (target == -2) {
                            char parent[MAX_PATH];

                            if (parent_path(parent, sizeof(parent)) &&
                                load_directory(parent)) {
                                page = 0;
                                screen_dirty = 1;
                            }
                        } else if (directory_items[target].type == TYPE_FOLDER) {
                            if (load_directory(directory_items[target].path)) {
                                page = 0;
                                screen_dirty = 1;
                            }
                        } else if (recycle_bin) {
                            if (bwa_open_reader_file(directory_items[target].path)) screen_dirty = 1;
                        } else if (executable_file(directory_items[target].name)) {
                            if (request_launch(&directory_items[target], page)) screen_dirty = 1;
                        } else if (open_associated_file(directory_items[target].path)) {
                            screen_dirty = 1;
                        }

                        last_target = -99;
                        last_click_time = 0;
                    } else if (target >= 0) {
                        explorer_select_item(target, modifiers);
                        if (!ctrl_down && !shift_down) explorer_shortcut_drag_begin(target, mouse_x, mouse_y);

                        screen_dirty = 1;
                        last_target =
                            (!ctrl_down && !shift_down) ? target : -99;
                        last_click_time = now;
                    } else {
                        if (target == -1 && point_in_rect(mouse_x, mouse_y,
                            explorer_client_x(), explorer_client_y(),
                            explorer_client_w(), explorer_client_h() - 12)) {
                            budo_selection_drag_begin(&explorer_select, mouse_x,
                                                      mouse_y, modifiers);
                        }
                        if (!explorer_select.dragging && !ctrl_down && !shift_down) {
                            explorer_clear_selection();
                            screen_dirty = 1;
                        }
                        if (target == -2) { explorer_up_selected = 1; screen_dirty = 1; }
                        last_target = target == -2 ? -2 : -99;
                        last_click_time = now;
                    }
                }
                handled = 1;
            }

            if (!handled && active_window >= APP_BWA_BASE) {
                BwaLoadedApp *app =
                    bwa_find_external_app(active_window);

                if (app != NULL && app->open) {
                    if (app->managed_window &&
                        !app->window.minimized &&
                        window_contains(&app->window,
                                        mouse_x, mouse_y)) {
                        int frame_action =
                            window_frame_mouse_down(&app->window,
                                                    mouse_x, mouse_y);
                        handled = 1;

                        if (frame_action == WINDOW_FRAME_CLOSE &&
                            (app->window_buttons &
                             BUDO_WINDOW_BUTTON_CLOSE)) {
                            (void)bwa_close_active_app();
                        } else if (frame_action ==
                                   WINDOW_FRAME_MINIMIZE &&
                                   (app->window_buttons &
                                    BUDO_WINDOW_BUTTON_MINIMIZE)) {
                            window_minimize_state(&app->window);
                            desktop_focus_visible();
                        } else if (frame_action ==
                                   WINDOW_FRAME_MAXIMIZE &&
                                   (app->window_buttons &
                                    BUDO_WINDOW_BUTTON_MAXIMIZE)) {
                            window_toggle_maximize_state(&app->window);
                        } else if (frame_action ==
                                   WINDOW_FRAME_CLIENT &&
                                   app->definition.callbacks.mouse_down != NULL) {
                            bwa_callback_app = app;
                            (void)app->definition.callbacks.mouse_down(
                                mouse_x, mouse_y, buttons);
                            bwa_callback_app = NULL;
                        }
                        screen_dirty = 1;
                    } else if (!app->managed_window &&
                               app->definition.callbacks.mouse_down != NULL) {
                        bwa_callback_app = app;
                        handled = app->definition.callbacks.mouse_down(
                            mouse_x, mouse_y, buttons);
                        bwa_callback_app = NULL;
                        if (handled) screen_dirty = 1;
                    }
                }
            }

            if (!handled) {
                int i;

                for (i = bwa_instance_count - 1; i >= 0; --i) {
                    BwaLoadedApp *app = &bwa_instances[i];

                    if (app->open && app->managed_window &&
                        !app->window.minimized &&
                        app->definition.runtime_id != active_window &&
                        window_contains(&app->window,
                                        mouse_x, mouse_y)) {
                        active_window = app->definition.runtime_id;
                        screen_dirty = 1;
                        handled = 1;
                        break;
                    }
                }
            }

            if (!handled &&
                terminal_window.open && !terminal_window.minimized &&
                window_contains(&terminal_window, mouse_x, mouse_y)) {
                active_window = APP_TERMINAL;
                screen_dirty = 1;
                handled = 1;
            }

            if (!handled &&
                editor_window.open && !editor_window.minimized &&
                window_contains(&editor_window, mouse_x, mouse_y)) {
                active_window = APP_EDITOR;
                screen_dirty = 1;
                handled = 1;
            }

            if (!handled &&
                explorer_window.open && !explorer_window.minimized &&
                window_contains(&explorer_window, mouse_x, mouse_y)) {
                active_window = APP_EXPLORER;
                screen_dirty = 1;
                handled = 1;
            }

            if (!handled && desktop_point_owner(mouse_x, mouse_y) == APP_NONE) {
                (void)desktop_selection_pointer(mouse_x, mouse_y, 1, &page);
                screen_dirty = 1;
            }
        }

        if (explorer_shortcut_drag_update(mouse_x, mouse_y, buttons & 1)) {
            last_target = -99;
            last_click_time = 0;
            screen_dirty = 1;
        }

        if ((buttons & 1) &&
            desktop_icon_pressed &&
            desktop_drag_app != APP_NONE) {
            int dx = mouse_x - desktop_press_x;
            int dy = mouse_y - desktop_press_y;

            if (!desktop_icon_dragging &&
                (dx > 3 || dx < -3 || dy > 3 || dy < -3)) {
                desktop_icon_dragging = 1;
                desktop_last_click = -1;
                last_target = -99;
                last_click_time = 0;
            }

            if (desktop_icon_dragging) {
                int new_x = mouse_x - desktop_drag_dx;
                int new_y = mouse_y - desktop_drag_dy;

                if (new_x < 22) new_x = 22;
                if (new_y < 24) new_y = 24;
                if (new_x > SCREEN_WIDTH - 50) new_x = SCREEN_WIDTH - 50;
                if (new_y > SCREEN_HEIGHT - 38) new_y = SCREEN_HEIGHT - 38;

                for (int id = 0; id < DESKTOP_SELECTION_MAX; ++id) {
                    if (desktop_drag_slots[id] < 0) continue;
                    int sx, sy;
                    desktop_slot_position(desktop_drag_slots[id], &sx, &sy);
                    int offset_x = new_x - desktop_drag_origin_x;
                    int offset_y = new_y - desktop_drag_origin_y;
                    if (sx + offset_x < 22) new_x += 22 - sx - offset_x;
                    if (sy + offset_y < 24) new_y += 24 - sy - offset_y;
                    if (sx + offset_x > SCREEN_WIDTH - 50) new_x -= sx + offset_x - SCREEN_WIDTH + 50;
                    if (sy + offset_y > SCREEN_HEIGHT - 38) new_y -= sy + offset_y - SCREEN_HEIGHT + 38;
                }
                if (new_x != desktop_drag_x || new_y != desktop_drag_y) {
                    desktop_drag_x = new_x;
                    desktop_drag_y = new_y;
                    desktop_drag_offset_x = new_x - desktop_drag_origin_x;
                    desktop_drag_offset_y = new_y - desktop_drag_origin_y;
                    screen_dirty = 1;
                }
            }
        }

        if ((buttons & 2) && !(previous_buttons & 2) && !confirm_kind &&
            editor_file_dialog == EDITOR_FILE_DIALOG_NONE) {
            if (desktop_selection_pointer(mouse_x, mouse_y, 2, &page)) {
                last_target = -99;
                last_click_time = 0;
                screen_dirty = 1;
            }
        }
        if (editor_selecting) {
            if (buttons & 1) {
                int ty = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H +
                    ((editor_writer_mode && editor_writer_ruler) ? EDITOR_RULER_H : 0) + 5;
                int bottom = ty + editor_visible_rows() * 9 - 1;
                if (mouse_y < ty || mouse_y > bottom) {
                    desktop_scroll_configure(APP_EDITOR, page);
                    int top = editor_vscroll.position + (mouse_y < ty ? -1 : 1);
                    if (top > budo_scroll_limit(&editor_vscroll)) top = budo_scroll_limit(&editor_vscroll);
                    editor_set_top_visual_row(top);
                }
                int y = mouse_y < ty ? ty : mouse_y > bottom ? bottom : mouse_y;
                int x = mouse_x < editor_text_x() ? editor_text_x() : mouse_x;
                editor_place_cursor_from_point(x, y);
                screen_dirty = 1;
            } else editor_selecting = 0;
        }
        if (explorer_select.dragging && (buttons & 1)) {
            explorer_rubber_update(mouse_x, mouse_y, page);
            screen_dirty = 1;
        }
        if (!(buttons & 1) && explorer_select.dragging) {
            explorer_rubber_update(mouse_x, mouse_y, page);
            explorer_select.dragging = 0;
            screen_dirty = 1;
        }

        if (desktop_select.dragging) {
            desktop_selection_drag_update(mouse_x, mouse_y);
            if (!(buttons & 1)) desktop_select.dragging = 0;
            screen_dirty = 1;
        }

        if ((buttons & 2) && !(previous_buttons & 2) &&
            active_window >= APP_BWA_BASE) {
            BwaLoadedApp *app =
                bwa_find_external_app(active_window);

            if (app != NULL && app->open &&
                (!app->managed_window || !app->window.minimized) &&
                app->definition.callbacks.mouse_down != NULL) {
                int right_handled;

                bwa_callback_app = app;
                right_handled =
                    app->definition.callbacks.mouse_down(
                        mouse_x, mouse_y, buttons);
                bwa_callback_app = NULL;
                if (right_handled) {
                    screen_dirty = 1;
                }
            }
        }

        if (!(buttons & 2) && (previous_buttons & 2) &&
            active_window >= APP_BWA_BASE) {
            BwaLoadedApp *app =
                bwa_find_external_app(active_window);

            if (app != NULL && app->open &&
                (!app->managed_window || !app->window.minimized) &&
                app->definition.callbacks.mouse_up != NULL) {
                int right_up_handled;

                bwa_callback_app = app;
                right_up_handled =
                    app->definition.callbacks.mouse_up(
                        mouse_x, mouse_y, buttons);
                bwa_callback_app = NULL;
                if (right_up_handled) {
                    screen_dirty = 1;
                }
            }
        }

        if (!confirm_kind && editor_file_dialog == EDITOR_FILE_DIALOG_NONE &&
            active_window >= APP_BWA_BASE &&
            ((buttons & 3) || mouse_x != previous_mouse_x ||
             mouse_y != previous_mouse_y) && !ui_captured) {
            BwaLoadedApp *app =
                bwa_find_external_app(active_window);

            if (app != NULL && app->open &&
                (!app->managed_window || !app->window.minimized) &&
                app->definition.callbacks.mouse_move != NULL) {
                int move_handled;

                bwa_callback_app = app;
                move_handled =
                    app->definition.callbacks.mouse_move(
                        mouse_x, mouse_y, buttons);
                bwa_callback_app = NULL;
                if (move_handled) {
                    screen_dirty = 1;
                }
            }
        }

        if ((buttons & 1) && !ui_captured &&
            desktop_scroll_pointer(mouse_x, mouse_y, BUDO_POINTER_MOVE, &page))
            screen_dirty = 1;
        if (!(buttons & 1) && (previous_buttons & 1))
            (void)desktop_scroll_pointer(mouse_x, mouse_y, BUDO_POINTER_UP, &page);

        if ((buttons & 1) && editor_ruler_drag != 0) {
            int ruler_x = editor_text_x();
            int total_cols = EDITOR_WRITER_PAGE_COLS;
            int col = (mouse_x - ruler_x + 3) / 6;

            if (col < 0) col = 0;
            if (col > total_cols) col = total_cols;

            if (editor_ruler_drag == 1) {
                int max_left =
                    total_cols - editor_writer_right_indent - 8;
                if (max_left < 0) max_left = 0;
                if (col > max_left) col = max_left;
                editor_writer_left_indent = col;
                if (editor_writer_first_indent >
                    total_cols - editor_writer_left_indent -
                    editor_writer_right_indent - 8) {
                    editor_writer_first_indent = 0;
                }
            } else if (editor_ruler_drag == 2) {
                int first = col - editor_writer_left_indent;
                int max_first =
                    total_cols - editor_writer_left_indent -
                    editor_writer_right_indent - 8;
                if (first < 0) first = 0;
                if (first > max_first) first = max_first;
                editor_writer_first_indent = first;
            } else {
                int right = EDITOR_WRITER_PAGE_COLS - col;
                int max_right =
                    EDITOR_WRITER_PAGE_COLS -
                    editor_writer_left_indent - 8;
                if (right < 0) right = 0;
                if (right > max_right) right = max_right;
                editor_writer_right_indent = right;
            }

            editor_ensure_cursor_visible();
            screen_dirty = 1;
        }

        if (buttons & 1) {
            if (window_update_pointer(&terminal_window,
                                      mouse_x, mouse_y,
                                      WINDOW_MIN_W, 180)) {
                screen_dirty = 1;
            }

            if (window_update_pointer(&explorer_window,
                                      mouse_x, mouse_y,
                                      WINDOW_MIN_W, WINDOW_MIN_H)) {
                clamp_page(&page);
                screen_dirty = 1;
            }

            if (window_update_pointer(&editor_window,
                                      mouse_x, mouse_y,
                                      editor_writer_mode ?
                                      EDITOR_WRITER_MIN_W : WINDOW_MIN_W,
                                      WINDOW_MIN_H)) {
                editor_ensure_cursor_visible();
                screen_dirty = 1;
            }

            if (active_window >= APP_BWA_BASE) {
                BwaLoadedApp *app =
                    bwa_find_external_app(active_window);

                if (app != NULL && app->managed_window &&
                    !app->window.minimized &&
                    window_update_pointer(&app->window,
                                          mouse_x, mouse_y,
                                          app->window_min_w,
                                          app->window_min_h)) {
                    screen_dirty = 1;
                }
            }
        }

        if (!(buttons & 1) && (previous_buttons & 1)) {
            if (explorer_drag_shortcut >= 0) {
                (void)explorer_shortcut_drag_drop(mouse_x, mouse_y);
                screen_dirty = 1;
            }
            if (active_window >= APP_BWA_BASE) {
                BwaLoadedApp *app =
                    bwa_find_external_app(active_window);

                if (app != NULL && app->open &&
                    (!app->managed_window || !app->window.minimized) &&
                    app->definition.callbacks.mouse_up != NULL) {
                    int up_handled;

                    bwa_callback_app = app;
                    up_handled =
                        app->definition.callbacks.mouse_up(
                            mouse_x, mouse_y, buttons);
                    bwa_callback_app = NULL;
                    if (up_handled) {
                        screen_dirty = 1;
                    }
                }
            }

            if (!desktop_icon_dragging && desktop_icon_pressed) desktop_selection_release();
            if (desktop_icon_dragging) {
                desktop_group_drop();

                desktop_icon_dragging = 0;
                (void)save_desktop_layout();
                screen_dirty = 1;
            }

            desktop_icon_pressed = 0;
            desktop_icon_dragging = 0;
            desktop_drag_app = APP_NONE;
            window_end_pointer(&explorer_window);
            window_end_pointer(&editor_window);
            window_end_pointer(&terminal_window);
            if (active_window >= APP_BWA_BASE) {
                BwaLoadedApp *app =
                    bwa_find_external_app(active_window);
                if (app != NULL && app->managed_window) {
                    window_end_pointer(&app->window);
                }
            }
            if (editor_ruler_drag != 0) {
                editor_ruler_drag = 0;
                (void)save_editor_settings();
            }
        }

        if (kbhit()) {
            int key = getch();

            if (text_input_active()) {
                text_caret_visible = 1;
                text_caret_last_toggle = bw_clock();
                caret_dirty = 1;
            }

            if (confirm_kind) {
                if (key == 27) desktop_confirm_result(BUDO_RESPONSE_CANCEL);
                else if (key == 13 || key == 's' || key == 'S' || key == 'y' || key == 'Y')
                    desktop_confirm_result(BUDO_RESPONSE_SAVE);
                else if (confirm_kind == BUDO_CONFIRM_SAVE && (key == 'd' || key == 'D'))
                    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
                clamp_page(&page);
                screen_dirty = 1;
            } else if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
                editor_file_dialog_key(key);
                screen_dirty = 1;
            } else if (desktop_switch_key(key, bw_key_modifiers())) {
                screen_dirty = 1;
            } else if (key == 27 && context_menu) {
                context_menu = 0;
                screen_dirty = 1;
            } else if (key == 27 && (explorer_file_menu || explorer_view_menu || terminal_file_menu)) {
                explorer_file_menu = explorer_view_menu = terminal_file_menu = 0;
                screen_dirty = 1;
            } else if (active_window == APP_TERMINAL &&
                terminal_window.open &&
                !terminal_window.minimized) {
                if (terminal_handle_key(key)) {
                    screen_dirty = 1;
                }

            } else if (active_window == APP_EDITOR &&
                editor_window.open &&
                !editor_window.minimized &&
                editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
                editor_file_dialog_key(key);
                screen_dirty = 1;
            } else if (active_window == APP_EDITOR &&
                editor_window.open &&
                !editor_window.minimized &&
                editor_dialog != EDITOR_DIALOG_NONE) {
                if (editor_search_dialog_active()) {
                    editor_search_key(key);
                    screen_dirty = 1;
                } else if (key == 27) {
                    editor_dialog = EDITOR_DIALOG_NONE;
                    editor_dialog_input[0] = '\0';
                    editor_dialog_len = 0;
                    screen_dirty = 1;
                } else if (key == 8) {
                    if (editor_dialog_len > 0) {
                        editor_dialog_input[--editor_dialog_len] = '\0';
                        screen_dirty = 1;
                    }
                } else if (key == 13) {
                    if (editor_dialog == EDITOR_DIALOG_OPEN) {
                        (void)editor_load_file(editor_dialog_input);
                        editor_dialog = EDITOR_DIALOG_NONE;
                    } else if (editor_dialog == EDITOR_DIALOG_SAVE_AS) {
                        (void)editor_save_file(editor_dialog_input);
                        editor_dialog = EDITOR_DIALOG_NONE;
                    }
                    screen_dirty = 1;
                } else if (key >= 32 && key != 127 &&
                           editor_dialog_len <
                           (int)sizeof(editor_dialog_input) - 1) {
                    editor_dialog_input[editor_dialog_len++] = (char)key;
                    editor_dialog_input[editor_dialog_len] = '\0';
                    screen_dirty = 1;
                }
            } else if (key == 27 && active_window == APP_EXPLORER) {
                if (explorer_rename_active) explorer_cancel_rename();
                else explorer_clear_selection();
                screen_dirty = 1;
            } else if (active_window == APP_NONE && desktop_selection_key(key, &page)) {
                if (shortcut_launch_requested) { shortcut_launch_requested = 0; screen_dirty = 1; }
                screen_dirty = 1;
            } else if (key == 27 && active_window < APP_BWA_BASE) {
                if (editor_menu != EDITOR_MENU_NONE) {
                    editor_menu = EDITOR_MENU_NONE;
                    screen_dirty = 1;
                } else {
                    desktop_exit_requested = 1;
                    if (desktop_request_exit()) break;
                    screen_dirty = 1;
                }
            } else if (active_window >= APP_BWA_BASE) {
                BwaLoadedApp *app =
                    bwa_find_external_app(active_window);

                if (app != NULL && app->open &&
                    (!app->managed_window || !app->window.minimized) &&
                    app->definition.callbacks.key != NULL) {
                    int key_handled;
                    bwa_callback_app = app;
                    if (key == 0) key = 0x100 | getch();
                    key_handled = app->definition.callbacks.key(key);
                    bwa_callback_app = NULL;
                    if (key_handled) {
                        screen_dirty = 1;
                    }
                }
            } else if (active_window == APP_EDITOR &&
                editor_window.open &&
                !editor_window.minimized) {
                if (key == 0) {
                    int extended = getch();
                    if (extended == 201 || extended == 202) {
                        desktop_scroll_configure(APP_EDITOR, page);
                        int top = editor_vscroll.position + (extended == 201 ? -3 : 3);
                        int limit = budo_scroll_limit(&editor_vscroll);
                        if (top > limit) top = limit;
                        editor_set_top_visual_row(top);
                    } else if (extended == 83) {
                        editor_delete_forward();
                    } else if (extended == 61) {
                        editor_search_action((bw_key_modifiers() & 3u) ? 1 : 0);
                    } else {
                        (void)editor_navigation(extended, bw_key_modifiers());
                    }

                    if (extended != 61) {
                        editor_match_line = -1;
                        editor_typing_line = -1;
                    }

                    screen_dirty = 1;
                } else if (key == 1) {
                    editor_anchor_line = editor_anchor_col = 0;
                    editor_cursor_line = editor_line_count - 1;
                    editor_cursor_col = (int)strlen(editor_lines[editor_cursor_line]);
                    editor_selection_active = 1;
                    editor_ensure_cursor_visible();
                    screen_dirty = 1;
                } else if (key == 3 || key == 24) {
                    editor_selection_copy(key == 24);
                    screen_dirty = 1;
                } else if (key == 22) {
                    editor_paste();
                    screen_dirty = 1;
                } else if (key == 14 || key == 15) {
                    editor_request_action(key == 14 ? 1 : 2, NULL);
                    screen_dirty = 1;
                } else if (key == 19) {
                    editor_save_current();
                    screen_dirty = 1;
                } else if (key == 26 || key == 25) {
                    editor_history_restore(key == 25);
                    screen_dirty = 1;
                } else if (key == 6) {
                    editor_begin_dialog(EDITOR_DIALOG_FIND, "");
                    screen_dirty = 1;
                } else if (key == 18 || (key == 8 && (bw_key_modifiers() & 4u))) {
                    editor_begin_dialog(EDITOR_DIALOG_REPLACE_FIND, "");
                    screen_dirty = 1;
                } else if (key == 8) {
                    editor_match_line = -1;
                    editor_preferred_visual_col = -1;
                    editor_backspace();
                    screen_dirty = 1;
                } else if (key == 13) {
                    editor_preferred_visual_col = -1;
                    editor_newline();
                    screen_dirty = 1;
                } else if (key == 9) {
                    int i;
                    int spaces = 4;

                    editor_preferred_visual_col = -1;
                    if (editor_writer_mode) {
                        int visual_col = editor_cursor_screen_col();
                        int next_tab = editor_writer_next_tab(visual_col);
                        spaces = next_tab - visual_col;
                        if (spaces < 1) spaces = 1;
                    }

                    for (i = 0; i < spaces; ++i)
                        editor_insert_char(' ');
                    screen_dirty = 1;
                } else if (key >= 32 && key != 127) {
                    editor_preferred_visual_col = -1;
                    editor_insert_char((char)key);
                    screen_dirty = 1;
                }
            } else if (active_window == APP_EXPLORER &&
                       explorer_window.open &&
                       !explorer_window.minimized) {
                if (explorer_rename_active) {
                    if (key == 27) {
                        explorer_cancel_rename();
                        screen_dirty = 1;
                    } else if (key == 13) {
                        if (explorer_commit_rename()) {
                            clamp_page(&page);
                        }
                        screen_dirty = 1;
                    } else if (key == 8) {
                        if (explorer_rename_len > 0) {
                            explorer_rename_input[--explorer_rename_len] =
                                '\0';
                            screen_dirty = 1;
                        }
                    } else if (key >= 32 && key != 127 &&
                               explorer_rename_len < MAX_NAME - 1) {
                        explorer_rename_input[explorer_rename_len++] =
                            (char)key;
                        explorer_rename_input[explorer_rename_len] = '\0';
                        screen_dirty = 1;
                    }
                } else if (key != 0 && explorer_folder_key(key, 0, &page)) {
                    screen_dirty = 1;
                    last_target = -99;
                } else if (key == ' ') {
                    int order[MAX_ITEMS];
                    int count = explorer_order(order);
                    budo_selection_space(&explorer_select, order, count, keyboard_modifiers());
                    screen_dirty = 1;
                } else if (key == 13 && explorer_up_selected) {
                    char parent[MAX_PATH];
                    if (parent_path(parent, sizeof(parent)) && load_directory(parent)) page = 0;
                    screen_dirty = 1;
                } else if (key == 13 && explorer_selected_item >= 0) {
                    DesktopItem *entry = &directory_items[explorer_selected_item];
                    if (entry->type == TYPE_FOLDER) {
                        if (load_directory(entry->path)) page = 0;
                    } else if (executable_path(entry->path)) {
                        if (request_launch(entry, page)) screen_dirty = 1;
                    } else if (recycle_bin) (void)bwa_open_reader_file(entry->path);
                    else (void)open_associated_file(entry->path);
                    screen_dirty = 1;
                } else if (key == 1) {
                    explorer_select_all_visible();
                    last_target = -99;
                    screen_dirty = 1;
                } else if (key == 3) {
                    explorer_stage_clipboard(EXPLORER_CLIP_COPY);
                    last_target = -99;
                } else if (key == 5) {
                    if (explorer_edit_selection()) {
                        screen_dirty = 1;
                    }
                    last_target = -99;
                } else if (key == 18) {
                    if (explorer_begin_rename()) {
                        screen_dirty = 1;
                    }
                    last_target = -99;
                } else if (key == 24) {
                    explorer_stage_clipboard(EXPLORER_CLIP_CUT);
                    last_target = -99;
                } else if (key == 22) {
                    if (explorer_paste_clipboard()) {
                        clamp_page(&page);
                        screen_dirty = 1;
                    }
                    last_target = -99;
                } else if (key == 0) {
                    int extended = getch();

                    if (extended == 201 || extended == 202) {
                        if (point_in_rect(mouse_x, mouse_y, explorer_window.x + 8,
                                          explorer_folder_y(), explorer_folder_width(), explorer_folder_rows() * 12)) {
                            explorer_folder_top += extended == 201 ? -3 : 3;
                            desktop_scroll_configure(APP_EXPLORER, page);
                        } else page += extended == 201 ? -1 : 1;
                        clamp_page(&page);
                        screen_dirty = 1;
                    } else if (explorer_folder_key(0, extended, &page)) {
                        screen_dirty = 1;
                        last_target = -99;
                    } else if (extended == 60) {
                        (void)explorer_begin_rename();
                        screen_dirty = 1;
                    } else if (extended == 83) {
                        (void)desktop_confirm(APP_EXPLORER, 3, "Delete selected files?",
                                              "Move the selected entries to Recycle Bin?");
                        screen_dirty = 1;
                        last_target = -99;
                    } else if (extended == 75 || extended == 77 ||
                               extended == 72 || extended == 80 ||
                               extended == 71 || extended == 79 ||
                               extended == 73 || extended == 81) {
                        explorer_navigate(extended, &page);
                        last_target = -99;
                        screen_dirty = 1;
                    }
                }
            }
        }

        if (text_input_active()) {
            clock_t caret_now = bw_clock();

            if (text_caret_last_toggle == 0) {
                text_caret_last_toggle = caret_now;
            } else if (caret_now - text_caret_last_toggle >=
                       CLOCKS_PER_SEC / 2) {
                text_caret_visible = !text_caret_visible;
                text_caret_last_toggle = caret_now;
                caret_dirty = 1;
            }
        } else {
            if (caret_drawn) caret_dirty = 1;
            text_caret_visible = 1;
            text_caret_last_toggle = 0;
        }

        if ((editor_menu != EDITOR_MENU_NONE || explorer_file_menu || explorer_view_menu || terminal_file_menu || picker_filter_menu || context_menu) &&
            (mouse_x != previous_mouse_x || mouse_y != previous_mouse_y))
            screen_dirty = 1;
        if (caret_dirty && (editor_search_dialog_active() ||
                            editor_file_dialog != EDITOR_FILE_DIALOG_NONE)) screen_dirty = 1;
        if (screen_dirty) {
            clamp_page(&page);
            draw_desktop(page);

            if (!present_framebuffer()) {
                exit_status = 1;
                goto application_exit;
            }

            caret_drawn = 0;
            if (text_input_active() && text_caret_visible) {
                int caret_x;
                int caret_y;

                if (text_caret_rect(&caret_x, &caret_y)) {
                    draw_text_caret_vga();
                    caret_drawn = 1;
                }
            }

            mouse_get_state(&mouse_x, &mouse_y, &buttons);
            draw_cursor_vga(mouse_x, mouse_y);
            previous_mouse_x = mouse_x;
            previous_mouse_y = mouse_y;
            cursor_drawn = 1;
            caret_dirty = 0;
            screen_dirty = 0;
        } else if (caret_dirty) {
            if (cursor_drawn) {
                restore_cursor_area(previous_mouse_x, previous_mouse_y);
                cursor_drawn = 0;
            }

            if (caret_drawn) {
                restore_text_caret_vga();
                caret_drawn = 0;
            }

            if (text_input_active() && text_caret_visible) {
                draw_text_caret_vga();
                caret_drawn = 1;
            }

            draw_cursor_vga(mouse_x, mouse_y);
            previous_mouse_x = mouse_x;
            previous_mouse_y = mouse_y;
            cursor_drawn = 1;
            caret_dirty = 0;
        } else if (!cursor_drawn ||
                   mouse_x != previous_mouse_x ||
                   mouse_y != previous_mouse_y) {
            if (cursor_drawn) {
                restore_cursor_area(previous_mouse_x, previous_mouse_y);
            }

            if (caret_drawn) {
                draw_text_caret_vga();
            }

            draw_cursor_vga(mouse_x, mouse_y);
            previous_mouse_x = mouse_x;
            previous_mouse_y = mouse_y;
            cursor_drawn = 1;
        }

        desktop_focus_visible();
        builtin_select(active_window);
        previous_buttons = buttons;
        if (desktop_exit_requested && desktop_request_exit()) break;
        if (!bw_end_frame()) break;
    }

application_exit:
    for (int i = 0; i < bwa_instance_count; ++i) {
        BwaLoadedApp *app = &bwa_instances[i];
        bwa_callback_app = app;
        if (app->open && app->definition.callbacks.close) app->definition.callbacks.close();
        if (app->handle) dlclose(app->handle);
    }
    bwa_callback_app = NULL;
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (terminal_instances[slot]) {
            builtin_select(3 + slot * 3);
            session_stop();
            if (slot) free(terminal_instances[slot]);
        }
        if (editor_instances[slot]) {
            editor_state = editor_instances[slot];
            editor_history_reset();
            if (slot) free(editor_instances[slot]);
        }
        if (slot) free(explorer_instances[slot]);
    }
    for (int i = 0; i < bwa_external_app_count; ++i)
        if (bwa_external_apps[i].handle) dlclose(bwa_external_apps[i].handle);
    free(picker_selection.selected);
    picker_selection.selected = NULL;
    free(editor_file_items);
    editor_file_items = NULL;
    free(editor_clipboard);
    set_text_mode();
    bw_finish();
    return exit_status;
}

#undef page
