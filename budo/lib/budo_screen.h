#ifndef BUDO_SCREEN_H
#define BUDO_SCREEN_H

#include "../../lib/budo_gfx.h"
#include <SDL.h>

/* SDL convenience adapter for demos. SDL is used for timers/assets/audio;
 * window creation and SDL_PollEvent are replaced by the terminal transport.
 */
struct budo_screen {
    struct budo_gfx *gfx;
    Uint8 keys[SDL_NUM_SCANCODES];
    int disconnected;
};
int budo_screen_open(struct budo_screen *screen, int width, int height);
int budo_screen_poll(struct budo_screen *screen, SDL_Event *event);
int budo_screen_present(struct budo_screen *screen, const uint32_t *pixels);
void budo_screen_close(struct budo_screen *screen);
/* Resolve relative to the executable, so launching from another CWD works. */
int budo_asset_path(char *out, size_t size, const char *argv0, const char *relative);
#endif
