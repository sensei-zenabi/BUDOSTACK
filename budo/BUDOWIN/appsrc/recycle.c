#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int app_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_RECYCLE_BIN);
}

static void app_icon(int x, int y)
{
    host_api->fill_rect(x + 5, y + 7, 16, 20, host_api->get_system_color(BUDO_SYS_COLOR_FACE));
    host_api->draw_rect(x + 5, y + 7, 16, 20, host_api->get_system_color(BUDO_SYS_COLOR_TEXT));
    host_api->fill_rect(x + 3, y + 4, 20, 3, host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT));
}


int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->launch_host_app || !host->get_system_color || !host->open_reader_file) return 0;
    host_api = host;
    app->app_id = "recycle";
    app->name = "Recycle Bin";
    app->flags = BWA_FLAG_LAUNCHER;
    app->callbacks.open = app_open;
    app->callbacks.draw_icon = app_icon;
    app->callbacks.open_file = 0;
    return 1;
}
