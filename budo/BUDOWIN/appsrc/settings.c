#include "../sdk/budowin.h"

#define WIN_X 110
#define WIN_Y 70
#define WIN_W 420
#define WIN_H 340
#define ROW_H 13

static const BwaHostApi *host_api;
static int top_row = 0;

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
    int list_h = h - 82;
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

    list_x = wx + 16;
    list_y = wy + 48;
    list_w = ww - 32;
    list_h = wh - 82;
    visible_rows = visible_rows_for_height(wh);
    count = host_api->get_file_association_count();

    host_api->draw_text(list_x, wy + 32, "Type",
                        ui_color(BUDO_SYS_COLOR_TEXT), 8);
    host_api->draw_text(list_x + 92, wy + 32, "Application",
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
        y = list_y + row * ROW_H + 3;
        host_api->draw_text(list_x + 5, y, ext,
                            ui_color(BUDO_SYS_COLOR_TEXT), 8);
        host_api->draw_text(list_x + 92, y,
                            app_name[0] != '\0' ? app_name : "(None)",
                            ui_color(BUDO_SYS_COLOR_TEXT), 24);
    }

    host_api->draw_standard_button(wx + 16, wy + wh - 25,
                                   44, 16, "Up", 0);
    host_api->draw_standard_button(wx + 66, wy + wh - 25,
                                   44, 16, "Down", 0);
    host_api->draw_text(wx + 128, wy + wh - 20,
                        "Click an entry to change its application.",
                        ui_color(BUDO_SYS_COLOR_TEXT),
                        (ww - 136) / 6);
}

static int settings_open(void)
{
    top_row = 0;
    return host_api->window_create(
        WIN_X, WIN_Y, WIN_W, WIN_H,
        "Settings - File Associations",
        BUDO_WINDOW_DEFAULT_BUTTONS);
}

static void settings_draw(void)
{
    draw_list();
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

    (void)host_api->set_file_association(ext, next_id);
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
    int visible_rows;
    int count;

    (void)buttons;

    if (!settings_rect(&wx, &wy, &ww, &wh)) return 0;

    list_x = wx + 16;
    list_y = wy + 48;
    list_w = ww - 32;
    list_h = wh - 82;
    visible_rows = visible_rows_for_height(wh);
    count = host_api->get_file_association_count();

    if (host_api->point_in_rect(x, y, wx + 16, wy + wh - 25,
                                44, 16)) {
        if (top_row > 0) --top_row;
        return 1;
    }

    if (host_api->point_in_rect(x, y, wx + 66, wy + wh - 25,
                                44, 16)) {
        if (top_row + visible_rows < count) ++top_row;
        return 1;
    }

    if (host_api->point_in_rect(x, y,
                                list_x, list_y, list_w, list_h)) {
        int row = (y - list_y) / ROW_H;
        int index = top_row + row;

        if (index >= 0 && index < count) {
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

    if (key == 27) {
        return host_api->window_close();
    }
    if (key == 0) {
        return 0;
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
        host->abi_minor < 5 ||
        host->get_system_color == 0 ||
        host->draw_standard_button == 0 ||
        host->draw_sunken_panel == 0 ||
        host->window_create == 0 ||
        host->window_get_rect == 0 ||
        host->window_close == 0 ||
        host->get_file_association_count == 0 ||
        host->get_file_association == 0 ||
        host->set_file_association == 0 ||
        host->get_file_app_count == 0 ||
        host->get_file_app == 0) {
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

    return 1;
}
