#include "../sdk/budowin.h"

static const BwaHostApi *host_api;

static int editor_open(void)
{
    return host_api->launch_host_app(BWA_HOST_APP_EDITOR);
}

static void editor_draw_icon(int x, int y)
{
    host_api->fill_rect(x + 4, y, 16, 16, BUDO_COLOR_SHADOW);
    host_api->fill_rect(x + 5, y + 1, 14, 14, BUDO_COLOR_WHITE);
    host_api->fill_rect(x + 15, y + 1, 4, 4, BUDO_COLOR_DESKTOP);
    host_api->fill_rect(x + 15, y + 4, 4, 1, BUDO_COLOR_SHADOW);
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
    app->app_id = "editor";
    app->name = "Editor";
    app->flags = BWA_FLAG_LAUNCHER;
    app->callbacks.open = editor_open;
    app->callbacks.draw = 0;
    app->callbacks.mouse_down = 0;
    app->callbacks.key = 0;
    app->callbacks.close = 0;
    app->callbacks.open_file = 0;
    app->callbacks.draw_icon = editor_draw_icon;

    return 1;
}
