#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "runtime.h"
#include "../../lib/budo_gfx.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef DHERO_SDL_AUDIO
#include <SDL.h>
#endif

static struct budo_gfx *screen;
static unsigned char pixels[320 * 200];
static uint32_t palette[256];
static unsigned char font[512 * 8];
static int dac_index, dac_channel, cursor_row, cursor_col, pending_scan;
#ifdef DHERO_SDL_AUDIO
static SDL_AudioDeviceID audio;
#endif

static void cleanup(void) {
#ifdef DHERO_SDL_AUDIO
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
#endif
    budo_gfx_close(screen);
    screen = NULL;
}

static void present(void) {
    if (budo_gfx_present(screen, pixels, palette) < 0) {
        fprintf(stderr, "Dungeon Hero: graphics connection closed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void dhero_pixels_write(const void *source, size_t size, unsigned long address) {
    if (address < 0xA0000 || address - 0xA0000 > sizeof(pixels) ||
        size > sizeof(pixels) - (address - 0xA0000)) {
        fprintf(stderr, "Dungeon Hero: invalid framebuffer write\n");
        exit(EXIT_FAILURE);
    }
    memcpy(pixels + address - 0xA0000, source, size);
}

void dhero_pixels_read(unsigned long address, size_t size, void *dest) {
    if (address < 0xA0000 || address - 0xA0000 > sizeof(pixels) ||
        size > sizeof(pixels) - (address - 0xA0000)) {
        fprintf(stderr, "Dungeon Hero: invalid framebuffer read\n");
        exit(EXIT_FAILURE);
    }
    memcpy(dest, pixels + address - 0xA0000, size);
}

void dhero_dac(int port, int value) {
    if (port == 0x3C8) {
        dac_index = value & 255;
        dac_channel = 0;
    } else if (port == 0x3C9) {
        unsigned int shift = 16u - (unsigned int)dac_channel * 8u;
        unsigned int component = ((unsigned int)value & 63u) * 255u / 63u;
        palette[dac_index] = (palette[dac_index] & ~(255u << shift)) |
                             (component << shift) | 0xFF000000u;
        if (++dac_channel == 3) {
            dac_channel = 0;
            dac_index = (dac_index + 1) & 255;
        }
    }
}

void dhero_mode(int mode) {
    memset(pixels, 0, sizeof(pixels));
    cursor_row = cursor_col = 0;
    if (mode == 3) {
        for (int i = 0; i < 256; ++i) palette[i] = 0xFFFFFFFFu;
        palette[0] = 0xFF000000u;
    }
}

static void glyph(int row, int col, unsigned char value, unsigned char color) {
    if (row < 0 || row >= 25 || col < 0 || col >= 40) return;
    for (int y = 0; y < 8; ++y) {
        unsigned char bits = font[(size_t)value * 8u + (size_t)y];
        for (int x = 0; x < 8; ++x) {
            pixels[(row * 8 + y) * 320 + col * 8 + x] =
                bits & (0x80u >> x) ? color : 0;
        }
    }
}

void dhero_text(int row, int col, const char *text, unsigned char color) {
    for (; *text && col < 40; ++text, ++col) glyph(row, col, (unsigned char)*text, color);
}

int dhero_putchar(int value) {
    if (value == '\n') {
        ++cursor_row;
        cursor_col = 0;
    } else if (value == '\r') {
        cursor_col = 0;
    } else if (value == '\b') {
        if (cursor_col > 0) --cursor_col;
    } else {
        glyph(cursor_row, cursor_col++, (unsigned char)value, 255);
        if (cursor_col == 40) {
            cursor_col = 0;
            ++cursor_row;
        }
    }
    if (cursor_row >= 25) {
        memmove(pixels, pixels + 320 * 8, sizeof(pixels) - 320 * 8);
        memset(pixels + 320 * 192, 0, 320 * 8);
        cursor_row = 24;
    }
    return value;
}

int dhero_fputs(const char *text, FILE *stream) {
    if (stream != stdout) return fputs(text, stream);
    while (*text) dhero_putchar((unsigned char)*text++);
    return 0;
}

int dhero_printf(const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    int result = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    dhero_fputs(text, stdout);
    return result;
}

static int scan_code(int code) {
    /* Transport scancodes follow SDL's USB key positions. */
    switch (code) {
        case 82: return 72;
        case 81: return 80;
        case 80: return 75;
        case 79: return 77;
        case 75: return 73;
        case 78: return 81;
        case 76: return 83;
        case 58: return 59;
        case 59: return 60;
        default: return 0;
    }
}

int dhero_getch(void) {
    if (pending_scan) {
        int value = pending_scan;
        pending_scan = 0;
        return value;
    }
    present();
    for (;;) {
        struct budo_gfx_event event;
        int result = budo_gfx_poll_event(screen, &event);
        if (result < 0 || (result > 0 && event.type == BUDO_GFX_QUIT)) exit(EXIT_SUCCESS);
        if (result > 0 && event.type == BUDO_GFX_RESET) pending_scan = 0;
        if (result > 0 && event.type == BUDO_GFX_KEY_DOWN) {
            pending_scan = scan_code(event.scancode);
            if (pending_scan) return 0;
            if (event.key == 10 || event.key == 13) return 13;
            if (event.key > 0 && event.key < 128) return event.key;
        }
        struct timespec pause = {0, 8000000};
        nanosleep(&pause, NULL);
    }
}

void dhero_delay(unsigned int ms) {
    present();
    struct timespec duration = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    while (nanosleep(&duration, &duration) < 0 && errno == EINTR) {}
}

void dhero_tone(unsigned int frequency, unsigned int ms) {
#ifdef DHERO_SDL_AUDIO
    if (audio) {
        SDL_ClearQueuedAudio(audio);
        if (frequency && ms) {
            size_t count = (size_t)ms * 48u;
            int16_t *samples = malloc(count * sizeof(*samples));
            if (!samples) {
                perror("Dungeon Hero audio allocation");
                return;
            }
            for (size_t i = 0; i < count; ++i) {
                samples[i] = ((i * frequency / 24000u) & 1u) ? 2500 : -2500;
            }
            if (SDL_QueueAudio(audio, samples, (Uint32)(count * sizeof(*samples))) < 0)
                fprintf(stderr, "Dungeon Hero audio: %s\n", SDL_GetError());
            free(samples);
        }
    }
#else
    (void)frequency;
#endif
    if (ms) dhero_delay(ms);
}

int dhero_run(int argc, char **argv) {
    char path[4096];
    ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (size < 0) {
        if (!realpath(argv[0], path)) {
            perror("Dungeon Hero executable path");
            return 1;
        }
    } else if ((size_t)size >= sizeof(path) - 1) {
        fprintf(stderr, "Dungeon Hero executable path too long\n");
        return 1;
    } else path[size] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash || (size_t)(slash - path) + sizeof("/DHERO") > sizeof(path)) return 1;
    memcpy(slash, "/DHERO", sizeof("/DHERO"));
    if (chdir(path) < 0) {
        perror("Dungeon Hero assets");
        return 1;
    }
    FILE *file = fopen("system.psf", "rb");
    unsigned char header[4];
    if (!file || fread(header, 1, 4, file) != 4 || header[0] != 0x36 ||
        header[1] != 0x04 || header[3] != 8 || fread(font, 1, 256 * 8, file) != 256 * 8) {
        fprintf(stderr, "Dungeon Hero: missing or invalid 8-pixel PSF font\n");
        if (file) fclose(file);
        return 1;
    }
    fclose(file);
    if (budo_gfx_open(&screen, 320, 200, BUDO_GFX_INDEX8) < 0) {
        fprintf(stderr, "Dungeon Hero: %s. Launch inside apps/terminal.\n", strerror(errno));
        return 1;
    }
    atexit(cleanup);
#ifdef DHERO_SDL_AUDIO
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.freq = 48000;
        spec.format = AUDIO_S16SYS;
        spec.channels = 1;
        spec.samples = 512;
        audio = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);
        else fprintf(stderr, "Dungeon Hero audio: %s\n", SDL_GetError());
    } else fprintf(stderr, "Dungeon Hero audio: %s\n", SDL_GetError());
#endif
    dhero_mode(3);
    if (argc == 2 && strcmp(argv[1], "--editor") == 0) return dhero_editor_run();
    if (argc != 1) {
        fprintf(stderr, "Usage: %s [--editor]\n", argv[0]);
        return 1;
    }
    return dhero_game_run();
}
