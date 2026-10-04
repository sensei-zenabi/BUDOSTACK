#include "budowin.h"

static const BwaHostApi *host_api;

static int hello_open(void)
{
    return host_api->window_create(
        180, 150, 280, 140,
        "Hello BWA",
        BUDO_WINDOW_DEFAULT_BUTTONS);
}

static void hello_draw(void)
{
    int x;
    int y;
    int w;
    int h;

    if (!host_api->window_get_client_rect(&x, &y, &w, &h)) {
        return;
    }

    host_api->draw_text(x + 12, y + 18,
                        "Hello from a BUDOWIN app",
                        host_api->get_system_color(BUDO_SYS_COLOR_TEXT),
                        (w - 24) / 6);
}

static int hello_key(int key)
{
    if (key == 27) {
        return host_api->window_close();
    }
    return 0;
}

int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (host == 0 || app == 0 ||
        host->abi_major != BWA_ABI_MAJOR ||
        host->abi_minor < 5 ||
        host->window_create == 0 ||
        host->window_get_client_rect == 0 ||
        host->window_close == 0 ||
        host->get_system_color == 0) {
        return 0;
    }

    host_api = host;

    app->runtime_id = 0;
    app->app_id = "hello";
    app->name = "Hello BWA";
    app->flags = BWA_FLAG_SINGLETON;
    app->callbacks.open = hello_open;
    app->callbacks.draw = hello_draw;
    app->callbacks.mouse_down = 0;
    app->callbacks.key = hello_key;
    app->callbacks.close = 0;
    app->callbacks.draw_icon = 0;
    app->callbacks.open_file = 0;

    return 1;
}
