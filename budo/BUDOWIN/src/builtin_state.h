/* Per-window state for host applications. Selected only by the desktop dispatcher.
 * The private aliases preserve helper signatures while every buffer, history
 * and window belongs to an instance. v_ members permit unselected inspection. */
/* Visible rows are rebuilt on navigation/expansion. */
#define EXPLORER_TREE_MAX 1024
#define EXPLORER_TREE_EXPANDED_MAX 256
typedef struct ExplorerFolderRow {
    char path[MAX_PATH];
    char name[MAX_NAME];
    int depth;
    int expandable;
} ExplorerFolderRow;
/* Packed snapshots avoid reserving a full document per history entry. */
#define EDITOR_HISTORY_LIMIT 32
struct EditorSnapshot {
    char *text;
    int lines;
    int row;
    int col;
};

#define BUILTIN_INSTANCE_MAX 24
typedef struct ExplorerState {
    int runtime_id;
    int v_recycle_bin;
    int v_page;
    DesktopItem v_items[MAX_ITEMS];
    int v_item_count;
    char v_current_path[MAX_PATH];
    int v_only_executables;
    unsigned char v_explorer_selection[MAX_ITEMS];
    unsigned char v_explorer_drag_snapshot[MAX_ITEMS];
    BudoSelection v_explorer_select;
    int v_explorer_rename_active;
    int v_explorer_rename_item;
    char v_explorer_rename_input[MAX_NAME];
    int v_explorer_rename_len;
    int v_explorer_drag_shortcut, v_explorer_shortcut_dragging;
    int v_explorer_drag_press_x, v_explorer_drag_press_y, v_explorer_drag_x, v_explorer_drag_y;
    int v_explorer_up_selected;
    int v_explorer_file_menu;
    int v_explorer_list_view;
    ExplorerFolderRow v_explorer_folders[EXPLORER_TREE_MAX];
    char v_explorer_expanded[EXPLORER_TREE_EXPANDED_MAX][MAX_PATH];
    int v_explorer_folder_count, v_explorer_expanded_count;
    int v_explorer_folder_top, v_explorer_folder_focus;
    int v_explorer_folder_keyboard;
    int v_explorer_folder_preferred_width;
    int v_explorer_folder_left;
    int v_explorer_divider_dragging, v_explorer_divider_grab;
    char v_explorer_folder_last_click[MAX_PATH];
    clock_t v_explorer_folder_click_time;
    const char *v_explorer_status;
    char v_explorer_error[160];
    int v_explorer_view_menu;
    int v_explorer_creating_folder;
    BudoScrollbar v_explorer_scroll;
    BudoScrollbar v_explorer_folder_scroll, v_explorer_folder_hscroll;
    AppWindow v_explorer_window;
} ExplorerState;
static ExplorerState explorer_initial;
static ExplorerState explorer_initial = {
    .runtime_id = 1,
    .v_item_count = 0,
    .v_current_path = "/",
    .v_only_executables = 0,
    .v_explorer_select = {
    .selected = explorer_initial.v_explorer_selection, .snapshot = explorer_initial.v_explorer_drag_snapshot,
    .capacity = MAX_ITEMS, .focus = -1, .anchor = -1
},
    .v_explorer_rename_active = 0,
    .v_explorer_rename_item = -1,
    .v_explorer_rename_len = 0,
    .v_explorer_drag_shortcut = -1,
    .v_explorer_file_menu = 0,
    .v_explorer_folder_focus = -1,
    .v_explorer_creating_folder = 0,
    .v_explorer_window = {
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}
};
static const ExplorerState explorer_defaults = {
    .runtime_id = 1,
    .v_item_count = 0,
    .v_current_path = "/",
    .v_only_executables = 0,
    .v_explorer_select = {
    .selected = NULL, .snapshot = NULL,
    .capacity = MAX_ITEMS, .focus = -1, .anchor = -1
},
    .v_explorer_rename_active = 0,
    .v_explorer_rename_item = -1,
    .v_explorer_rename_len = 0,
    .v_explorer_drag_shortcut = -1,
    .v_explorer_file_menu = 0,
    .v_explorer_folder_focus = -1,
    .v_explorer_creating_folder = 0,
    .v_explorer_window = {
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    WINDOW_DEFAULT_X, WINDOW_DEFAULT_Y,
    WINDOW_DEFAULT_W, WINDOW_DEFAULT_H,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}
};
static ExplorerState *explorer_state = &explorer_initial;
static ExplorerState *explorer_instances[BUILTIN_INSTANCE_MAX] = {&explorer_initial};
#define recycle_bin (explorer_state->v_recycle_bin)
#define directory_items (explorer_state->v_items)
#define item_count (explorer_state->v_item_count)
#define current_path (explorer_state->v_current_path)
#define only_executables (explorer_state->v_only_executables)
#define explorer_selection (explorer_state->v_explorer_selection)
#define explorer_drag_snapshot (explorer_state->v_explorer_drag_snapshot)
#define explorer_select (explorer_state->v_explorer_select)
#define explorer_rename_active (explorer_state->v_explorer_rename_active)
#define explorer_rename_item (explorer_state->v_explorer_rename_item)
#define explorer_rename_input (explorer_state->v_explorer_rename_input)
#define explorer_rename_len (explorer_state->v_explorer_rename_len)
#define explorer_drag_shortcut (explorer_state->v_explorer_drag_shortcut)
#define explorer_shortcut_dragging (explorer_state->v_explorer_shortcut_dragging)
#define explorer_drag_press_x (explorer_state->v_explorer_drag_press_x)
#define explorer_drag_press_y (explorer_state->v_explorer_drag_press_y)
#define explorer_drag_x (explorer_state->v_explorer_drag_x)
#define explorer_drag_y (explorer_state->v_explorer_drag_y)
#define explorer_up_selected (explorer_state->v_explorer_up_selected)
#define explorer_file_menu (explorer_state->v_explorer_file_menu)
#define explorer_list_view (explorer_state->v_explorer_list_view)
#define explorer_folders (explorer_state->v_explorer_folders)
#define explorer_expanded (explorer_state->v_explorer_expanded)
#define explorer_folder_count (explorer_state->v_explorer_folder_count)
#define explorer_expanded_count (explorer_state->v_explorer_expanded_count)
#define explorer_folder_top (explorer_state->v_explorer_folder_top)
#define explorer_folder_focus (explorer_state->v_explorer_folder_focus)
#define explorer_folder_keyboard (explorer_state->v_explorer_folder_keyboard)
#define explorer_folder_preferred_width (explorer_state->v_explorer_folder_preferred_width)
#define explorer_folder_left (explorer_state->v_explorer_folder_left)
#define explorer_divider_dragging (explorer_state->v_explorer_divider_dragging)
#define explorer_divider_grab (explorer_state->v_explorer_divider_grab)
#define explorer_folder_last_click (explorer_state->v_explorer_folder_last_click)
#define explorer_folder_click_time (explorer_state->v_explorer_folder_click_time)
#define explorer_status (explorer_state->v_explorer_status)
#define explorer_error (explorer_state->v_explorer_error)
#define explorer_view_menu (explorer_state->v_explorer_view_menu)
#define explorer_creating_folder (explorer_state->v_explorer_creating_folder)
#define explorer_scroll (explorer_state->v_explorer_scroll)
#define explorer_folder_scroll (explorer_state->v_explorer_folder_scroll)
#define explorer_folder_hscroll (explorer_state->v_explorer_folder_hscroll)
#define explorer_window (explorer_state->v_explorer_window)

typedef struct EditorState {
    int runtime_id;
    char v_editor_lines[EDITOR_MAX_LINES][EDITOR_MAX_COLS];
    int v_editor_read_only;
    int v_editor_end_affinity, v_editor_end_line, v_editor_end_col;
    int v_editor_last_click_line, v_editor_last_click_col;
    clock_t v_editor_click_time;
    int v_editor_line_count;
    int v_editor_cursor_line;
    int v_editor_cursor_col;
    int v_editor_anchor_line, v_editor_anchor_col, v_editor_selection_active;
    int v_editor_selecting;
    int v_editor_top_line;
    int v_editor_top_segment;
    int v_editor_left_col;
    int v_editor_menu;
    int v_editor_dialog;
    char v_editor_path[MAX_PATH];
    char v_editor_dialog_input[MAX_PATH];
    int v_editor_dialog_len;
    char v_editor_search_text[MAX_PATH];
    char v_editor_replacement[MAX_PATH];
    int v_editor_search_case;
    int v_editor_search_word;
    int v_editor_search_wrap;
    int v_editor_search_field;
    int v_editor_search_caret;
    int v_editor_search_selected;
    int v_editor_match_line;
    int v_editor_match_col;
    int v_editor_match_len;
    char v_editor_status[64];
    int v_editor_word_wrap;
    int v_editor_show_row_numbers;
    int v_editor_writer_mode;
    int v_editor_writer_ruler;
    int v_editor_writer_left_indent;
    int v_editor_writer_first_indent;
    int v_editor_writer_right_indent;
    int v_editor_writer_tabs[EDITOR_WRITER_TABS];
    int v_editor_ruler_drag;
    char v_editor_last_save[24];
    int v_editor_preferred_visual_col;
    int v_editor_rtf_overflow;
    int v_editor_pending_action;
    char v_editor_pending_path[MAX_PATH];
    uint64_t v_editor_saved_hash;
    BudoScrollbar v_editor_vscroll, v_editor_hscroll;
    AppWindow v_editor_window;
    struct EditorSnapshot v_editor_undo[EDITOR_HISTORY_LIMIT];
    struct EditorSnapshot v_editor_redo[EDITOR_HISTORY_LIMIT];
    int v_editor_undo_count;
    int v_editor_redo_count;
    int v_editor_typing_line;
    int v_editor_typing_col;
    clock_t v_editor_typing_time;
} EditorState;
static EditorState editor_initial = {
    .runtime_id = 2,
    .v_editor_line_count = 1,
    .v_editor_cursor_line = 0,
    .v_editor_cursor_col = 0,
    .v_editor_top_line = 0,
    .v_editor_top_segment = 0,
    .v_editor_left_col = 0,
    .v_editor_menu = EDITOR_MENU_NONE,
    .v_editor_dialog = EDITOR_DIALOG_NONE,
    .v_editor_path = "",
    .v_editor_dialog_input = "",
    .v_editor_dialog_len = 0,
    .v_editor_search_text = "",
    .v_editor_replacement = "",
    .v_editor_search_case = 0,
    .v_editor_search_word = 0,
    .v_editor_search_wrap = 1,
    .v_editor_search_field = 0,
    .v_editor_search_caret = 0,
    .v_editor_search_selected = 0,
    .v_editor_match_line = -1,
    .v_editor_match_col = 0,
    .v_editor_match_len = 0,
    .v_editor_status = "",
    .v_editor_word_wrap = 0,
    .v_editor_show_row_numbers = 1,
    .v_editor_writer_mode = 0,
    .v_editor_writer_ruler = 1,
    .v_editor_writer_left_indent = 0,
    .v_editor_writer_first_indent = 0,
    .v_editor_writer_right_indent = 0,
    .v_editor_writer_tabs = {8, 16, 24, 32, 40, 48, 56, 64},
    .v_editor_ruler_drag = 0,
    .v_editor_last_save = "Not saved",
    .v_editor_preferred_visual_col = -1,
    .v_editor_rtf_overflow = 0,
    .v_editor_pending_action = 0,
    .v_editor_saved_hash = 0,
    .v_editor_window = {
    90, 78, 480, 330,
    90, 78, 480, 330,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
},
    .v_editor_undo_count = 0,
    .v_editor_redo_count = 0,
    .v_editor_typing_line = -1,
    .v_editor_typing_col = -1,
    .v_editor_typing_time = 0
};
static const EditorState editor_defaults = {
    .runtime_id = 2,
    .v_editor_line_count = 1,
    .v_editor_cursor_line = 0,
    .v_editor_cursor_col = 0,
    .v_editor_top_line = 0,
    .v_editor_top_segment = 0,
    .v_editor_left_col = 0,
    .v_editor_menu = EDITOR_MENU_NONE,
    .v_editor_dialog = EDITOR_DIALOG_NONE,
    .v_editor_path = "",
    .v_editor_dialog_input = "",
    .v_editor_dialog_len = 0,
    .v_editor_search_text = "",
    .v_editor_replacement = "",
    .v_editor_search_case = 0,
    .v_editor_search_word = 0,
    .v_editor_search_wrap = 1,
    .v_editor_search_field = 0,
    .v_editor_search_caret = 0,
    .v_editor_search_selected = 0,
    .v_editor_match_line = -1,
    .v_editor_match_col = 0,
    .v_editor_match_len = 0,
    .v_editor_status = "",
    .v_editor_word_wrap = 0,
    .v_editor_show_row_numbers = 1,
    .v_editor_writer_mode = 0,
    .v_editor_writer_ruler = 1,
    .v_editor_writer_left_indent = 0,
    .v_editor_writer_first_indent = 0,
    .v_editor_writer_right_indent = 0,
    .v_editor_writer_tabs = {8, 16, 24, 32, 40, 48, 56, 64},
    .v_editor_ruler_drag = 0,
    .v_editor_last_save = "Not saved",
    .v_editor_preferred_visual_col = -1,
    .v_editor_rtf_overflow = 0,
    .v_editor_pending_action = 0,
    .v_editor_saved_hash = 0,
    .v_editor_window = {
    90, 78, 480, 330,
    90, 78, 480, 330,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
},
    .v_editor_undo_count = 0,
    .v_editor_redo_count = 0,
    .v_editor_typing_line = -1,
    .v_editor_typing_col = -1,
    .v_editor_typing_time = 0
};
static EditorState *editor_state = &editor_initial;
static EditorState *editor_instances[BUILTIN_INSTANCE_MAX] = {&editor_initial};
#define editor_read_only (editor_state->v_editor_read_only)
#define editor_lines (editor_state->v_editor_lines)
#define editor_line_count (editor_state->v_editor_line_count)
#define editor_cursor_line (editor_state->v_editor_cursor_line)
#define editor_cursor_col (editor_state->v_editor_cursor_col)
#define editor_anchor_line (editor_state->v_editor_anchor_line)
#define editor_anchor_col (editor_state->v_editor_anchor_col)
#define editor_selection_active (editor_state->v_editor_selection_active)
#define editor_selecting (editor_state->v_editor_selecting)
#define editor_top_line (editor_state->v_editor_top_line)
#define editor_top_segment (editor_state->v_editor_top_segment)
#define editor_left_col (editor_state->v_editor_left_col)
#define editor_menu (editor_state->v_editor_menu)
#define editor_dialog (editor_state->v_editor_dialog)
#define editor_path (editor_state->v_editor_path)
#define editor_dialog_input (editor_state->v_editor_dialog_input)
#define editor_dialog_len (editor_state->v_editor_dialog_len)
#define editor_search_text (editor_state->v_editor_search_text)
#define editor_replacement (editor_state->v_editor_replacement)
#define editor_search_case (editor_state->v_editor_search_case)
#define editor_search_word (editor_state->v_editor_search_word)
#define editor_search_wrap (editor_state->v_editor_search_wrap)
#define editor_search_field (editor_state->v_editor_search_field)
#define editor_search_caret (editor_state->v_editor_search_caret)
#define editor_search_selected (editor_state->v_editor_search_selected)
#define editor_match_line (editor_state->v_editor_match_line)
#define editor_match_col (editor_state->v_editor_match_col)
#define editor_match_len (editor_state->v_editor_match_len)
#define editor_status (editor_state->v_editor_status)
#define editor_word_wrap (editor_state->v_editor_word_wrap)
#define editor_show_row_numbers (editor_state->v_editor_show_row_numbers)
#define editor_writer_mode (editor_state->v_editor_writer_mode)
#define editor_writer_ruler (editor_state->v_editor_writer_ruler)
#define editor_writer_left_indent (editor_state->v_editor_writer_left_indent)
#define editor_writer_first_indent (editor_state->v_editor_writer_first_indent)
#define editor_writer_right_indent (editor_state->v_editor_writer_right_indent)
#define editor_writer_tabs (editor_state->v_editor_writer_tabs)
#define editor_ruler_drag (editor_state->v_editor_ruler_drag)
#define editor_last_save (editor_state->v_editor_last_save)
#define editor_preferred_visual_col (editor_state->v_editor_preferred_visual_col)
#define editor_rtf_overflow (editor_state->v_editor_rtf_overflow)
#define editor_pending_action (editor_state->v_editor_pending_action)
#define editor_pending_path (editor_state->v_editor_pending_path)
#define editor_saved_hash (editor_state->v_editor_saved_hash)
#define editor_vscroll (editor_state->v_editor_vscroll)
#define editor_hscroll (editor_state->v_editor_hscroll)
#define editor_window (editor_state->v_editor_window)
#define editor_undo (editor_state->v_editor_undo)
#define editor_redo (editor_state->v_editor_redo)
#define editor_undo_count (editor_state->v_editor_undo_count)
#define editor_redo_count (editor_state->v_editor_redo_count)
#define editor_typing_line (editor_state->v_editor_typing_line)
#define editor_typing_col (editor_state->v_editor_typing_col)
#define editor_typing_time (editor_state->v_editor_typing_time)

typedef struct TerminalState {
    char v_terminal_title[MAX_NAME];
    int runtime_id;
    int v_terminal_file_menu;
    BudoScrollbar v_terminal_scrollbar;
    AppWindow v_terminal_window;
    char v_terminal_lines[TERMINAL_MAX_LINES][TERMINAL_LINE_LEN];
    int v_terminal_line_count;
    char v_terminal_input[TERMINAL_INPUT_LEN];
    int v_terminal_input_len;
    int v_terminal_cursor;
    char v_terminal_history[TERMINAL_HISTORY_MAX][TERMINAL_INPUT_LEN];
    int v_terminal_history_count;
    int v_terminal_history_pos;
    int v_terminal_scroll;
    int v_terminal_paging;
    int v_terminal_page_top;
    int v_terminal_page_end;
    char v_terminal_completion[TERMINAL_COMPLETION_MAX][MAX_NAME];
    int v_terminal_completion_count;
    int v_terminal_completion_index;
    int v_terminal_completion_start;
    int v_terminal_completion_end;
    char v_terminal_completion_prefix[MAX_PATH];
    char v_terminal_cwd[MAX_PATH];
} TerminalState;
static TerminalState terminal_initial = {
    .runtime_id = 3,
    .v_terminal_title = "Terminal - BUDOSTACK",
    .v_terminal_file_menu = 0,
    .v_terminal_window = {
    110, 90, 500, 300,
    110, 90, 500, 300,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
},
    .v_terminal_line_count = 0,
    .v_terminal_input = "",
    .v_terminal_input_len = 0,
    .v_terminal_cursor = 0,
    .v_terminal_history_count = 0,
    .v_terminal_history_pos = -1,
    .v_terminal_scroll = 0,
    .v_terminal_paging = 0,
    .v_terminal_page_top = 0,
    .v_terminal_page_end = 0,
    .v_terminal_completion_count = 0,
    .v_terminal_completion_index = -1,
    .v_terminal_completion_start = 0,
    .v_terminal_completion_end = 0,
    .v_terminal_completion_prefix = "",
    .v_terminal_cwd = ""
};
static const TerminalState terminal_defaults = {
    .runtime_id = 3,
    .v_terminal_title = "Terminal - BUDOSTACK",
    .v_terminal_file_menu = 0,
    .v_terminal_window = {
    110, 90, 500, 300,
    110, 90, 500, 300,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
},
    .v_terminal_line_count = 0,
    .v_terminal_input = "",
    .v_terminal_input_len = 0,
    .v_terminal_cursor = 0,
    .v_terminal_history_count = 0,
    .v_terminal_history_pos = -1,
    .v_terminal_scroll = 0,
    .v_terminal_paging = 0,
    .v_terminal_page_top = 0,
    .v_terminal_page_end = 0,
    .v_terminal_completion_count = 0,
    .v_terminal_completion_index = -1,
    .v_terminal_completion_start = 0,
    .v_terminal_completion_end = 0,
    .v_terminal_completion_prefix = "",
    .v_terminal_cwd = ""
};
static TerminalState *terminal_state = &terminal_initial;
static TerminalState *terminal_instances[BUILTIN_INSTANCE_MAX] = {&terminal_initial};
#define terminal_file_menu (terminal_state->v_terminal_file_menu)
#define terminal_scrollbar (terminal_state->v_terminal_scrollbar)
#define terminal_window (terminal_state->v_terminal_window)
#define terminal_lines (terminal_state->v_terminal_lines)
#define terminal_line_count (terminal_state->v_terminal_line_count)
#define terminal_input (terminal_state->v_terminal_input)
#define terminal_input_len (terminal_state->v_terminal_input_len)
#define terminal_cursor (terminal_state->v_terminal_cursor)
#define terminal_history (terminal_state->v_terminal_history)
#define terminal_history_count (terminal_state->v_terminal_history_count)
#define terminal_history_pos (terminal_state->v_terminal_history_pos)
#define terminal_scroll (terminal_state->v_terminal_scroll)
#define terminal_paging (terminal_state->v_terminal_paging)
#define terminal_page_top (terminal_state->v_terminal_page_top)
#define terminal_page_end (terminal_state->v_terminal_page_end)
#define terminal_completion (terminal_state->v_terminal_completion)
#define terminal_completion_count (terminal_state->v_terminal_completion_count)
#define terminal_completion_index (terminal_state->v_terminal_completion_index)
#define terminal_completion_start (terminal_state->v_terminal_completion_start)
#define terminal_completion_end (terminal_state->v_terminal_completion_end)
#define terminal_completion_prefix (terminal_state->v_terminal_completion_prefix)
#define terminal_cwd (terminal_state->v_terminal_cwd)


#define terminal_title (terminal_state->v_terminal_title)
