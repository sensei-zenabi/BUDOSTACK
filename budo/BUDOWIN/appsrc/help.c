#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int app_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_HELP);
}

static void app_icon(int x, int y)
{
    host_api->draw_text(x + 8, y + 8, "?", host_api->get_system_color(BUDO_SYS_COLOR_HIGHLIGHT), 1);
}


int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->launch_host_app || !host->get_system_color || !host->open_reader_file) return 0;
    host_api = host;
    app->app_id = "help";
    app->name = "Help";
    app->flags = BWA_FLAG_LAUNCHER;
    app->callbacks.open = app_open;
    app->callbacks.draw_icon = app_icon;
    app->callbacks.open_file = 0;
    return 1;
}
