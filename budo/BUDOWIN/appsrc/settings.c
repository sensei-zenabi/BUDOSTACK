#include "../sdk/ui.h"

#define WIN_X 110
#define WIN_Y 70
#define WIN_W 420
#define WIN_H 340
#define ROW_H 13

static const BwaHostApi *host_api;
static int top_row = 0;
static int file_menu = 0;
static int settings_tab;
static int selected_association = -1;
static int adding_association;
static char new_extension[9];
static unsigned int extension_len;
static const char *settings_status = "Click an entry to change its application.";
static BudoScrollbar association_scroll;

static int text_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void copy_text(char *dest, unsigned int size, const char *src)
{
    unsigned int i = 0;

    if (size == 0) return;

    while (src[i] != '\0' && i + 1 < size) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

static unsigned char ui_color(int role)
{
    return host_api->get_system_color(role);
}

static int settings_rect(int *x, int *y, int *w, int *h)
{
    return host_api->window_get_rect(x, y, w, h);
}

static int visible_rows_for_height(int h)
{
    int list_h = h - 148;
    int rows = list_h / ROW_H;

    return rows > 1 ? rows : 1;
}

static void app_name_for_id(const char *app_id,
                            char *name,
                            unsigned int name_size)
{
    int count = host_api->get_file_app_count();
    int i;

    if (app_id[0] == '\0') {
        name[0] = '\0';
        return;
    }

    for (i = 0; i < count; ++i) {
        char id[BWA_ID_LEN];
        char app_name[BWA_NAME_LEN];

        if (host_api->get_file_app(i,
                                   id, sizeof(id),
                                   app_name, sizeof(app_name)) &&
            text_equal(id, app_id)) {
            copy_text(name, name_size, app_name);
            return;
        }
    }

    copy_text(name, name_size, app_id);
}

static void settings_scroll_configure(void)
{
    int x, y, w, h;
    if (!settings_rect(&x, &y, &w, &h)) return;
    association_scroll.x = x + w - 32;
    association_scroll.y = y + 94;
    association_scroll.length = h - 148;
    association_scroll.total = host_api->get_file_association_count();
    association_scroll.page = visible_rows_for_height(h);
    association_scroll.position = top_row;
    budo_scroll_clamp(&association_scroll);
    top_row = association_scroll.position;
}

static int settings_pointer(int x, int y, int event)
{
    if (!settings_tab) return 0;
    settings_scroll_configure();
    int handled = budo_scroll_pointer_host(host_api, &association_scroll, x, y, event);
    top_row = association_scroll.position;
    return handled;
}

static int settings_mouse_move(int x, int y, int buttons)
{
    if (file_menu) return 1;
    return buttons & 1 ? settings_pointer(x, y, BUDO_POINTER_MOVE) : 0;
}

static int settings_mouse_up(int x, int y, int buttons)
{
    (void)buttons;
    return settings_pointer(x, y, BUDO_POINTER_UP);
}

static void draw_list(void)
{
    int wx;
    int wy;
    int ww;
    int wh;
    int list_x;
    int list_y;
    int list_w;
    int list_h;
    int visible_rows;
    int count;
    int row;

    if (!settings_rect(&wx, &wy, &ww, &wh)) return;
    settings_scroll_configure();
    budo_menu_bar_item(host_api, wx + 5, wy + 23, 36, "File", file_menu);
    host_api->draw_button_state(wx + 16, wy + 46, 126, 22, "User Interface",
                                settings_tab == 0 ? BUDO_BUTTON_PRESSED : 0);
    host_api->draw_button_state(wx + 146, wy + 46, 150, 22, "File Associations",
                                settings_tab == 1 ? BUDO_BUTTON_PRESSED : 0);
    if (!settings_tab) {
        host_api->draw_text(wx + 16, wy + 90, "Keyboard language", ui_color(BUDO_SYS_COLOR_TEXT), 20);
        int nordic = host_api->get_keyboard_layout();
        host_api->draw_button_state(wx + 16, wy + 112, 66, 22, "ENG", nordic ? 0 : BUDO_BUTTON_PRESSED);
        host_api->draw_button_state(wx + 88, wy + 112, 66, 22, "NORD", nordic ? BUDO_BUTTON_PRESSED : 0);
        host_api->draw_text(wx + 16, wy + 148, "NORD: Finnish / Swedish keyboard", ui_color(BUDO_SYS_COLOR_TEXT), (ww - 32) / 6);
        host_api->draw_text(wx + 16, wy + 164, "AltGr: @, brackets and backslash", ui_color(BUDO_SYS_COLOR_TEXT), (ww - 32) / 6);
        host_api->draw_text(wx + 16, wy + wh - 20, settings_status, ui_color(BUDO_SYS_COLOR_TEXT), (ww - 32) / 6);
        return;
    }

    list_x = wx + 16;
    list_y = wy + 94;
    list_w = ww - 48;
    list_h = wh - 148;
    visible_rows = visible_rows_for_height(wh);
    count = host_api->get_file_association_count();

    host_api->draw_text(list_x, wy + 81, "Type",
                        ui_color(BUDO_SYS_COLOR_TEXT), 8);
    host_api->draw_text(list_x + 92, wy + 81, "Application",
                        ui_color(BUDO_SYS_COLOR_TEXT), 16);

    host_api->draw_sunken_panel(list_x - 1, list_y - 1,
                                list_w + 2, list_h + 2,
                                ui_color(BUDO_SYS_COLOR_HIGHLIGHT));

    for (row = 0; row < visible_rows; ++row) {
        int index = top_row + row;
        char ext[12];
        char app_id[BWA_ID_LEN];
        char app_name[BWA_NAME_LEN];
        int y;

        if (index >= count) break;
        if (!host_api->get_file_association(index,
                                             ext, sizeof(ext),
                                             app_id, sizeof(app_id))) {
            continue;
        }

        app_name_for_id(app_id, app_name, sizeof(app_name));
        if (index == selected_association)
            host_api->fill_rect(list_x + 2, list_y + row * ROW_H + 1, list_w - 4, ROW_H - 1,
                                ui_color(BUDO_SYS_COLOR_TITLE_ACTIVE));
        y = list_y + row * ROW_H + 3;
        host_api->draw_text(list_x + 5, y, ext,
                            ui_color(index == selected_association ? BUDO_SYS_COLOR_TITLE_TEXT : BUDO_SYS_COLOR_TEXT), 8);
        host_api->draw_text(list_x + 92, y,
                            app_name[0] != '\0' ? app_name : "(None)",
                            ui_color(index == selected_association ? BUDO_SYS_COLOR_TITLE_TEXT : BUDO_SYS_COLOR_TEXT), 24);
    }

    budo_scroll_draw(host_api, &association_scroll);
    host_api->draw_standard_button(wx + 16, wy + wh - 46, 58, 20, adding_association ? "Add" : "New...", 0);
    host_api->draw_button_state(wx + 80, wy + wh - 46, 66, 20, "Delete",
                                selected_association < 0 ? BUDO_BUTTON_DISABLED : 0);
    if (adding_association) {
        host_api->draw_sunken_panel(wx + 156, wy + wh - 46, 72, 20, ui_color(BUDO_SYS_COLOR_HIGHLIGHT));
        host_api->draw_text(wx + 160, wy + wh - 40, new_extension, ui_color(BUDO_SYS_COLOR_TEXT), 8);
        host_api->pointer_region(wx + 156, wy + wh - 46, 72, 20, BUDO_CURSOR_TEXT, "Extension, e.g. .TXT; Enter adds it");
    }
    host_api->draw_text(wx + 16, wy + wh - 20,
                        settings_status,
                        ui_color(BUDO_SYS_COLOR_TEXT),
                        (ww - 32) / 6);
}

static int settings_open(void)
{
    top_row = 0;
    settings_tab = adding_association = 0;
    selected_association = -1;
    settings_status = "Keyboard settings are saved automatically.";
    association_scroll.held = association_scroll.armed = association_scroll.dragging = 0;
    if (!host_api->window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                                  "Settings",
                                  BUDO_WINDOW_DEFAULT_BUTTONS)) return 0;
    return host_api->window_set_min_size(360, 220);
}

static void settings_draw(void)
{
    draw_list();
    if (file_menu) {
        int x,y,w,h;
        const char *items[1] = {"Close"};
        if (settings_rect(&x,&y,&w,&h))
            budo_menu_draw(host_api, x + 5, y + 41, 140, items, 1);
    }
}

static void cycle_association(int index)
{
    char ext[12];
    char current[BWA_ID_LEN];
    int app_count;
    int i;
    int current_index = -1;
    char next_id[BWA_ID_LEN];

    if (!host_api->get_file_association(index,
                                         ext, sizeof(ext),
                                         current, sizeof(current))) {
        return;
    }

    app_count = host_api->get_file_app_count();

    for (i = 0; i < app_count; ++i) {
        char id[BWA_ID_LEN];
        char name[BWA_NAME_LEN];

        if (host_api->get_file_app(i, id, sizeof(id), name, sizeof(name)) &&
            text_equal(id, current)) {
            current_index = i;
            break;
        }
    }

    if (current[0] == '\0') {
        if (app_count > 0) {
            char name[BWA_NAME_LEN];
            if (!host_api->get_file_app(0, next_id, sizeof(next_id),
                                         name, sizeof(name))) {
                return;
            }
        } else {
            return;
        }
    } else if (current_index + 1 < app_count) {
        char name[BWA_NAME_LEN];
        if (!host_api->get_file_app(current_index + 1,
                                     next_id, sizeof(next_id),
                                     name, sizeof(name))) {
            return;
        }
    } else {
        next_id[0] = '\0';
    }

    settings_status = host_api->set_file_association(ext, next_id) ?
                      "Association saved." : "Save failed; previous association preserved.";
}

static void add_association(void)
{
    int count = host_api->get_file_association_count();
    for (int i = 0; i < count; ++i) {
        char ext[12], id[BWA_ID_LEN];
        if (host_api->get_file_association(i, ext, sizeof(ext), id, sizeof(id)) && text_equal(ext, new_extension)) {
            settings_status = "Extension already exists. Click its row to change it.";
            return;
        }
    }
    if (host_api->set_file_association(new_extension, "editor")) {
        adding_association = 0;
        selected_association = host_api->get_file_association_count() - 1;
        settings_status = "Association saved. Click a row to change application.";
    } else settings_status = "Invalid extension, full list, or save failed.";
}

static void delete_association(void)
{
    char ext[12], id[BWA_ID_LEN];
    if (host_api->get_file_association(selected_association, ext, sizeof(ext), id, sizeof(id))) {
        settings_status = host_api->delete_file_association(ext) ? "Association deleted." : "Delete failed; association preserved.";
        selected_association = -1;
    }
}

static int settings_mouse_down(int x, int y, int buttons)
{
    int wx;
    int wy;
    int ww;
    int wh;
    int list_x;
    int list_y;
    int list_w;
    int list_h;
    int count;

    (void)buttons;

    if (!settings_rect(&wx, &wy, &ww, &wh)) return 0;

    list_x = wx + 16;
    list_y = wy + 94;
    list_w = ww - 48;
    list_h = wh - 148;
    count = host_api->get_file_association_count();

    if (file_menu) {
        int item = budo_menu_hit(x,y,wx+5,wy+41,140,1);
        file_menu = 0;
        if (item == 0) (void)host_api->window_close();
        return 1;
    }
    if (host_api->point_in_rect(x,y,wx+5,wy+23,36,18)) {
        file_menu = 1;
        return 1;
    }
    if (host_api->point_in_rect(x, y, wx + 16, wy + 46, 280, 22)) {
        settings_tab = x >= wx + 146;
        adding_association = 0;
        settings_status = settings_tab ? "Click an entry to change its application." : "Keyboard settings are saved automatically.";
        return 1;
    }
    if (!settings_tab) {
        if (host_api->point_in_rect(x, y, wx + 16, wy + 112, 138, 22)) {
            settings_status = host_api->set_keyboard_layout(x >= wx + 88) ? "Keyboard language saved." : "Save failed; previous layout preserved.";
            return 1;
        }
        return 0;
    }
    if (host_api->point_in_rect(x, y, wx + 16, wy + wh - 46, 58, 20)) {
        if (adding_association) add_association();
        else {
            adding_association = 1;
            copy_text(new_extension, sizeof(new_extension), ".");
            extension_len = 1;
            settings_status = "Type an extension, then press Enter or Add.";
        }
        return 1;
    }
    if (host_api->point_in_rect(x, y, wx + 80, wy + wh - 46, 66, 20)) {
        delete_association();
        return 1;
    }
    if (settings_pointer(x, y, BUDO_POINTER_DOWN)) return 1;

    if (host_api->point_in_rect(x, y,
                                list_x, list_y, list_w, list_h)) {
        int row = (y - list_y) / ROW_H;
        int index = top_row + row;

        if (index >= 0 && index < count) {
            selected_association = index;
            cycle_association(index);
            return 1;
        }
    }

    return 0;
}

static int settings_key(int key)
{
    int wh;
    int count;
    int visible_rows;

    if (!settings_rect(0, 0, 0, &wh)) return 0;

    count = host_api->get_file_association_count();
    visible_rows = visible_rows_for_height(wh);

    if (adding_association) {
        if (key == 27) adding_association = 0;
        else if (key == 13) add_association();
        else if (key == 8 && extension_len) new_extension[--extension_len] = '\0';
        else if (((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
                  (key >= '0' && key <= '9') || key == '.') && extension_len < 8) {
            new_extension[extension_len++] = (char)(key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key);
            new_extension[extension_len] = '\0';
        }
        return 1;
    }
    if (settings_tab && key == (0x100 | 83)) { delete_association(); return 1; }
    if (key == 27) {
        if (file_menu) { file_menu = 0; return 1; }
        return host_api->window_close();
    }
    if (!settings_tab) return 0;
    if (key >= 0x100) {
        int scan = key & 255;
        if (scan == 201) top_row -= 3;
        else if (scan == 202) top_row += 3;
        else if (scan == 72) --top_row;
        else if (scan == 80) ++top_row;
        else if (scan == 73) top_row -= visible_rows;
        else if (scan == 81) top_row += visible_rows;
        else if (scan == 71) top_row = 0;
        else if (scan == 79) top_row = count;
        else return 0;
        settings_scroll_configure();
        return 1;
    }
    if (key == 'u' || key == 'U') {
        if (top_row > 0) --top_row;
        return 1;
    }
    if (key == 'd' || key == 'D') {
        if (top_row + visible_rows < count) ++top_row;
        return 1;
    }

    return 0;
}

static void settings_draw_icon(int x, int y)
{
    host_api->fill_rect(x + 2, y + 2, 20, 14,
                        ui_color(BUDO_SYS_COLOR_SHADOW));
    host_api->fill_rect(x + 3, y + 3, 18, 12,
                        ui_color(BUDO_SYS_COLOR_FACE));
    host_api->draw_text(x + 6, y + 5, "SET",
                        ui_color(BUDO_SYS_COLOR_TEXT), 3);
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (host == 0 || app == 0 ||
        host->abi_major != BWA_ABI_MAJOR ||
        host->abi_minor < 13 ||
        host->get_system_color == 0 ||
        host->draw_standard_button == 0 ||
        host->draw_sunken_panel == 0 ||
        host->window_create == 0 ||
        host->window_set_min_size == 0 ||
        host->window_get_rect == 0 ||
        host->window_close == 0 ||
        host->get_file_association_count == 0 ||
        host->get_file_association == 0 ||
        host->set_file_association == 0 ||
        host->get_file_app_count == 0 ||
        host->get_file_app == 0 ||
        host->get_keyboard_layout == 0 || host->set_keyboard_layout == 0 ||
        host->delete_file_association == 0) {
        return 0;
    }

    host_api = host;

    app->runtime_id = 0;
    app->app_id = "settings";
    app->name = "Settings";
    app->flags = BWA_FLAG_SINGLETON;
    app->callbacks.open = settings_open;
    app->callbacks.draw = settings_draw;
    app->callbacks.mouse_down = settings_mouse_down;
    app->callbacks.key = settings_key;
    app->callbacks.close = 0;
    app->callbacks.draw_icon = settings_draw_icon;
    app->callbacks.open_file = 0;
    app->callbacks.mouse_move = settings_mouse_move;
    app->callbacks.mouse_up = settings_mouse_up;

    return 1;
}
