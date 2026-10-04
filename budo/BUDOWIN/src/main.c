#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "bwa.h"
#include <time.h>
#include <unistd.h>

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
#define DESKTOP_COLOR 0U
#define TEXT_COLOR 1U
#define FILE_DARK 2U
#define FOLDER_DARK 2U
#define FILE_COLOR 4U
#define FOLDER_COLOR 5U
#define BACK_COLOR 6U
#define CURSOR_COLOR 1U
#define MOUSE_CURSOR_OUTLINE_COLOR 1U
#define MOUSE_CURSOR_FILL_COLOR 4U

#define WINDOW_FACE_COLOR 4U
#define WINDOW_CHROME_COLOR 5U
#define WINDOW_HIGHLIGHT_COLOR 4U
#define WINDOW_SHADOW_COLOR 2U
#define WINDOW_FRAME_COLOR 2U
#define TITLE_COLOR 6U
#define TITLE_TEXT_COLOR 4U
#define DESKTOP_TEXT_COLOR 4U
#define TERMINAL_BG_COLOR 1U
#define TERMINAL_TEXT_COLOR 5U

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

#define TYPE_FOLDER 1
#define TYPE_FILE 2

#define APP_NONE 0
#define APP_EXPLORER 1
#define APP_EDITOR 2
#define APP_TERMINAL 3
#define APP_BWA_BASE 100
#define BWA_MAX_EXTERNAL 16

#define EDITOR_MAX_LINES 512
#define EDITOR_MAX_COLS 2048
#define EDITOR_MENU_H 14
#define EDITOR_STATUS_H 14
#define EDITOR_RULER_H 20
#define EDITOR_WRITER_TABS 8
#define EDITOR_WRITER_PAGE_COLS 77
#define EDITOR_WRITER_PAGE_W (EDITOR_WRITER_PAGE_COLS * 6)
#define EDITOR_WRITER_MIN_W (EDITOR_WRITER_PAGE_W + 16)
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
} AppWindow;

#define WINDOW_FRAME_NONE      0
#define WINDOW_FRAME_CLIENT    1
#define WINDOW_FRAME_MINIMIZE  2
#define WINDOW_FRAME_MAXIMIZE  3
#define WINDOW_FRAME_CLOSE     4
#define WINDOW_FRAME_RESIZE    5
#define WINDOW_FRAME_DRAG      6

typedef struct BudoMenuItem {
    const char *label;
    int enabled;
    int checked;
} BudoMenuItem;

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
static DesktopItem items[MAX_ITEMS];
static int item_count = 0;
static char current_path[MAX_PATH] = "/";
static char home_path[MAX_PATH] = "/";
static int resume_explorer = 0;
static int only_executables = 0;
static int explorer_selected_item = -1;
static unsigned char explorer_selection[MAX_ITEMS];
static ExplorerClipboardItem explorer_clipboard[MAX_ITEMS];
static int explorer_clipboard_count = 0;
static int explorer_clipboard_mode = EXPLORER_CLIP_NONE;
static int explorer_rename_active = 0;
static int explorer_rename_item = -1;
static char explorer_rename_input[MAX_NAME];
static int explorer_rename_len = 0;
static int explorer_desktop_slot = 0;
static int editor_desktop_slot = 1;
static int desktop_drag_app = APP_NONE;
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
static BwaLoadedApp *bwa_callback_app = NULL;

static char editor_lines[EDITOR_MAX_LINES][EDITOR_MAX_COLS];
static int editor_line_count = 1;
static int editor_cursor_line = 0;
static int editor_cursor_col = 0;
static int editor_top_line = 0;
static int editor_left_col = 0;
static int editor_menu = EDITOR_MENU_NONE;
static int editor_dialog = EDITOR_DIALOG_NONE;
static char editor_path[MAX_PATH] = "";
static char editor_dialog_input[MAX_PATH] = "";
static char editor_replace_find[MAX_PATH] = "";
static int editor_dialog_len = 0;
static char editor_status[64] = "";
static int editor_word_wrap = 0;
static int editor_show_row_numbers = 1;
static int editor_writer_mode = 0;
static int editor_writer_ruler = 1;
static int editor_writer_left_indent = 0;
static int editor_writer_first_indent = 0;
static int editor_writer_right_indent = 0;
static int editor_writer_tabs[EDITOR_WRITER_TABS] =
    {8, 16, 24, 32, 40, 48, 56, 64};
static int editor_ruler_drag = 0;
static char editor_last_save[24] = "Not saved";
static int text_caret_visible = 1;
static clock_t text_caret_last_toggle = 0;
static int editor_preferred_visual_col = -1;

static int editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
static DesktopItem editor_file_items[MAX_ITEMS];
static int editor_file_count = 0;
static int editor_file_top = 0;
static int editor_file_selected = -1;
static char editor_file_path[MAX_PATH] = "/";
static char editor_file_name[MAX_NAME] = "";
static int editor_file_name_len = 0;
static clock_t editor_file_last_click_time = 0;

static AppWindow explorer_window = {
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static AppWindow editor_window = {
    90, 78, 480, 330,
    90, 78, 480, 330,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static AppWindow terminal_window = {
    110, 90, 500, 300,
    110, 90, 500, 300,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char terminal_lines[TERMINAL_MAX_LINES][TERMINAL_LINE_LEN];
static int terminal_line_count = 0;
static char terminal_input[TERMINAL_INPUT_LEN] = "";
static int terminal_input_len = 0;
static int terminal_cursor = 0;
static char terminal_history[TERMINAL_HISTORY_MAX][TERMINAL_INPUT_LEN];
static int terminal_history_count = 0;
static int terminal_history_pos = -1;
static int terminal_scroll = 0;
static int terminal_paging = 0;
static int terminal_page_top = 0;
static int terminal_page_end = 0;
static char terminal_completion[TERMINAL_COMPLETION_MAX][MAX_NAME];
static int terminal_completion_count = 0;
static int terminal_completion_index = -1;
static int terminal_completion_start = 0;
static int terminal_completion_end = 0;
static char terminal_completion_prefix[MAX_PATH] = "";
static char terminal_cwd[MAX_PATH] = "";
static int terminal_native_launch_requested = 0;
static char terminal_native_command[TERMINAL_INPUT_LEN] = "";
static int terminal_native_is_batch = 0;

static int window_contains(const AppWindow *window, int x, int y);
static int point_in_rect(int px, int py, int x, int y, int w, int h);
static void draw_text(int x, int y, const char *text,
                      unsigned char color, int max_chars);
static int page_count(void);
static int item_for_slot(int page, int slot);
static void explorer_clear_selection(void);
static void explorer_select_all_visible(void);
static void explorer_select_range(int first, int last, int add);
static void explorer_stage_clipboard(int mode);
static int explorer_paste_clipboard(void);
static int explorer_make_copy_name(const char *name,
                                   char *dest,
                                   size_t dest_size);
static int explorer_delete_selection(void);
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
static int text_file(const char *name);
static int code_file(const char *name);
static int find_file_association(const char *extension);
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
static int editor_save_rtf(const char *path);
static int editor_load_rtf(const char *path);
static int editor_export_postscript(const char *path);
static int editor_writer_segment_indent(int segment);
static void open_explorer(void);
static void open_editor(void);
static void open_terminal(void);
static int terminal_visible_rows(void);
static int terminal_command_resolves_executable(const char *command,
                                                int *is_batch);
static int terminal_save_resume_state(void);
static void terminal_load_resume_state(void);
static int terminal_request_native_launch(const char *command,
                                          int is_batch,
                                          int page);
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

static void set_classic_gui_palette(void)
{
    int r;
    int g;
    int b;

    /* VGA DAC values are 0..63.  Entries 0..15 stay UI-owned. */
    set_palette_entry(0, 0, 32, 32);    /* teal desktop */
    set_palette_entry(1, 0, 0, 0);      /* black */
    set_palette_entry(2, 31, 31, 31);   /* dark grey shadow */
    set_palette_entry(3, 42, 42, 42);   /* medium grey */
    set_palette_entry(4, 63, 63, 63);   /* white highlight */
    set_palette_entry(5, 48, 48, 48);   /* classic light grey face */
    set_palette_entry(6, 0, 0, 40);     /* dark blue title bar */
    set_palette_entry(7, 0, 0, 63);     /* bright blue accent */
    set_palette_entry(8, 63, 63, 0);    /* yellow accent */
    set_palette_entry(9, 40, 0, 0);     /* dark red accent */
    set_palette_entry(10, 0, 40, 0);
    set_palette_entry(11, 0, 40, 40);
    set_palette_entry(12, 40, 0, 40);
    set_palette_entry(13, 40, 20, 0);
    set_palette_entry(14, 40, 40, 40);
    set_palette_entry(15, 63, 63, 63);

    /*
     * Entries 16..231 form a fixed 6x6x6 RGB cube for PCX assets.
     * This prevents an image palette from changing BUDOWIN's UI colors.
     */
    for (r = 0; r < 6; ++r) {
        for (g = 0; g < 6; ++g) {
            for (b = 0; b < 6; ++b) {
                unsigned char index =
                    (unsigned char)(16 + r * 36 + g * 6 + b);
                set_palette_entry(index,
                                  (unsigned char)((r * 63) / 5),
                                  (unsigned char)((g * 63) / 5),
                                  (unsigned char)((b * 63) / 5));
            }
        }
    }
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

static void draw_window_chrome(const AppWindow *window)
{
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
              TITLE_COLOR);
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
        window->y = 0;
        window->w = SCREEN_WIDTH;
        window->h = SCREEN_HEIGHT;
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
    if (!window->maximized &&
        point_in_rect(x, y,
                      window->x + window->w - 12,
                      window->y + window->h - 12,
                      12, 12)) {
        window->resizing = 1;
        window->resize_mouse_x = x;
        window->resize_mouse_y = y;
        window->resize_w = window->w;
        window->resize_h = window->h;
        return WINDOW_FRAME_RESIZE;
    }
    if (!window->maximized &&
        point_in_rect(x, y,
                      window->x + 2, window->y + 2,
                      window->w - 52, WINDOW_TITLE_H - 2)) {
        window->dragging = 1;
        window->drag_dx = x - window->x;
        window->drag_dy = y - window->y;
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
        int new_x = mouse_x - window->drag_dx;
        int new_y = mouse_y - window->drag_dy;

        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
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
        int new_w = window->resize_w +
                    mouse_x - window->resize_mouse_x;
        int new_h = window->resize_h +
                    mouse_y - window->resize_mouse_y;

        if (new_w < min_w) new_w = min_w;
        if (new_h < min_h) new_h = min_h;
        if (window->x + new_w > SCREEN_WIDTH)
            new_w = SCREEN_WIDTH - window->x;
        if (window->y + new_h > SCREEN_HEIGHT)
            new_h = SCREEN_HEIGHT - window->y;

        if (new_w != window->w || new_h != window->h) {
            window->w = new_w;
            window->h = new_h;
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
    if (active) {
        fill_rect(x, y, w, EDITOR_MENU_H, WINDOW_HIGHLIGHT_COLOR);
        draw_rect(x, y, w, EDITOR_MENU_H, WINDOW_SHADOW_COLOR);
    }
    draw_text(x + 4, y + 4, label, TEXT_COLOR, (w - 8) / 6);
}

static void draw_popup_menu(int x, int y, int w,
                            const BudoMenuItem *items, int count)
{
    int i;
    int h = count * 12 + 4;

    fill_rect(x, y, w, h, WINDOW_CHROME_COLOR);
    draw_rect(x, y, w, h, TEXT_COLOR);
    draw_bevel(x + 1, y + 1, w - 2, h - 2, 1);

    for (i = 0; i < count; ++i) {
        unsigned char color = items[i].enabled ? TEXT_COLOR :
                              WINDOW_SHADOW_COLOR;
        if (items[i].checked) {
            draw_text(x + 5, y + 4 + i * 12, "x", color, 1);
        }
        draw_text(x + 15, y + 4 + i * 12,
                  items[i].label, color, (w - 20) / 6);
    }
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

    for (i = 0; text[i] != '\0' && i < max_chars; ++i) {
        draw_char(x + i * 6, y, text[i], color);
    }
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

    for (row = 0; row < MOUSE_CURSOR_H; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + MOUSE_CURSOR_W;
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
                cursor_pcx_icon[row * MOUSE_CURSOR_W + image_x];

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

static int text_caret_rect(int *x, int *y)
{
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
        char prompt[MAX_PATH + TERMINAL_INPUT_LEN + 2];
        int text_x = terminal_window.x + 7;
        int cols = (terminal_window.w - 14) / 6;
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
        *y = terminal_window.y + terminal_window.h - 13;
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

    for (row = 0; row < 7; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + 5;
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
    unsigned char rowbuf[5];
    unsigned char color =
        active_window == APP_TERMINAL ?
        TERMINAL_TEXT_COLOR : CURSOR_COLOR;
    int x;
    int y;
    int row;
    int col;

    if (!text_caret_rect(&x, &y)) return;

    for (col = 0; col < 5; ++col) {
        rowbuf[col] = color;
    }

    for (row = 0; row < 7; ++row) {
        int sy = y + row;
        int start_x = x;
        int end_x = x + 5;
        int offset;
        int length;

        if (sy < 0 || sy >= SCREEN_HEIGHT) continue;
        if (start_x < 0) start_x = 0;
        if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;

        length = end_x - start_x;
        if (length <= 0) continue;

        offset = sy * SCREEN_WIDTH + start_x;
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

static int explorer_client_x(void)
{
    return explorer_window.x + 8;
}

static int explorer_client_y(void)
{
    return explorer_window.y + WINDOW_TITLE_H + 8;
}

static int explorer_client_w(void)
{
    return explorer_window.w - 16;
}

static int explorer_client_h(void)
{
    return explorer_window.h - WINDOW_TITLE_H - 20;
}

static int explorer_cols(void)
{
    int cols = explorer_client_w() / GRID_X_STEP;
    return cols > 0 ? cols : 1;
}

static int explorer_rows(void)
{
    int rows = (explorer_client_h() - 12) / GRID_Y_STEP;
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

    *x = explorer_client_x() + col * GRID_X_STEP + 20;
    *y = explorer_client_y() + row * GRID_Y_STEP;
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

    col = rel_x / GRID_X_STEP;
    row = rel_y / GRID_Y_STEP;

    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return -1;
    }

    slot = row * cols + col;
    slot_position(slot, &icon_x, &icon_y);

    if (x < icon_x - 20 || x >= icon_x + 52 ||
        y < icon_y - 2 || y >= icon_y + 40) {
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
    fill_rect(x + 5, y + 1, 14, 14, FILE_COLOR);
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

static void draw_labeled_icon(int x, int y, const char *name, int type,
                              int selected)
{
    int len = (int)strlen(name);
    int shown = len > 12 ? 12 : len;
    int is_exec = type == TYPE_FILE && executable_file(name);
    int is_code = type == TYPE_FILE && code_file(name);
    int is_text = type == TYPE_FILE && text_file(name);
    int pcx;
    const unsigned char *pixels;
    int label_width = shown * 6 - 1;
    int label_x;
    unsigned char label_color = selected ?
                                TITLE_TEXT_COLOR : TEXT_COLOR;

    if (selected) {
        fill_rect(x - 20, y - 2, 72, 42, TITLE_COLOR);
    }

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
        draw_text(label_x, y + DESKTOP_ICON_H + 2,
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

    draw_text(label_x, y + ICON_H + 2, name, label_color, shown);
}

static void draw_back_icon(int x, int y)
{
    int label_width = 4 * 6 - 1;
    int label_x;

    if (back_pcx_icon_loaded) {
        label_x = x + DESKTOP_ICON_W / 2 - label_width / 2;
        draw_pcx_icon(x, y, back_pcx_icon);
        draw_text(label_x, y + DESKTOP_ICON_H + 2,
                  "BACK", TEXT_COLOR, 4);
        return;
    }

    label_x = x + ICON_W / 2 - label_width / 2;
    draw_back(x, y);
    draw_text(label_x, y + ICON_H + 2, "BACK", TEXT_COLOR, 4);
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
              y + ICON_H + 3, "File", DESKTOP_TEXT_COLOR, 4);
    draw_text(center_x - explorer_width / 2,
              y + ICON_H + 12, "Explorer", DESKTOP_TEXT_COLOR, 8);
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
              y + ICON_H + 5, "Editor", DESKTOP_TEXT_COLOR, 6);
}

static void draw_window_button(int x, int y, int kind)
{
    fill_rect(x, y, 14, 14, WINDOW_CHROME_COLOR);
    draw_bevel(x, y, 14, 14, 1);

    if (kind == 0) {
        fill_rect(x + 3, y + 9, 8, 2, WINDOW_FRAME_COLOR);
    } else if (kind == 1) {
        draw_rect(x + 3, y + 3, 8, 8, WINDOW_FRAME_COLOR);
        fill_rect(x + 4, y + 4, 6, 1, WINDOW_FRAME_COLOR);
    } else {
        int i;
        for (i = 0; i < 8; ++i) {
            put_pixel(x + 3 + i, y + 3 + i, WINDOW_FRAME_COLOR);
            put_pixel(x + 10 - i, y + 3 + i, WINDOW_FRAME_COLOR);
        }
    }
}

static void draw_checkbox(int x, int y, int checked)
{
    fill_rect(x, y, 10, 10, FILE_COLOR);
    draw_bevel(x, y, 10, 10, 0);

    if (checked) {
        put_pixel(x + 2, y + 5, WINDOW_FRAME_COLOR);
        put_pixel(x + 3, y + 6, WINDOW_FRAME_COLOR);
        put_pixel(x + 4, y + 7, WINDOW_FRAME_COLOR);
        put_pixel(x + 5, y + 6, WINDOW_FRAME_COLOR);
        put_pixel(x + 6, y + 5, WINDOW_FRAME_COLOR);
        put_pixel(x + 7, y + 4, WINDOW_FRAME_COLOR);
        put_pixel(x + 8, y + 3, WINDOW_FRAME_COLOR);
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
              "File Explorer", TITLE_TEXT_COLOR, 13);

    draw_checkbox(filter_x, filter_y, only_executables);
    draw_text(filter_x + 14, title_y + 6,
              "Only Executables", TITLE_TEXT_COLOR, 16);

    draw_window_button(min_x, title_y + WINDOW_BORDER, 0);
    draw_window_button(max_x, title_y + WINDOW_BORDER, 1);
    draw_window_button(close_x, title_y + WINDOW_BORDER, 2);

    slots = explorer_slots();

    for (slot = 0; slot < slots; ++slot) {
        int item_index = item_for_slot(page, slot);
        int x;
        int y;

        slot_position(slot, &x, &y);

        if (item_index == -2) {
            draw_back_icon(x, y);
        } else if (item_index >= 0) {
            draw_labeled_icon(x, y,
                              items[item_index].name,
                              items[item_index].type,
                              explorer_selection[item_index] != 0);
        }
    }

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
                  shortcuts, TEXT_COLOR, max_chars);
    }

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
                  "Rename", TITLE_TEXT_COLOR, 6);
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
                  "Enter=Rename  Esc=Cancel",
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
    memset(editor_lines, 0, sizeof(editor_lines));
    editor_line_count = 1;
    editor_cursor_line = 0;
    editor_cursor_col = 0;
    editor_top_line = 0;
    editor_left_col = 0;
    editor_path[0] = '\0';
    strcpy(editor_last_save, "Not saved");
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
    char *line = editor_lines[editor_line_count - 1];
    int len = (int)strlen(line);

    if (len < EDITOR_MAX_COLS - 1) {
        line[len] = (char)ch;
        line[len + 1] = '\0';
    }
}

static void editor_rtf_new_paragraph(void)
{
    if (editor_line_count < EDITOR_MAX_LINES) {
        ++editor_line_count;
        editor_lines[editor_line_count - 1][0] = '\0';
    }
}

static int editor_load_rtf(const char *path)
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

    fclose(file);

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
    editor_left_col = 0;
    editor_preferred_visual_col = -1;
    editor_update_last_save(editor_path);
    editor_set_status("RTF opened");
    return 1;
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

static int editor_load_file(const char *path)
{
    FILE *file;

    if (editor_path_is_rtf(path)) {
        return editor_load_rtf(path);
    }

    file = fopen(path, "rt");
    char buffer[EDITOR_MAX_COLS * 2];

    if (file == NULL) {
        editor_set_status("Open failed");
        return 0;
    }

    memset(editor_lines, 0, sizeof(editor_lines));
    editor_line_count = 0;

    while (editor_line_count < EDITOR_MAX_LINES &&
           fgets(buffer, sizeof(buffer), file) != NULL) {
        size_t len = strlen(buffer);

        while (len > 0 &&
               (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[--len] = '\0';
        }

        if (len >= EDITOR_MAX_COLS) {
            len = EDITOR_MAX_COLS - 1;
        }

        memcpy(editor_lines[editor_line_count], buffer, len);
        editor_lines[editor_line_count][len] = '\0';
        ++editor_line_count;
    }

    fclose(file);

    if (editor_line_count == 0) {
        editor_line_count = 1;
    }

    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    editor_cursor_line = 0;
    editor_cursor_col = 0;
    editor_top_line = 0;
    editor_left_col = 0;
    editor_writer_mode = 0;
    editor_update_last_save(editor_path);
    editor_set_status("File opened");
    return 1;
}

static int editor_save_file(const char *path)
{
    FILE *file;
    int i;

    if (editor_writer_mode) {
        return editor_save_rtf(path);
    }

    file = fopen(path, "wt");

    if (file == NULL) {
        editor_set_status("Save failed");
        return 0;
    }

    for (i = 0; i < editor_line_count; ++i) {
        fputs(editor_lines[i], file);
        if (i + 1 < editor_line_count) {
            fputc('\n', file);
        }
    }

    fclose(file);
    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    editor_update_last_save(editor_path);
    editor_set_status("File saved");
    return 1;
}

static int editor_find_from(const char *needle, int start_line, int start_col)
{
    int line;

    if (needle[0] == '\0') {
        return 0;
    }

    for (line = start_line; line < editor_line_count; ++line) {
        const char *base = editor_lines[line];
        const char *found;
        int begin = line == start_line ? start_col : 0;
        int len = (int)strlen(base);

        if (begin > len) {
            begin = len;
        }

        found = strstr(base + begin, needle);
        if (found != NULL) {
            editor_cursor_line = line;
            editor_cursor_col = (int)(found - base);
            editor_ensure_cursor_visible();
            editor_set_status("Found");
            return 1;
        }
    }

    editor_set_status("Not found");
    return 0;
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
        strncmp(line + editor_cursor_col, find_text,
                (size_t)find_len) != 0) {
        return 0;
    }

    if (line_len - find_len + replace_len >= EDITOR_MAX_COLS) {
        editor_set_status("Replacement too long");
        return 0;
    }

    memmove(line + editor_cursor_col + replace_len,
            line + editor_cursor_col + find_len,
            (size_t)(line_len - editor_cursor_col - find_len + 1));
    memcpy(line + editor_cursor_col,
           replace_text, (size_t)replace_len);
    editor_cursor_col += replace_len;
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

    for (line = 0; line < editor_line_count; ++line) {
        char *text = editor_lines[line];
        int pos = 0;

        while (text[pos] != '\0') {
            char *found = strstr(text + pos, find_text);
            int line_len;

            if (found == NULL) {
                break;
            }

            line_len = (int)strlen(text);
            pos = (int)(found - text);

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
        editor_set_status("Replace all complete");
    } else {
        editor_set_status("No matches");
    }

    return count;
}

static void editor_begin_dialog(int dialog, const char *initial)
{
    size_t len = strlen(initial);

    if (len >= sizeof(editor_dialog_input)) {
        len = sizeof(editor_dialog_input) - 1;
    }

    editor_dialog = dialog;
    memcpy(editor_dialog_input, initial, len);
    editor_dialog_input[len] = '\0';
    editor_dialog_len = (int)len;
    editor_menu = EDITOR_MENU_NONE;
}

static void editor_delete_line(void)
{
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
         EDITOR_STATUS_H - ruler_h - 10) / 9;
    return rows > 0 ? rows : 1;
}

static int editor_text_x(void)
{
    if (editor_writer_mode) {
        return editor_window.x +
               (editor_window.w - EDITOR_WRITER_PAGE_W) / 2;
    }

    return editor_window.x + (editor_rows_enabled() ? 40 : 8);
}

static int editor_visible_cols(void)
{
    int right_padding = 8;
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

    if (start > 0) {
        while (start < len && isspace((unsigned char)text[start])) {
            ++start;
        }
    }

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
        int visible_end = break_at;

        while (visible_end > start &&
               isspace((unsigned char)text[visible_end - 1])) {
            --visible_end;
        }

        *draw_len = visible_end - start;
        i = break_at;
        while (i < len && isspace((unsigned char)text[i])) {
            ++i;
        }
        *next_start = i;
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

        if (editor_cursor_col <= draw_end) {
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

    for (line = editor_top_line; line < editor_cursor_line; ++line) {
        row += editor_wrapped_rows_for_line(line, cols);
    }

    editor_cursor_wrap_position(&local_row, &local_col);
    row += local_row;
    return row;
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
                    return 1;
                }

                ++row;
                ++segment;
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
        (void)editor_set_cursor_from_visual_row(top_abs_row + row, col);
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

static void editor_ensure_cursor_visible(void)
{
    int rows = editor_visible_rows();
    int cols = editor_visible_cols();

    if (editor_cursor_line < editor_top_line) {
        editor_top_line = editor_cursor_line;
    }

    if (editor_wrap_enabled()) {
        editor_left_col = 0;

        while (editor_top_line < editor_cursor_line &&
               editor_cursor_screen_row() >= rows) {
            ++editor_top_line;
        }
    } else {
        if (editor_cursor_line >= editor_top_line + rows) {
            editor_top_line = editor_cursor_line - rows + 1;
        }

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

static void editor_insert_char(char ch)
{
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

    memmove(line + editor_cursor_col + 1,
            line + editor_cursor_col,
            (size_t)(len - editor_cursor_col + 1));
    line[editor_cursor_col] = ch;
    ++editor_cursor_col;
    editor_ensure_cursor_visible();
}

static void editor_newline(void)
{
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

static void editor_backspace(void)
{
    char *line = editor_lines[editor_cursor_line];
    int len = (int)strlen(line);

    if (editor_cursor_col > 0) {
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
    if (editor_writer_mode) {
        char title[MAX_NAME + 16];
        const char *name = editor_path[0] != '\0' ?
                           strrchr(editor_path, '/') : NULL;
        int max_title_chars =
            (wrap_x - editor_window.x - 13) / 6;

        if (name != NULL) ++name;
        else if (editor_path[0] != '\0') name = editor_path;
        else name = "Untitled";

        if (max_title_chars < 5) max_title_chars = 5;
        snprintf(title, sizeof(title), "Write - [%.60s]", name);
        draw_text(editor_window.x + 7, editor_window.y + 6,
                  title, TITLE_TEXT_COLOR, max_title_chars);
    } else {
        draw_text(editor_window.x + 7, editor_window.y + 6,
                  "Editor", TITLE_TEXT_COLOR, 6);
    }

    draw_checkbox(wrap_x, editor_window.y + 6,
                  editor_wrap_enabled());
    draw_text(wrap_x + 14, editor_window.y + 6,
              "Wrap", TITLE_TEXT_COLOR, 4);
    draw_checkbox(rows_x, editor_window.y + 6,
                  editor_rows_enabled());
    draw_text(rows_x + 14, editor_window.y + 6,
              "Rows", TITLE_TEXT_COLOR, 4);
    draw_checkbox(writer_x, editor_window.y + 6,
                  editor_writer_mode);
    draw_text(writer_x + 14, editor_window.y + 6,
              "Writer", TITLE_TEXT_COLOR, 6);

    draw_window_button(min_x, editor_window.y + WINDOW_BORDER, 0);
    draw_window_button(max_x, editor_window.y + WINDOW_BORDER, 1);
    draw_window_button(close_x, editor_window.y + WINDOW_BORDER, 2);

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
    fill_rect(editor_window.x + 2,
              editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H,
              editor_window.w - 4, 1, FOLDER_DARK);

    if (editor_writer_mode && editor_writer_ruler) {
        int ruler_y = editor_window.y + WINDOW_TITLE_H +
                      EDITOR_MENU_H + 2;
        int ruler_x = text_x;
        int ruler_w = EDITOR_WRITER_PAGE_W;
        int tick;
        int i;
        int left_x = ruler_x + editor_writer_left_indent * 6;
        int first_x = ruler_x +
                      (editor_writer_left_indent +
                       editor_writer_first_indent) * 6;
        int right_x = ruler_x +
                      (EDITOR_WRITER_PAGE_COLS -
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
                              number, FOLDER_COLOR, 3);
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

                if (editor_rows_enabled() && segment == 0) {
                    char number[16];
                    snprintf(number, sizeof(number), "%3d", line + 1);
                    draw_text(editor_window.x + 6, y,
                              number, FOLDER_COLOR, 3);
                }

                if (draw_len > 0) {
                    int draw_x = text_x +
                                 (editor_writer_mode ?
                                  editor_writer_segment_indent(segment) * 6 :
                                  0);
                    draw_text(draw_x, y,
                              editor_lines[line] + draw_start,
                              TEXT_COLOR, draw_len);
                }

                ++screen_row;
                ++segment;

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
                          number, FOLDER_COLOR, 3);
            }

            if (editor_left_col < len) {
                draw_text(text_x, y,
                          editor_lines[line] + editor_left_col,
                          TEXT_COLOR, visible_cols);
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
                  EDITOR_STATUS_H - 2, FILE_DARK);
        snprintf(status_text, sizeof(status_text),
                 "Ln %d  Col %d  Words %d  Saved %s",
                 editor_cursor_line + 1,
                 editor_cursor_col + 1,
                 words,
                 editor_last_save);
        draw_text(editor_window.x + 6, status_y + 3,
                  status_text, TEXT_COLOR,
                  (editor_window.w - 16) / 6);
    }

    if (editor_menu != EDITOR_MENU_NONE) {
        int menu_x = editor_window.x + 5;
        int menu_y = editor_window.y + WINDOW_TITLE_H + EDITOR_MENU_H;
        BudoMenuItem file_items[6] = {
            {"New", 1, 0},
            {"Open...", 1, 0},
            {"Save", 1, 0},
            {"Save As...", 1, 0},
            {"Export...", 1, 0},
            {"Close", 1, 0}
        };
        BudoMenuItem edit_items[2] = {
            {"Delete Line", 1, 0},
            {"Clear All", 1, 0}
        };
        BudoMenuItem search_items[3] = {
            {"Find...", 1, 0},
            {"Replace...", 1, 0},
            {"Replace All...", 1, 0}
        };
        BudoMenuItem document_items[1] = {
            {editor_writer_ruler ? "Ruler" : "Ruler", 1,
             editor_writer_ruler}
        };

        if (editor_menu == EDITOR_MENU_FILE) {
            if (!editor_writer_mode) {
                file_items[4].label = "Close";
                draw_popup_menu(menu_x, menu_y, 112, file_items, 5);
            } else {
                draw_popup_menu(menu_x, menu_y, 112, file_items, 6);
            }
        } else if (editor_menu == EDITOR_MENU_EDIT) {
            draw_popup_menu(menu_x + 36, menu_y, 112,
                            edit_items, 2);
        } else if (editor_menu == EDITOR_MENU_SEARCH) {
            draw_popup_menu(menu_x + 72, menu_y, 112,
                            search_items, 3);
        } else if (editor_menu == EDITOR_MENU_DOCUMENT) {
            draw_popup_menu(menu_x + 108, menu_y, 112,
                            document_items, 1);
        }
    }

    if (editor_file_dialog != EDITOR_FILE_DIALOG_NONE) {
        editor_draw_file_dialog();
    }

    if (editor_dialog != EDITOR_DIALOG_NONE) {
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

        fill_rect(dialog_x, dialog_y, dialog_w, 64, DESKTOP_COLOR);
        draw_rect(dialog_x, dialog_y, dialog_w, 64, TEXT_COLOR);
        fill_rect(dialog_x + 1, dialog_y + 1,
                  dialog_w - 2, 14, FILE_DARK);
        draw_text(dialog_x + 5, dialog_y + 5,
                  label, TEXT_COLOR, 20);
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

static int terminal_extract_command_name(const char *command,
                                         char *name,
                                         size_t name_size)
{
    const char *p = command;
    size_t len = 0;
    int quoted = 0;

    while (isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p == '"') {
        quoted = 1;
        ++p;
    }

    while (*p != '\0') {
        if (quoted) {
            if (*p == '"') break;
        } else if (isspace((unsigned char)*p)) {
            break;
        }

        if (len + 1 >= name_size) {
            return 0;
        }
        name[len++] = *p++;
    }

    name[len] = '\0';
    return len > 0;
}

static int terminal_try_executable_path(const char *base, char *resolved,
                                        size_t resolved_size, int *is_batch)
{
    struct stat st;
    *is_batch = 0;
    return stat(base, &st) == 0 && S_ISREG(st.st_mode) &&
           access(base, X_OK) == 0 && copy_text(resolved, resolved_size, base);
}

static int terminal_command_resolves_executable(const char *command,
                                                int *is_batch)
{
    char name[MAX_PATH];
    char candidate[MAX_PATH];
    char saved[MAX_PATH];
    const char *path_env;

    *is_batch = 0;

    if (!terminal_extract_command_name(command, name, sizeof(name))) {
        return 0;
    }

    if (getcwd(saved, sizeof(saved)) == NULL) {
        return 0;
    }

    if (chdir(terminal_cwd) != 0) {
        return 0;
    }

    if (strchr(name, '/') != NULL ||
        strchr(name, '/') != NULL ||
        (name[0] != '\0' && name[1] == ':')) {
        int found = terminal_try_executable_path(name, candidate,
                                                 sizeof(candidate),
                                                 is_batch);
        bw_restore_directory(saved);
        return found;
    }

    if (terminal_try_executable_path(name, candidate,
                                     sizeof(candidate), is_batch)) {
        bw_restore_directory(saved);
        return 1;
    }

    path_env = getenv("PATH");
    if (path_env != NULL) {
        const char *p = path_env;

        while (*p != '\0') {
            char dir[MAX_PATH];
            size_t len = 0;

            while (*p != '\0' && *p != ':') {
                if (len + 1 < sizeof(dir)) {
                    dir[len++] = *p;
                }
                ++p;
            }
            if (*p == ':') ++p;
            dir[len] = '\0';

            if (len > 0 &&
                join_path(candidate, sizeof(candidate), dir, name) &&
                terminal_try_executable_path(candidate, candidate,
                                             sizeof(candidate),
                                             is_batch)) {
                bw_restore_directory(saved);
                return 1;
            }
        }
    }

    bw_restore_directory(saved);
    return 0;
}

static void terminal_execute(void)
{
    char command[TERMINAL_INPUT_LEN];
    char prompt[MAX_PATH + TERMINAL_INPUT_LEN + 2];
    const char *p;

    strcpy(command, terminal_input);
    snprintf(prompt, sizeof(prompt), "%s>%s", terminal_cwd, command);
    terminal_add_wrapped_line(prompt);
    terminal_store_history(command);

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
        terminal_window.open = 0;
        terminal_window.minimized = 0;
        active_window = APP_NONE;
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

    {
        int is_batch = 0;

        if (bw_command_needs_handoff(p) &&
            terminal_command_resolves_executable(p, &is_batch)) {
            if (copy_text(terminal_native_command,
                          sizeof(terminal_native_command), p)) {
                terminal_native_is_batch = is_batch;
                terminal_native_launch_requested = 1;
            } else {
                terminal_add_line("Command line is too long.");
            }
            return;
        }
    }

    terminal_run_external(p);
}

static int terminal_visible_rows(void)
{
    int rows = (terminal_window.h - WINDOW_TITLE_H - 18) / 9;
    return rows > 1 ? rows - 1 : 1;
}

static void draw_terminal_window(void)
{
    int min_x = window_min_button_x(&terminal_window);
    int max_x = window_max_button_x(&terminal_window);
    int close_x = window_close_button_x(&terminal_window);
    int text_x = terminal_window.x + 7;
    int text_y = terminal_window.y + WINDOW_TITLE_H + 6;
    int cols = (terminal_window.w - 14) / 6;
    int rows = terminal_visible_rows();
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
              "BUDOSTACK Terminal", TITLE_TEXT_COLOR, 19);
    draw_window_button(min_x, terminal_window.y + WINDOW_BORDER, 0);
    draw_window_button(max_x, terminal_window.y + WINDOW_BORDER, 1);
    draw_window_button(close_x, terminal_window.y + WINDOW_BORDER, 2);

    fill_rect(terminal_window.x + WINDOW_BORDER,
              terminal_window.y + WINDOW_TITLE_H + 1,
              terminal_window.w - WINDOW_BORDER * 2,
              terminal_window.h - WINDOW_TITLE_H - WINDOW_BORDER - 1,
              TERMINAL_BG_COLOR);
    draw_bevel(terminal_window.x + WINDOW_BORDER - 1,
               terminal_window.y + WINDOW_TITLE_H,
               terminal_window.w - (WINDOW_BORDER - 1) * 2,
               terminal_window.h - WINDOW_TITLE_H - WINDOW_BORDER + 1, 0);

    for (i = 0; i < rows &&
                start + i < terminal_line_count; ++i) {
        draw_text(text_x, text_y + i * 9,
                  terminal_lines[start + i],
                  TERMINAL_TEXT_COLOR, cols);
    }

    if (terminal_paging) {
        draw_text(text_x,
                  terminal_window.y + terminal_window.h - 13,
                  "-- More --  Space: page  Enter: line  Esc: quit",
                  TERMINAL_TEXT_COLOR, cols);
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

    draw_text(text_x,
              terminal_window.y + terminal_window.h - 13,
              prompt + visible_start,
              TERMINAL_TEXT_COLOR, cols);

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
}

static void minimize_terminal(void)
{
    window_minimize_state(&terminal_window);
    if (active_window == APP_TERMINAL) {
        active_window = explorer_window.open && !explorer_window.minimized ?
                        APP_EXPLORER :
                        (editor_window.open && !editor_window.minimized ?
                         APP_EDITOR : APP_NONE);
    }
}

static void close_terminal(void)
{
    window_close_state(&terminal_window);
    terminal_paging = 0;
    terminal_scroll = 0;
    if (active_window == APP_TERMINAL) {
        active_window = explorer_window.open && !explorer_window.minimized ?
                        APP_EXPLORER :
                        (editor_window.open && !editor_window.minimized ?
                         APP_EDITOR : APP_NONE);
    }
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

        if (extended == 15) {
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
    draw_editor_window();
}

static int bwa_open_editor_file(const char *path)
{
    if (path == NULL || !editor_load_file(path)) {
        return 0;
    }

    open_editor();
    return 1;
}

static void bwa_draw_terminal(void)
{
    draw_terminal_window();
}

static BwaAppDefinition bwa_builtin_apps[] = {
    {
        APP_EXPLORER,
        "explorer",
        "File Explorer",
        BWA_FLAG_SINGLETON,
        {NULL, bwa_draw_explorer, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
    },
    {
        APP_EDITOR,
        "editor",
        "Editor",
        BWA_FLAG_SINGLETON,
        {NULL, bwa_draw_editor, NULL, NULL, NULL, NULL, bwa_open_editor_file, NULL, NULL}
    },
    {
        APP_TERMINAL,
        "terminal",
        "Terminal",
        BWA_FLAG_SINGLETON,
        {NULL, bwa_draw_terminal, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
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
        if (bwa_builtin_apps[i].runtime_id == runtime_id) {
            return &bwa_builtin_apps[i];
        }
    }

    return NULL;
}

static void bwa_draw_builtin_app(int runtime_id)
{
    BwaAppDefinition *app = bwa_find_builtin_app(runtime_id);

    if (app != NULL && app->callbacks.draw != NULL) {
        app->callbacks.draw();
    }
}

static int bwa_launch_host_app(int app_id)
{
    if (app_id == BWA_HOST_APP_EXPLORER) {
        open_explorer();
        active_window = APP_EXPLORER;
        return 1;
    }

    if (app_id == BWA_HOST_APP_EDITOR) {
        open_editor();
        return 1;
    }

    if (app_id == BWA_HOST_APP_TERMINAL) {
        open_terminal();
        return 1;
    }

    return 0;
}

static unsigned char bwa_get_system_color(int role)
{
    switch (role) {
        case BUDO_SYS_COLOR_DESKTOP:      return DESKTOP_COLOR;
        case BUDO_SYS_COLOR_TEXT:         return TEXT_COLOR;
        case BUDO_SYS_COLOR_SHADOW:       return WINDOW_SHADOW_COLOR;
        case BUDO_SYS_COLOR_MIDGRAY:      return 3U;
        case BUDO_SYS_COLOR_HIGHLIGHT:    return WINDOW_HIGHLIGHT_COLOR;
        case BUDO_SYS_COLOR_FACE:         return WINDOW_CHROME_COLOR;
        case BUDO_SYS_COLOR_TITLE_ACTIVE: return TITLE_COLOR;
        case BUDO_SYS_COLOR_TITLE_TEXT:   return TITLE_TEXT_COLOR;
        case BUDO_SYS_COLOR_ACCENT:       return 7U;
        default:                          return TEXT_COLOR;
    }
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
        case BUDO_SYS_METRIC_CHAR_WIDTH:     return 6;
        case BUDO_SYS_METRIC_LINE_HEIGHT:    return 9;
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

    draw_window_chrome(&window);
    if (title != NULL) {
        draw_text(x + 7, y + 6, title, TITLE_TEXT_COLOR,
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

static void bwa_draw_standard_button(int x, int y, int w, int h,
                                     const char *label, int pressed)
{
    int text_w;
    int text_x;
    int text_y;

    fill_rect(x, y, w, h, WINDOW_CHROME_COLOR);
    draw_bevel(x, y, w, h, pressed ? 0 : 1);

    if (label == NULL) return;

    text_w = (int)strlen(label) * 6;
    text_x = x + (w - text_w) / 2;
    text_y = y + (h - 7) / 2;
    if (pressed) {
        ++text_x;
        ++text_y;
    }
    draw_text(text_x, text_y, label, TEXT_COLOR, w / 6);
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

    app->open = 0;
    if (app->managed_window) {
        window_close_state(&app->window);
    }

    if (active_window == app->definition.runtime_id) {
        active_window =
            explorer_window.open && !explorer_window.minimized ?
            APP_EXPLORER :
            (editor_window.open && !editor_window.minimized ?
             APP_EDITOR :
             (terminal_window.open && !terminal_window.minimized ?
              APP_TERMINAL : APP_NONE));
    }
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

static int bwa_file_read_all(const char *path,
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

static int bwa_file_write_all(const char *path,
                              const unsigned char *buffer,
                              unsigned int size)
{
    FILE *file;
    size_t written;

    if (path == NULL || buffer == NULL) return 0;

    char user_path[MAX_PATH];
    if (!bw_user_path(user_path, sizeof(user_path), path)) return 0;
    file = fopen(user_path, "wb");
    if (file == NULL) return 0;

    written = fwrite(buffer, 1, size, file);
    if (written != size || ferror(file)) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
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
    fill_rect_rgb
};

static BwaLoadedApp *bwa_find_external_app(int runtime_id)
{
    int i;

    for (i = 0; i < bwa_external_app_count; ++i) {
        if (bwa_external_apps[i].definition.runtime_id == runtime_id) {
            return &bwa_external_apps[i];
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
    BwaLoadedApp *app;

    if (active_window < APP_BWA_BASE) return 0;

    app = bwa_find_external_app(active_window);
    if (app == NULL) return 0;

    if (app->definition.callbacks.close != NULL) {
        bwa_callback_app = app;
        app->definition.callbacks.close();
        bwa_callback_app = NULL;
    }

    app->open = 0;
    if (app->managed_window) {
        window_close_state(&app->window);
    }
    active_window = explorer_window.open && !explorer_window.minimized ?
                    APP_EXPLORER :
                    (editor_window.open && !editor_window.minimized ?
                     APP_EDITOR :
                     (terminal_window.open && !terminal_window.minimized ?
                      APP_TERMINAL : APP_NONE));
    return 1;
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

    app = bwa_find_external_app_id(app_id);
    if (app == NULL || app->definition.callbacks.open_file == NULL) {
        return 0;
    }

    bwa_callback_app = app;
    if (!app->definition.callbacks.open_file(path)) {
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
                  y + ICON_H + 5,
                  label, DESKTOP_TEXT_COLOR, len);
        return;
    }

    center_x = x + ICON_W / 2;
    draw_rect(x + 3, y + 2, ICON_W - 6, ICON_H - 5, DESKTOP_TEXT_COLOR);
    draw_rect(x + 6, y + 5, ICON_W - 12, ICON_H - 11, FOLDER_DARK);
    draw_text(center_x - label_width / 2,
              y + ICON_H + 5, label, DESKTOP_TEXT_COLOR, len);
}

static void bwa_draw_external_app(BwaLoadedApp *app)
{
    if (app == NULL || !app->open ||
        (app->definition.flags & BWA_FLAG_LAUNCHER)) {
        return;
    }

    if (app->managed_window) {
        if (app->window.minimized) return;
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
}

static void bwa_draw_external_nonactive(void)
{
    int i;

    for (i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];

        if (app->definition.runtime_id != active_window) {
            bwa_draw_external_app(app);
        }
    }
}

static void draw_desktop(int page)
{
    int explorer_x;
    int explorer_y;
    int editor_x;
    int editor_y;

    if (desktop_background_loaded) {
        memcpy(framebuffer, desktop_background, sizeof(framebuffer));
        memcpy(rgb_framebuffer, desktop_background_rgb, sizeof(rgb_framebuffer));
        memset(rgb_mask, 1, sizeof(rgb_mask));
    } else {
        memset(framebuffer, DESKTOP_COLOR, sizeof(framebuffer));
        memset(rgb_mask, 0, sizeof(rgb_mask));
    }
    draw_text_centered(8, "BUDOWIN by BUDOSTACK", DESKTOP_TEXT_COLOR);

    desktop_slot_position(explorer_desktop_slot, &explorer_x, &explorer_y);
    desktop_slot_position(editor_desktop_slot, &editor_x, &editor_y);

    if (desktop_icon_dragging && desktop_drag_app == APP_EXPLORER) {
        explorer_x = desktop_drag_x;
        explorer_y = desktop_drag_y;
    } else if (desktop_icon_dragging && desktop_drag_app == APP_EDITOR) {
        editor_x = desktop_drag_x;
        editor_y = desktop_drag_y;
    }

    if (!bwa_has_external_app_id("explorer") &&
        (!explorer_window.open || explorer_window.minimized)) {
        draw_explorer_app_icon(explorer_x, explorer_y);
    }
    if (!bwa_has_external_app_id("editor") &&
        (!editor_window.open || editor_window.minimized)) {
        draw_editor_app_icon(editor_x, editor_y);
    }

    {
        int i;

        for (i = 0; i < bwa_external_app_count; ++i) {
            BwaLoadedApp *app = &bwa_external_apps[i];

            if (!app->open ||
                (app->managed_window && app->window.minimized)) {
                int app_x;
                int app_y;

                desktop_slot_position(app->desktop_slot, &app_x, &app_y);

                if (desktop_icon_dragging &&
                    desktop_drag_app == app->definition.runtime_id) {
                    app_x = desktop_drag_x;
                    app_y = desktop_drag_y;
                }

                draw_bwa_app_icon(app_x, app_y, app);
            }
        }
    }

    bwa_draw_page = page;
    bwa_draw_external_nonactive();

    if (active_window == APP_EXPLORER) {
        bwa_draw_builtin_app(APP_EDITOR);
        bwa_draw_builtin_app(APP_TERMINAL);
        bwa_draw_builtin_app(APP_EXPLORER);
    } else if (active_window == APP_EDITOR) {
        bwa_draw_builtin_app(APP_EXPLORER);
        bwa_draw_builtin_app(APP_TERMINAL);
        bwa_draw_builtin_app(APP_EDITOR);
    } else if (active_window == APP_TERMINAL) {
        bwa_draw_builtin_app(APP_EXPLORER);
        bwa_draw_builtin_app(APP_EDITOR);
        bwa_draw_builtin_app(APP_TERMINAL);
    } else {
        BwaLoadedApp *active_app =
            bwa_find_external_app(active_window);

        bwa_draw_builtin_app(APP_EXPLORER);
        bwa_draw_builtin_app(APP_EDITOR);
        bwa_draw_builtin_app(APP_TERMINAL);
        bwa_draw_external_app(active_app);
    }
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
    return strcmp(current_path, "/") == 0;
}

static int load_directory(const char *path)
{
    struct ffblk entry;
    char pattern[MAX_PATH];
    int done;

    if (!copy_text(current_path, sizeof(current_path), path)) {
        return 0;
    }

    if (!join_path(pattern, sizeof(pattern), current_path, "*.*")) {
        return 0;
    }

    item_count = 0;
    done = findfirst(pattern, &entry, FA_DIREC | FA_HIDDEN | FA_SYSTEM);

    while (!done && item_count < MAX_ITEMS) {
        char full_path[MAX_PATH];

        if (strcmp(entry.ff_name, ".") != 0 &&
            strcmp(entry.ff_name, "..") != 0 &&
            !(entry.ff_attrib & FA_LABEL) &&
            join_path(full_path, sizeof(full_path), current_path, entry.ff_name) &&
            copy_text(items[item_count].name, sizeof(items[item_count].name), entry.ff_name) &&
            copy_text(items[item_count].path, sizeof(items[item_count].path), full_path)) {
            items[item_count].type =
                (entry.ff_attrib & FA_DIREC) ? TYPE_FOLDER : TYPE_FILE;
            ++item_count;
        }

        done = findnext(&entry);
    }

    qsort(items, (size_t)item_count, sizeof(items[0]), compare_items);
    memset(explorer_selection, 0, sizeof(explorer_selection));
    explorer_selected_item = -1;
    return 1;
}

static int parent_path(char *dest, size_t dest_size)
{
    char temp[MAX_PATH];
    char *slash;

    if (is_root_path()) {
        return 0;
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

static int editor_file_load_directory(const char *path)
{
    struct ffblk entry;
    char pattern[MAX_PATH];
    int done;

    if (!copy_text(editor_file_path, sizeof(editor_file_path), path)) {
        return 0;
    }

    if (!join_path(pattern, sizeof(pattern), editor_file_path, "*.*")) {
        return 0;
    }

    editor_file_count = 0;
    done = findfirst(pattern, &entry, FA_DIREC | FA_HIDDEN | FA_SYSTEM);

    while (!done && editor_file_count < MAX_ITEMS) {
        char full_path[MAX_PATH];

        if (strcmp(entry.ff_name, ".") != 0 &&
            strcmp(entry.ff_name, "..") != 0 &&
            !(entry.ff_attrib & FA_LABEL) &&
            join_path(full_path, sizeof(full_path),
                      editor_file_path, entry.ff_name) &&
            copy_text(editor_file_items[editor_file_count].name,
                      sizeof(editor_file_items[editor_file_count].name),
                      entry.ff_name) &&
            copy_text(editor_file_items[editor_file_count].path,
                      sizeof(editor_file_items[editor_file_count].path),
                      full_path)) {
            editor_file_items[editor_file_count].type =
                (entry.ff_attrib & FA_DIREC) ? TYPE_FOLDER : TYPE_FILE;
            ++editor_file_count;
        }

        done = findnext(&entry);
    }

    qsort(editor_file_items, (size_t)editor_file_count,
          sizeof(editor_file_items[0]), compare_items);
    editor_file_top = 0;
    editor_file_selected = -1;
    editor_file_last_click_time = 0;
    return 1;
}

static int editor_file_parent(char *dest, size_t dest_size)
{
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
    editor_file_name_len = (int)len;
}

static void editor_replace_extension(char *name,
                                     size_t name_size,
                                     const char *extension)
{
    char *dot = strrchr(name, '.');
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
    char start_path[MAX_PATH];

    editor_file_dialog = mode;
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
    char accepted_name[MAX_NAME];

    if (editor_file_name[0] == '\0' ||
        !copy_text(accepted_name, sizeof(accepted_name),
                   editor_file_name)) {
        editor_set_status("Choose a file name");
        return 0;
    }

    if (editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS &&
        editor_writer_mode) {
        editor_replace_extension(accepted_name,
                                 sizeof(accepted_name),
                                 ".RTF");
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT) {
        editor_replace_extension(accepted_name,
                                 sizeof(accepted_name),
                                 ".PS");
    }

    if (!join_path(full_path, sizeof(full_path),
                   editor_file_path, accepted_name)) {
        editor_set_status("File name too long");
        return 0;
    }

    if (editor_file_dialog == EDITOR_FILE_DIALOG_OPEN) {
        if (!editor_load_file(full_path)) return 0;
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS) {
        if (!editor_save_file(full_path)) return 0;
    } else if (editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT) {
        if (!editor_export_postscript(full_path)) return 0;
    } else {
        return 0;
    }

    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    return 1;
}

static void editor_file_dialog_click(int x, int y, clock_t now)
{
    int dialog_w = editor_window.w > 470 ? 440 : editor_window.w - 30;
    int dialog_h = editor_window.h > 310 ? 270 : editor_window.h - 40;
    int dialog_x = editor_window.x + (editor_window.w - dialog_w) / 2;
    int dialog_y = editor_window.y + (editor_window.h - dialog_h) / 2;
    int list_x = dialog_x + 10;
    int list_y = dialog_y + 38;
    int list_w = dialog_w - 20;
    int list_h = dialog_h - 102;
    int rows = list_h / 12;
    int ok_x = dialog_x + dialog_w - 132;
    int cancel_x = dialog_x + dialog_w - 68;
    int button_y = dialog_y + dialog_h - 24;

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
                editor_file_selected = index;
                editor_file_last_click_time = now;
                if (item->type == TYPE_FILE) {
                    editor_set_file_name(item->name);
                }
            }
        }
    }
}

static void editor_file_dialog_key(int key)
{
    int dialog_h = editor_window.h > 310 ? 270 : editor_window.h - 40;
    int rows = (dialog_h - 102) / 12;

    if (rows < 1) rows = 1;

    if (key == 27) {
        editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    } else if (key == 8) {
        if (editor_file_name_len > 0) {
            editor_file_name[--editor_file_name_len] = '\0';
        }
    } else if (key == 13) {
        if (editor_file_selected >= 0 &&
            editor_file_selected < editor_file_count &&
            editor_file_items[editor_file_selected].type == TYPE_FOLDER) {
            (void)editor_file_load_directory(
                editor_file_items[editor_file_selected].path);
        } else {
            (void)editor_file_accept();
        }
    } else if (key == 0) {
        int extended = getch();

        if (extended == 72 && editor_file_count > 0) {
            if (editor_file_selected < 0) editor_file_selected = 0;
            else if (editor_file_selected > 0) --editor_file_selected;
        } else if (extended == 80 && editor_file_count > 0) {
            if (editor_file_selected < 0) editor_file_selected = 0;
            else if (editor_file_selected + 1 < editor_file_count)
                ++editor_file_selected;
        }

        if (editor_file_selected < editor_file_top)
            editor_file_top = editor_file_selected;
        if (editor_file_selected >= editor_file_top + rows)
            editor_file_top = editor_file_selected - rows + 1;

        if (editor_file_selected >= 0 &&
            editor_file_selected < editor_file_count &&
            editor_file_items[editor_file_selected].type == TYPE_FILE) {
            editor_set_file_name(editor_file_items[editor_file_selected].name);
        }
    } else if (key >= 32 && key != 127 &&
               editor_file_name_len < MAX_NAME - 1) {
        editor_file_name[editor_file_name_len++] = (char)key;
        editor_file_name[editor_file_name_len] = '\0';
    }
}

static void editor_draw_file_dialog(void)
{
    int dialog_w = editor_window.w > 470 ? 440 : editor_window.w - 30;
    int dialog_h = editor_window.h > 310 ? 270 : editor_window.h - 40;
    int dialog_x = editor_window.x + (editor_window.w - dialog_w) / 2;
    int dialog_y = editor_window.y + (editor_window.h - dialog_h) / 2;
    int list_x = dialog_x + 10;
    int list_y = dialog_y + 38;
    int list_w = dialog_w - 20;
    int list_h = dialog_h - 102;
    int rows = list_h / 12;
    int row;
    int ok_x = dialog_x + dialog_w - 132;
    int cancel_x = dialog_x + dialog_w - 68;
    int button_y = dialog_y + dialog_h - 24;
    const char *title =
        editor_file_dialog == EDITOR_FILE_DIALOG_OPEN ? "Open" :
        editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT ?
        "Export PostScript" : "Save As";
    const char *ok_text =
        editor_file_dialog == EDITOR_FILE_DIALOG_OPEN ? "Open" :
        editor_file_dialog == EDITOR_FILE_DIALOG_EXPORT ?
        "Export" : "Save";

    fill_rect(dialog_x, dialog_y, dialog_w, dialog_h, DESKTOP_COLOR);
    draw_rect(dialog_x, dialog_y, dialog_w, dialog_h, TEXT_COLOR);
    fill_rect(dialog_x + 1, dialog_y + 1, dialog_w - 2, 15, FILE_DARK);
    draw_text(dialog_x + 6, dialog_y + 5, title, TEXT_COLOR, 20);

    draw_text(dialog_x + 8, dialog_y + 21,
              editor_file_path, FOLDER_COLOR, (dialog_w - 65) / 6);
    draw_rect(dialog_x + dialog_w - 48, dialog_y + 18, 38, 14, TEXT_COLOR);
    draw_text(dialog_x + dialog_w - 41, dialog_y + 22, "Up", TEXT_COLOR, 2);

    fill_rect(list_x, list_y, list_w, list_h, FILE_DARK);
    draw_rect(list_x, list_y, list_w, list_h, TEXT_COLOR);

    for (row = 0; row < rows; ++row) {
        int index = editor_file_top + row;
        int y = list_y + 3 + row * 12;

        if (index >= editor_file_count) break;

        if (index == editor_file_selected) {
            fill_rect(list_x + 2, list_y + 1 + row * 12,
                      list_w - 4, 11, FOLDER_DARK);
        }

        if (editor_file_items[index].type == TYPE_FOLDER) {
            draw_text(list_x + 5, y, "[Dir]", FOLDER_COLOR, 5);
            draw_text(list_x + 41, y,
                      editor_file_items[index].name, TEXT_COLOR, 12);
        } else {
            draw_text(list_x + 5, y,
                      editor_file_items[index].name, TEXT_COLOR, 20);
        }
    }

    draw_text(dialog_x + 8, dialog_y + dialog_h - 48,
              "File name:", TEXT_COLOR, 10);
    draw_rect(dialog_x + 72, dialog_y + dialog_h - 53,
              dialog_w - 82, 16, FOLDER_DARK);
    draw_text(dialog_x + 77, dialog_y + dialog_h - 48,
              editor_file_name, TEXT_COLOR, (dialog_w - 92) / 6);
    draw_rect(ok_x, button_y, 58, 16, TEXT_COLOR);
    draw_text(ok_x + 15, button_y + 5, ok_text, TEXT_COLOR, 6);
    draw_rect(cancel_x, button_y, 58, 16, TEXT_COLOR);
    draw_text(cancel_x + 8, button_y + 5, "Cancel", TEXT_COLOR, 6);
}

static int executable_file(const char *name)
{
    char path[MAX_PATH];
    struct stat info;
    return join_path(path, sizeof(path), current_path, name) &&
           stat(path, &info) == 0 && S_ISREG(info.st_mode) &&
           access(path, X_OK) == 0;
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
    int i;

    if (!join_path(path, sizeof(path), home_path, ASSOCIATION_FILE)) {
        return 0;
    }

    file = fopen(path, "wt");
    if (file == NULL) return 0;

    for (i = 0; i < file_association_count; ++i) {
        fprintf(file, "%s %s\n",
                file_associations[i].extension,
                file_associations[i].app_id[0] != '\0' ?
                file_associations[i].app_id : "-");
    }

    fclose(file);
    return 1;
}

static void load_file_associations(void)
{
    static const char *defaults[] = {
        ".TXT", ".RTF", ".INI", ".CFG", ".LOG", ".NFO", ".DIZ", ".MD",
        ".CSV", ".TSV", ".INF", ".LST", ".DOC", ".ME",
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

    if (!join_path(path, sizeof(path), home_path, ASSOCIATION_FILE)) return;

    file = fopen(path, "rt");
    if (file != NULL) {
        char ext[FILE_EXT_LEN + 1];
        char app_id[BWA_ID_LEN];

        while (fscanf(file, "%8s %15s", ext, app_id) == 2) {
            int index = find_file_association(ext);

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

static int bwa_set_file_association(const char *extension,
                                    const char *app_id)
{
    int index;

    if (extension == NULL || app_id == NULL) return 0;

    index = find_file_association(extension);
    if (index < 0) return 0;

    if (!copy_text(file_associations[index].app_id,
                   sizeof(file_associations[index].app_id),
                   app_id)) {
        return 0;
    }
    return save_file_associations();
}


static void normalize_slashes(char *path) { (void)path; }

static int terminal_save_resume_state(void)
{
    FILE *file = fopen(TERMINAL_STATE_FILE, "wt");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "%s\n", terminal_cwd);
    fprintf(file, "%d %d %d %d %d %d %d %d %d %d %d\n",
            terminal_window.x,
            terminal_window.y,
            terminal_window.w,
            terminal_window.h,
            terminal_window.restore_x,
            terminal_window.restore_y,
            terminal_window.restore_w,
            terminal_window.restore_h,
            terminal_window.minimized,
            terminal_window.maximized,
            terminal_scroll);
    fclose(file);
    return 1;
}

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

static int save_session_state(int page)
{
    FILE *file = fopen(STATE_FILE, "wt");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "%s\n", current_path);
    fprintf(file, "%d %d %d %d %d %d %d %d %d %d %d %d %d\n",
            page,
            only_executables,
            explorer_window.open,
            explorer_window.minimized,
            explorer_window.maximized,
            explorer_window.x,
            explorer_window.y,
            explorer_window.w,
            explorer_window.h,
            explorer_window.restore_x,
            explorer_window.restore_y,
            explorer_window.restore_w,
            explorer_window.restore_h);
    fclose(file);
    return 1;
}

static int request_launch(const DesktopItem *item, int page)
{
    if (!save_session_state(page)) return 0;
    return bw_request_launch(item->path, current_path, 0);
}
static int terminal_request_native_launch(const char *command, int is_batch, int page)
{
    (void)is_batch;
    if (!save_session_state(page) || !terminal_save_resume_state()) return 0;
    return bw_request_launch(command, terminal_cwd, 1);
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
        if (items[i].type == TYPE_FOLDER ||
            !only_executables ||
            executable_file(items[i].name)) {
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
        if (items[i].type == TYPE_FOLDER ||
            !only_executables ||
            executable_file(items[i].name)) {
            if (visible == position) {
                return i;
            }
            ++visible;
        }
    }

    return -1;
}

static void explorer_clear_selection(void)
{
    memset(explorer_selection, 0, sizeof(explorer_selection));
    explorer_selected_item = -1;
}

static void explorer_select_all_visible(void)
{
    int position;
    int count = visible_item_count();

    explorer_clear_selection();

    for (position = 0; position < count; ++position) {
        int index = visible_item_at(position);

        if (index >= 0) {
            explorer_selection[index] = 1;
            explorer_selected_item = index;
        }
    }
}

static void explorer_select_range(int first, int last, int add)
{
    int first_pos = -1;
    int last_pos = -1;
    int position;
    int count = visible_item_count();

    for (position = 0; position < count; ++position) {
        int index = visible_item_at(position);

        if (index == first) first_pos = position;
        if (index == last) last_pos = position;
    }

    if (first_pos < 0 || last_pos < 0) {
        if (!add) explorer_clear_selection();
        if (last >= 0 && last < item_count) {
            explorer_selection[last] = 1;
            explorer_selected_item = last;
        }
        return;
    }

    if (!add) explorer_clear_selection();

    if (first_pos > last_pos) {
        int swap = first_pos;
        first_pos = last_pos;
        last_pos = swap;
    }

    for (position = first_pos; position <= last_pos; ++position) {
        int index = visible_item_at(position);
        if (index >= 0) explorer_selection[index] = 1;
    }

    explorer_selected_item = first;
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
    if (lstat(path, &info) != 0) return 0;
    if (S_ISLNK(info.st_mode)) return unlink(path) == 0;
    if (type == TYPE_FILE) {
        return remove(path) == 0;
    }

    {
        struct ffblk entry;
        char pattern[MAX_PATH];
        int done;

        if (!join_path(pattern, sizeof(pattern), path, "*.*")) {
            return 0;
        }

        done = findfirst(pattern, &entry,
                         FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIREC);
        while (!done) {
            if (strcmp(entry.ff_name, ".") != 0 &&
                strcmp(entry.ff_name, "..") != 0 &&
                !(entry.ff_attrib & FA_LABEL)) {
                char child[MAX_PATH];
                int child_type =
                    (entry.ff_attrib & FA_DIREC) ?
                    TYPE_FOLDER : TYPE_FILE;

                if (!join_path(child, sizeof(child),
                               path, entry.ff_name) ||
                    !explorer_delete_path(child, child_type)) {
                    return 0;
                }
            }
            done = findnext(&entry);
        }
    }

    return rmdir(path) == 0;
}

static void explorer_stage_clipboard(int mode)
{
    int i;

    explorer_clipboard_count = 0;
    explorer_clipboard_mode = EXPLORER_CLIP_NONE;

    for (i = 0; i < item_count &&
                explorer_clipboard_count < MAX_ITEMS; ++i) {
        ExplorerClipboardItem *clip;

        if (!explorer_selection[i]) continue;

        clip = &explorer_clipboard[explorer_clipboard_count];
        if (!copy_text(clip->name, sizeof(clip->name), items[i].name) ||
            !copy_text(clip->path, sizeof(clip->path), items[i].path)) {
            continue;
        }

        clip->type = items[i].type;
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

static int explorer_paste_clipboard(void)
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

    if (changed) {
        (void)load_directory(refresh_path);
    }

    return changed;
}

static int explorer_begin_rename(void)
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

    if (count != 1 || selected < 0) return 0;

    if (!copy_text(explorer_rename_input,
                   sizeof(explorer_rename_input),
                   items[selected].name)) {
        return 0;
    }

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
    char old_path[MAX_PATH];
    char new_path[MAX_PATH];
    int i;
    int renamed_index = -1;

    if (!explorer_rename_active ||
        explorer_rename_item < 0 ||
        explorer_rename_item >= item_count ||
        !explorer_rename_name_valid(explorer_rename_input) ||
        !copy_text(old_path, sizeof(old_path),
                   items[explorer_rename_item].path) ||
        !join_path(new_path, sizeof(new_path),
                   current_path, explorer_rename_input)) {
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
                            explorer_rename_input);
        }
    }

    explorer_cancel_rename();

    if (!load_directory(current_path)) return 0;

    for (i = 0; i < item_count; ++i) {
        if (compare_names_ci(items[i].path, new_path) == 0) {
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
        items[selected].type != TYPE_FILE) {
        return 0;
    }

    return bwa_open_editor_file(items[selected].path);
}

static int explorer_delete_selection(void)
{
    char refresh_path[MAX_PATH];
    int i;
    int changed = 0;

    if (!copy_text(refresh_path, sizeof(refresh_path), current_path)) {
        return 0;
    }

    for (i = 0; i < item_count; ++i) {
        if (explorer_selection[i] &&
            explorer_delete_path(items[i].path, items[i].type)) {
            changed = 1;
        }
    }

    if (changed) {
        (void)load_directory(refresh_path);
    }

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
    int per_page = entries_per_page();

    int visible_count = visible_item_count();

    if (visible_count == 0) {
        return 1;
    }

    return (visible_count + per_page - 1) / per_page;
}

static int item_for_slot(int page, int slot)
{
    int first_slot = is_root_path() ? 0 : 1;
    int per_page = entries_per_page();
    int index;

    if (!is_root_path() && slot == 0) {
        return -2;
    }

    if (slot < first_slot) {
        return -1;
    }

    index = page * per_page + (slot - first_slot);

    if (index < 0 || index >= visible_item_count()) {
        return -1;
    }

    return visible_item_at(index);
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
    window_minimize_state(&explorer_window);
}

static void close_explorer(void)
{
    window_close_state(&explorer_window);
}

static void toggle_maximize_explorer(void)
{
    window_toggle_maximize_state(&explorer_window);
}

static void open_editor(void)
{
    window_open_state(&editor_window);
    active_window = APP_EDITOR;
}

static void minimize_editor(void)
{
    editor_menu = EDITOR_MENU_NONE;
    editor_dialog = EDITOR_DIALOG_NONE;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    window_minimize_state(&editor_window);
    if (active_window == APP_EDITOR) {
        active_window = explorer_window.open && !explorer_window.minimized ?
                        APP_EXPLORER : APP_NONE;
    }
}

static void close_editor(void)
{
    editor_menu = EDITOR_MENU_NONE;
    editor_dialog = EDITOR_DIALOG_NONE;
    editor_file_dialog = EDITOR_FILE_DIALOG_NONE;
    window_close_state(&editor_window);
    if (active_window == APP_EDITOR) {
        active_window = explorer_window.open && !explorer_window.minimized ?
                        APP_EXPLORER : APP_NONE;
    }
}

static void toggle_maximize_editor(void)
{
    window_toggle_maximize_state(&editor_window);
    editor_ensure_cursor_visible();
}

int main(int argc, char **argv)
{
    int mouse_x = SCREEN_WIDTH / 2;
    int mouse_y = SCREEN_HEIGHT / 2;
    int previous_mouse_x = mouse_x;
    int previous_mouse_y = mouse_y;
    int buttons = 0;
    int previous_buttons = 0;
    int page = 0;
    int last_target = -99;
    int screen_dirty = 1;
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

    if (!load_initial_directory(&page)) {
        return 1;
    }

    if (resume_explorer) {
        explorer_window.open = 1;
        active_window = APP_EXPLORER;
        clamp_page(&page);
    }

    terminal_load_resume_state();

    if (!set_graphics_mode()) {
        return 1;
    }

    set_classic_gui_palette();

    if (!mouse_init()) {
        set_text_mode();
        return 1;
    }

    for (;;) {
        if (!bw_begin_frame()) break;
        mouse_get_state(&mouse_x, &mouse_y, &buttons);

        if ((buttons & 1) && !(previous_buttons & 1)) {
            clock_t now = bw_clock();
            int handled = 0;

            if (active_window == APP_TERMINAL &&
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
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         wrap_x, editor_window.y + 4,
                                         48, 12)) {
                    if (!editor_writer_mode) {
                        editor_word_wrap = !editor_word_wrap;
                        editor_left_col = 0;
                        editor_ensure_cursor_visible();
                        (void)save_editor_settings();
                    }
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         rows_x, editor_window.y + 4,
                                         48, 12)) {
                    if (!editor_writer_mode) {
                        editor_show_row_numbers =
                            !editor_show_row_numbers;
                        editor_ensure_cursor_visible();
                        (void)save_editor_settings();
                    }
                    screen_dirty = 1;
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         writer_x, editor_window.y + 4,
                                         58, 12)) {
                    editor_writer_mode = !editor_writer_mode;
                    editor_left_col = 0;
                    editor_top_line = 0;
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
                                         104,
                                         editor_writer_mode ? 76 : 64)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 1)) / 12;
                    editor_menu = EDITOR_MENU_NONE;

                    if (item == 0) {
                        editor_reset_document();
                    } else if (item == 1) {
                        editor_begin_file_dialog(EDITOR_FILE_DIALOG_OPEN);
                    } else if (item == 2) {
                        if (editor_writer_mode) {
                            if (editor_path[0] != '\0' &&
                                editor_path_is_rtf(editor_path)) {
                                (void)editor_save_file(editor_path);
                            } else {
                                editor_begin_file_dialog(
                                    EDITOR_FILE_DIALOG_SAVE_AS);
                            }
                        } else if (editor_path[0] != '\0') {
                            (void)editor_save_file(editor_path);
                        } else {
                            editor_begin_file_dialog(
                                EDITOR_FILE_DIALOG_SAVE_AS);
                        }
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
                                         104, 28)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 1)) / 12;
                    editor_menu = EDITOR_MENU_NONE;
                    if (item == 0)
                        editor_delete_line();
                    else if (item == 1)
                        editor_reset_document();
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_SEARCH &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 77,
                                         menu_y + EDITOR_MENU_H,
                                         104, 40)) {
                    int item = (mouse_y -
                                (menu_y + EDITOR_MENU_H + 1)) / 12;
                    editor_menu = EDITOR_MENU_NONE;
                    if (item == 0)
                        editor_begin_dialog(EDITOR_DIALOG_FIND, "");
                    else if (item == 1)
                        editor_begin_dialog(EDITOR_DIALOG_REPLACE_FIND, "");
                    else if (item == 2)
                        editor_begin_dialog(EDITOR_DIALOG_REPLACE_ALL_FIND, "");
                    screen_dirty = 1;
                    handled = 1;
                } else if (editor_menu == EDITOR_MENU_DOCUMENT &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 113,
                                         menu_y + EDITOR_MENU_H,
                                         104, 16)) {
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
                    editor_place_cursor_from_point(mouse_x, mouse_y);
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
                } else if (!editor_window.maximized &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + editor_window.w - 12,
                                         editor_window.y + editor_window.h - 12,
                                         12, 12)) {
                    editor_window.resizing = 1;
                    editor_window.resize_mouse_x = mouse_x;
                    editor_window.resize_mouse_y = mouse_y;
                    editor_window.resize_w = editor_window.w;
                    editor_window.resize_h = editor_window.h;
                } else if (!editor_window.maximized &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_window.x + 2,
                                         editor_window.y + 2,
                                         editor_window.w - 52,
                                         WINDOW_TITLE_H - 2)) {
                    editor_window.dragging = 1;
                    editor_window.drag_dx = mouse_x - editor_window.x;
                    editor_window.drag_dy = mouse_y - editor_window.y;
                }
                handled = 1;
            } else if (active_window == APP_EXPLORER &&
                       window_contains(&explorer_window, mouse_x, mouse_y)) {
                int min_x = window_min_button_x(&explorer_window);
                int max_x = window_max_button_x(&explorer_window);
                int close_x = window_close_button_x(&explorer_window);
                int filter_x = explorer_window.x + 110;
                int filter_y = explorer_window.y + 6;

                if (explorer_rename_active) {
                    handled = 1;
                } else if (point_in_rect(mouse_x, mouse_y,
                                  filter_x, filter_y, 108, 11)) {
                    only_executables = !only_executables;
                    page = 0;
                    explorer_clear_selection();
                    screen_dirty = 1;
                    last_target = -99;
                } else if (point_in_rect(mouse_x, mouse_y,
                                         close_x, explorer_window.y + WINDOW_BORDER, 14, 14)) {
                    close_explorer();
                    active_window = editor_window.open && !editor_window.minimized ?
                                    APP_EDITOR : APP_NONE;
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
                    active_window = editor_window.open && !editor_window.minimized ?
                                    APP_EDITOR : APP_NONE;
                    screen_dirty = 1;
                    last_target = -99;
                } else if (!explorer_window.maximized &&
                           point_in_rect(mouse_x, mouse_y,
                                         explorer_window.x + explorer_window.w - 12,
                                         explorer_window.y + explorer_window.h - 12,
                                         12, 12)) {
                    explorer_window.resizing = 1;
                    explorer_window.resize_mouse_x = mouse_x;
                    explorer_window.resize_mouse_y = mouse_y;
                    explorer_window.resize_w = explorer_window.w;
                    explorer_window.resize_h = explorer_window.h;
                    last_target = -99;
                } else if (!explorer_window.maximized &&
                           point_in_rect(mouse_x, mouse_y,
                                         explorer_window.x + 2,
                                         explorer_window.y + 2,
                                         explorer_window.w - 52,
                                         WINDOW_TITLE_H - 2)) {
                    explorer_window.dragging = 1;
                    explorer_window.drag_dx = mouse_x - explorer_window.x;
                    explorer_window.drag_dy = mouse_y - explorer_window.y;
                    last_target = -99;
                } else {
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
                        } else if (items[target].type == TYPE_FOLDER) {
                            if (load_directory(items[target].path)) {
                                page = 0;
                                screen_dirty = 1;
                            }
                        } else if (executable_file(items[target].name)) {
                            if (request_launch(&items[target], page)) {
                                goto application_exit;
                            }
                        } else if (open_associated_file(items[target].path)) {
                            screen_dirty = 1;
                        }

                        last_target = -99;
                        last_click_time = 0;
                    } else if (target >= 0) {
                        if (shift_down && explorer_selected_item >= 0) {
                            explorer_select_range(explorer_selected_item,
                                                  target, ctrl_down);
                        } else if (ctrl_down) {
                            explorer_selection[target] =
                                explorer_selection[target] ? 0 : 1;
                            if (explorer_selection[target]) {
                                explorer_selected_item = target;
                            }
                        } else {
                            explorer_clear_selection();
                            explorer_selection[target] = 1;
                            explorer_selected_item = target;
                        }

                        screen_dirty = 1;
                        last_target =
                            (!ctrl_down && !shift_down) ? target : -99;
                        last_click_time = now;
                    } else {
                        if (!ctrl_down && !shift_down) {
                            explorer_clear_selection();
                            screen_dirty = 1;
                        }
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
                            active_window =
                                explorer_window.open &&
                                !explorer_window.minimized ?
                                APP_EXPLORER :
                                (editor_window.open &&
                                 !editor_window.minimized ?
                                 APP_EDITOR :
                                 (terminal_window.open &&
                                  !terminal_window.minimized ?
                                  APP_TERMINAL : APP_NONE));
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

                for (i = bwa_external_app_count - 1; i >= 0; --i) {
                    BwaLoadedApp *app = &bwa_external_apps[i];

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

            if (!handled) {
                int explorer_x;
                int explorer_y;
                int editor_x;
                int editor_y;
                int target = -1;

                desktop_slot_position(explorer_desktop_slot,
                                      &explorer_x, &explorer_y);
                desktop_slot_position(editor_desktop_slot,
                                      &editor_x, &editor_y);

                if (!bwa_has_external_app_id("explorer") &&
                    (!explorer_window.open || explorer_window.minimized) &&
                    point_in_rect(mouse_x, mouse_y,
                                  explorer_x - 22, explorer_y - 4, 72, 42)) {
                    target = -10;
                    desktop_drag_app = APP_EXPLORER;
                    desktop_drag_x = explorer_x;
                    desktop_drag_y = explorer_y;
                } else if (!bwa_has_external_app_id("editor") &&
                           (!editor_window.open || editor_window.minimized) &&
                           point_in_rect(mouse_x, mouse_y,
                                         editor_x - 22, editor_y - 4, 72, 42)) {
                    target = -11;
                    desktop_drag_app = APP_EDITOR;
                    desktop_drag_x = editor_x;
                    desktop_drag_y = editor_y;
                } else {
                    int i;

                    desktop_drag_app = APP_NONE;
                    for (i = 0; i < bwa_external_app_count; ++i) {
                        BwaLoadedApp *app = &bwa_external_apps[i];

                        if (!app->open ||
                            (app->managed_window &&
                             app->window.minimized)) {
                            int app_x;
                            int app_y;

                            desktop_slot_position(app->desktop_slot,
                                                  &app_x, &app_y);
                            if (point_in_rect(mouse_x, mouse_y,
                                              app_x - 22, app_y - 4,
                                              72, 42)) {
                                target = app->definition.runtime_id;
                                desktop_drag_app =
                                    app->definition.runtime_id;
                                desktop_drag_x = app_x;
                                desktop_drag_y = app_y;
                                break;
                            }
                        }
                    }
                }

                if (target != -1) {
                    desktop_icon_pressed = 1;
                    desktop_icon_dragging = 0;
                    desktop_press_x = mouse_x;
                    desktop_press_y = mouse_y;
                    desktop_drag_dx = mouse_x - desktop_drag_x;
                    desktop_drag_dy = mouse_y - desktop_drag_y;
                }

                if (target != -1 &&
                    last_target == target &&
                    now - last_click_time <= CLOCKS_PER_SEC / 2) {
                    if (target == -10) {
                        open_explorer();
                        page = 0;
                        active_window = APP_EXPLORER;
                    } else if (target == -11) {
                        open_editor();
                    } else if (target >= APP_BWA_BASE) {
                        BwaLoadedApp *app =
                            bwa_find_external_app(target);

                        if (app != NULL) {
                            int opened = 1;

                            if (app->managed_window && app->open &&
                                app->window.minimized) {
                                app->window.minimized = 0;
                            } else if (app->definition.callbacks.open != NULL) {
                                bwa_callback_app = app;
                                opened = app->definition.callbacks.open();
                                bwa_callback_app = NULL;
                            }

                            if (opened &&
                                !(app->definition.flags &
                                  BWA_FLAG_LAUNCHER)) {
                                app->open = 1;
                                if (app->managed_window) {
                                    app->window.open = 1;
                                    app->window.minimized = 0;
                                }
                                active_window =
                                    app->definition.runtime_id;
                            }
                        }
                    }
                    screen_dirty = 1;
                    desktop_icon_pressed = 0;
                    desktop_icon_dragging = 0;
                    desktop_drag_app = APP_NONE;
                    last_target = -99;
                    last_click_time = 0;
                } else {
                    last_target = target;
                    last_click_time = now;
                }
            }
        }

        if ((buttons & 1) &&
            desktop_icon_pressed &&
            desktop_drag_app != APP_NONE) {
            int dx = mouse_x - desktop_press_x;
            int dy = mouse_y - desktop_press_y;

            if (!desktop_icon_dragging &&
                (dx > 3 || dx < -3 || dy > 3 || dy < -3)) {
                desktop_icon_dragging = 1;
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

                if (new_x != desktop_drag_x || new_y != desktop_drag_y) {
                    desktop_drag_x = new_x;
                    desktop_drag_y = new_y;
                    screen_dirty = 1;
                }
            }
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

        if (active_window >= APP_BWA_BASE &&
            (mouse_x != previous_mouse_x ||
             mouse_y != previous_mouse_y)) {
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

            if (desktop_icon_dragging) {
                int slot = desktop_slot_from_point(desktop_drag_x,
                                                   desktop_drag_y);
                if (desktop_drag_app == APP_EXPLORER) {
                    explorer_desktop_slot = slot;
                } else if (desktop_drag_app == APP_EDITOR) {
                    editor_desktop_slot = slot;
                } else if (desktop_drag_app >= APP_BWA_BASE) {
                    BwaLoadedApp *app =
                        bwa_find_external_app(desktop_drag_app);

                    if (app != NULL) {
                        app->desktop_slot = slot;
                    }
                }

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

            if (active_window == APP_TERMINAL &&
                terminal_window.open &&
                !terminal_window.minimized) {
                if (terminal_handle_key(key)) {
                    screen_dirty = 1;
                }

                if (terminal_native_launch_requested) {
                    terminal_native_launch_requested = 0;

                    if (terminal_request_native_launch(
                            terminal_native_command,
                            terminal_native_is_batch,
                            page)) {
                        goto application_exit;
                    }

                    terminal_add_line("Unable to launch program.");
                    terminal_native_command[0] = '\0';
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
                if (key == 27) {
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
                    } else if (editor_dialog == EDITOR_DIALOG_FIND) {
                        (void)editor_find_from(editor_dialog_input,
                                               editor_cursor_line,
                                               editor_cursor_col + 1);
                        editor_dialog = EDITOR_DIALOG_NONE;
                    } else if (editor_dialog == EDITOR_DIALOG_REPLACE_FIND) {
                        strncpy(editor_replace_find,
                                editor_dialog_input,
                                sizeof(editor_replace_find) - 1);
                        editor_replace_find[
                            sizeof(editor_replace_find) - 1] = '\0';
                        editor_begin_dialog(EDITOR_DIALOG_REPLACE_WITH, "");
                    } else if (editor_dialog == EDITOR_DIALOG_REPLACE_WITH) {
                        if (!editor_replace_at_cursor(editor_replace_find,
                                                      editor_dialog_input)) {
                            if (editor_find_from(editor_replace_find,
                                                 editor_cursor_line,
                                                 editor_cursor_col + 1)) {
                                (void)editor_replace_at_cursor(
                                    editor_replace_find,
                                    editor_dialog_input);
                            }
                        }
                        editor_dialog = EDITOR_DIALOG_NONE;
                    } else if (editor_dialog ==
                               EDITOR_DIALOG_REPLACE_ALL_FIND) {
                        strncpy(editor_replace_find,
                                editor_dialog_input,
                                sizeof(editor_replace_find) - 1);
                        editor_replace_find[
                            sizeof(editor_replace_find) - 1] = '\0';
                        editor_begin_dialog(
                            EDITOR_DIALOG_REPLACE_ALL_WITH, "");
                    } else if (editor_dialog ==
                               EDITOR_DIALOG_REPLACE_ALL_WITH) {
                        (void)editor_replace_all(editor_replace_find,
                                                 editor_dialog_input);
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
            } else if (key == 27 && active_window < APP_BWA_BASE) {
                if (editor_menu != EDITOR_MENU_NONE) {
                    editor_menu = EDITOR_MENU_NONE;
                    screen_dirty = 1;
                } else {
                    break;
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
                    int line_len =
                        (int)strlen(editor_lines[editor_cursor_line]);

                    if (extended == 75) {
                        editor_preferred_visual_col = -1;
                        if (editor_cursor_col > 0) {
                            --editor_cursor_col;
                        } else if (editor_cursor_line > 0) {
                            --editor_cursor_line;
                            editor_cursor_col =
                                (int)strlen(editor_lines[editor_cursor_line]);
                        }
                        editor_ensure_cursor_visible();
                    } else if (extended == 77) {
                        editor_preferred_visual_col = -1;
                        if (editor_cursor_col < line_len) {
                            ++editor_cursor_col;
                        } else if (editor_cursor_line + 1 < editor_line_count) {
                            ++editor_cursor_line;
                            editor_cursor_col = 0;
                        }
                        editor_ensure_cursor_visible();
                    } else if (extended == 72) {
                        editor_move_visual_row(-1);
                    } else if (extended == 80) {
                        editor_move_visual_row(1);
                    } else if (extended == 71) {
                        editor_preferred_visual_col = -1;
                        editor_cursor_col = 0;
                        editor_ensure_cursor_visible();
                    } else if (extended == 79) {
                        editor_preferred_visual_col = -1;
                        editor_cursor_col = line_len;
                        editor_ensure_cursor_visible();
                    } else if (extended == 73) {
                        int i;
                        for (i = 0; i < editor_visible_rows(); ++i)
                            editor_move_visual_row(-1);
                    } else if (extended == 81) {
                        int i;
                        for (i = 0; i < editor_visible_rows(); ++i)
                            editor_move_visual_row(1);
                    }

                    if (extended == 72 || extended == 75 ||
                        extended == 77 || extended == 80 ||
                        extended == 71 || extended == 79 ||
                        extended == 73 || extended == 81) {
                        while (kbhit()) {
                            int queued = getch();
                            if (queued == 0 && kbhit()) {
                                (void)getch();
                            }
                        }
                    }

                    screen_dirty = 1;
                } else if (key == 8) {
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

                    if (extended == 83) {
                        if (explorer_delete_selection()) {
                            clamp_page(&page);
                            screen_dirty = 1;
                        }
                        last_target = -99;
                    } else if (extended == 77 &&
                               page + 1 < page_count()) {
                        ++page;
                        last_target = -99;
                        screen_dirty = 1;
                    } else if (extended == 75 && page > 0) {
                        --page;
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

        if (screen_dirty) {
            draw_desktop(page);

            if (!present_framebuffer()) {
                set_text_mode();
                return 1;
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

        previous_buttons = buttons;
        if (!bw_end_frame()) break;
    }

application_exit:
    set_text_mode();
    bw_finish();
    return 0;
}
