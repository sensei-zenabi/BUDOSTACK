#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int app_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_READER);
}

static void app_icon(int x, int y)
{
    host_api->fill_rect(x + 4, y + 2, 16, 24, host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT));
    host_api->draw_text(x + 7, y + 8, "R", host_api->get_system_color(BUDO_SYS_COLOR_TEXT), 1);
}

static int app_open_file(const char *path)
{
    return host_api->open_reader_file(path);
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->launch_host_app || !host->get_system_color || !host->open_reader_file) return 0;
    host_api = host;
    app->app_id = "reader";
    app->name = "Reader";
    app->flags = BWA_FLAG_LAUNCHER;
    app->callbacks.open = app_open;
    app->callbacks.draw_icon = app_icon;
    app->callbacks.open_file = app_open_file;
    return 1;
}
