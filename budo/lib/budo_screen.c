#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "budo_screen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int budo_screen_open(struct budo_screen *screen, int width, int height) {
    memset(screen, 0, sizeof(*screen));
    if (budo_gfx_open(&screen->gfx, (unsigned int)width, (unsigned int)height,
                       BUDO_GFX_ARGB8888) < 0) {
        fprintf(stderr, "Open graphics screen: %s. Launch this application inside apps/terminal.\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

int budo_screen_poll(struct budo_screen *screen, SDL_Event *event) {
    struct budo_gfx_event input;
    for (;;) {
        int result = budo_gfx_poll_event(screen->gfx, &input);
        memset(event, 0, sizeof(*event));
        if (result < 0) {
            if (screen->disconnected) {
                return 0;
            }
            screen->disconnected = 1;
            event->type = SDL_QUIT;
            return 1;
        }
        if (result == 0) {
            return 0;
        }
        if (input.type == BUDO_GFX_RESET) {
            memset(screen->keys, 0, sizeof(screen->keys));
            continue;
        }
        if (input.type == BUDO_GFX_KEY_DOWN || input.type == BUDO_GFX_KEY_UP) {
            int down = input.type == BUDO_GFX_KEY_DOWN;
            if (input.scancode >= 0 && input.scancode < SDL_NUM_SCANCODES) {
                screen->keys[input.scancode] = (Uint8)down;
            }
            event->type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event->key.state = down ? SDL_PRESSED : SDL_RELEASED;
            event->key.repeat = (Uint8)input.repeat;
            event->key.keysym.sym = input.key;
            event->key.keysym.scancode = (SDL_Scancode)input.scancode;
        } else if (input.type == BUDO_GFX_QUIT) {
            event->type = SDL_QUIT;
        } else if (input.type == BUDO_GFX_MOUSE_MOVE) {
            event->type = SDL_MOUSEMOTION;
            event->motion.x = input.x;
            event->motion.y = input.y;
        } else if (input.type == BUDO_GFX_MOUSE_DOWN || input.type == BUDO_GFX_MOUSE_UP) {
            event->type = input.type == BUDO_GFX_MOUSE_DOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event->button.button = (Uint8)input.button;
            event->button.x = input.x;
            event->button.y = input.y;
        } else if (input.type == BUDO_GFX_WHEEL) {
            event->type = SDL_MOUSEWHEEL;
            event->wheel.x = input.x;
            event->wheel.y = input.y;
        } else {
            continue;
        }
        return 1;
    }
}

int budo_screen_present(struct budo_screen *screen, const uint32_t *pixels) {
    if (budo_gfx_present(screen->gfx, pixels, NULL) < 0) {
        screen->disconnected = 1;
        fprintf(stderr, "Graphics session ended: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void budo_screen_close(struct budo_screen *screen) {
    budo_gfx_close(screen->gfx);
    screen->gfx = NULL;
}

int budo_asset_path(char *out, size_t size, const char *argv0, const char *relative) {
    char executable[4096];
    ssize_t count = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
    if (count < 0 || (size_t)count == sizeof(executable) - 1u) {
        if (!argv0 || !realpath(argv0, executable)) {
            return -1;
        }
    } else {
        executable[count] = '\0';
    }
    char *slash = strrchr(executable, '/');
    if (!slash) {
        return -1;
    }
    *slash = '\0';
    int written = snprintf(out, size, "%s/%s", executable, relative);
    if (written < 0 || (size_t)written >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}
