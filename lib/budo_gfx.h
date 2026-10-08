#ifndef BUDO_GFX_H
#define BUDO_GFX_H

#include <stddef.h>
#include <stdint.h>

/* Native Linux/POSIX graphics transport, protocol version 1. No SDL ABI crosses
 * the socket. Each terminal tab advertises its private endpoint in the
 * BUDOSTACK_GFX_SOCKET environment variable. One foreground client per tab.
 * ARGB8888 means numeric 0xAARRGGBB; alpha is ignored for the opaque screen.
 * INDEX8 uses a caller supplied 256-entry ARGB8888 palette.
 */
#define BUDO_GFX_VERSION 1u
#define BUDO_GFX_ARGB8888 1u
#define BUDO_GFX_INDEX8 2u
#define BUDO_GFX_KEY_DOWN 1u
#define BUDO_GFX_KEY_UP 2u
#define BUDO_GFX_MOUSE_MOVE 3u
#define BUDO_GFX_MOUSE_DOWN 4u
#define BUDO_GFX_MOUSE_UP 5u
#define BUDO_GFX_RESET 6u
#define BUDO_GFX_QUIT 7u
#define BUDO_GFX_WHEEL 8u
/* Unicode codepoint in key; supplements key events for text-only input. */
#define BUDO_GFX_TEXT_INPUT 9u

struct budo_gfx_event {
    uint32_t type;
    int32_t key;
    int32_t scancode;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t repeat;
};

struct budo_gfx;
struct budo_gfx_host;

/* Open fails outside apps/terminal: no fallback window is created. */
int budo_gfx_open(struct budo_gfx **out, unsigned int width,
                  unsigned int height, unsigned int format);
/* Copy and publish one complete frame. Blocks until the host has copied it,
 * with a bounded timeout. Input received meanwhile is queued. No tearing or
 * concurrent writing to a buffer the host owns. Returns -1 on disconnect.
 */
int budo_gfx_present(struct budo_gfx *gfx, const void *pixels,
                     const uint32_t palette[256]);
/* 1 = event, 0 = no event, -1 = disconnected. */
int budo_gfx_poll_event(struct budo_gfx *gfx, struct budo_gfx_event *event);
void budo_gfx_close(struct budo_gfx *gfx);
/* Request host keyboard capture on subsequent frames; default is disabled. */
int budo_gfx_set_keyboard_grab(struct budo_gfx *gfx, int enabled);

/* UTF-8 system clipboard. Returned text is caller-owned (free). */
int budo_gfx_set_clipboard(struct budo_gfx *gfx, const char *text);
char *budo_gfx_get_clipboard(struct budo_gfx *gfx);
/* Callbacks run on the host UI thread; get returns a malloc-owned string. */
void budo_gfx_host_clipboard(struct budo_gfx_host *host,
    int (*set)(void *, const char *), char *(*get)(void *), void *context);

/* Host API used by terminal. All calls run on its UI thread. */
int budo_gfx_host_open(struct budo_gfx_host **out);
const char *budo_gfx_host_path(const struct budo_gfx_host *host);
void budo_gfx_host_poll(struct budo_gfx_host *host);
int budo_gfx_host_active(const struct budo_gfx_host *host);
int budo_gfx_host_keyboard_grab(const struct budo_gfx_host *host);
/* The returned RGBA byte buffer remains owned by the host. */
const uint8_t *budo_gfx_host_pixels(struct budo_gfx_host *host,
                                   int *width, int *height, int *dirty);
void budo_gfx_host_event(struct budo_gfx_host *host,
                         const struct budo_gfx_event *event);
void budo_gfx_host_close(struct budo_gfx_host *host);

#endif
