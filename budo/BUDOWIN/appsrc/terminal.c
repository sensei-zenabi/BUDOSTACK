#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int terminal_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_TERMINAL);
}

static void terminal_draw_icon(int x, int y)
{
    host_api->fill_rect(x + 1, y + 1, 22, 15, BUDO_COLOR_TEXT);
    host_api->draw_rect(x, y,
                        BUDO_BUILTIN_ICON_W, BUDO_BUILTIN_ICON_H,
                        BUDO_COLOR_SHADOW);
    host_api->draw_text(x + 3, y + 5, "/>",
                        BUDO_COLOR_WHITE, 3);
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
    app->app_id = "terminal";
    app->name = "Terminal";
    app->flags = BWA_FLAG_SINGLETON | BWA_FLAG_LAUNCHER;
    app->callbacks.open = terminal_open;
    app->callbacks.draw = 0;
    app->callbacks.mouse_down = 0;
    app->callbacks.key = 0;
    app->callbacks.close = 0;
    app->callbacks.open_file = 0;
    app->callbacks.draw_icon = terminal_draw_icon;

    return 1;
}
