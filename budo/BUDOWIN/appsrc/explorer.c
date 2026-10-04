#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int explorer_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_EXPLORER);
}

static void explorer_draw_icon(int x, int y)
{
    host_api->fill_rect(x + 2, y, 9, 4, BUDO_COLOR_CHROME);
    host_api->fill_rect(x, y + 3,
                        BUDO_BUILTIN_ICON_W, 13, BUDO_COLOR_MIDGRAY);
    host_api->fill_rect(x + 1, y + 4,
                        BUDO_BUILTIN_ICON_W - 2, 11, BUDO_COLOR_CHROME);
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (host == 0 || app == 0 ||
        host->abi_major != BWA_ABI_MAJOR ||
        host->launch_host_app == 0) {
        return 0;
    }

    host_api = host;

    app->runtime_id = 0;
    app->app_id = "explorer";
    app->name = "File Explorer";
    app->flags = BWA_FLAG_SINGLETON | BWA_FLAG_LAUNCHER;
    app->callbacks.open = explorer_open;
    app->callbacks.draw = 0;
    app->callbacks.mouse_down = 0;
    app->callbacks.key = 0;
    app->callbacks.close = 0;
    app->callbacks.open_file = 0;
    app->callbacks.draw_icon = explorer_draw_icon;

    return 1;
}
