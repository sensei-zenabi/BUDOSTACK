#define _POSIX_C_SOURCE 200809L

#include "budo_sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(BUDOSTACK_HAVE_SDL2)
#if BUDOSTACK_HAVE_SDL2
#include <SDL2/SDL.h>
#define BUDO_SOUND_HAVE_SDL2 1
#else
#define BUDO_SOUND_HAVE_SDL2 0
#endif
#elif defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDO_SOUND_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDO_SOUND_HAVE_SDL2 1
#else
#define BUDO_SOUND_HAVE_SDL2 0
#endif
#else
#define BUDO_SOUND_HAVE_SDL2 0
#endif

#if BUDO_SOUND_HAVE_SDL2
static SDL_AudioDeviceID budo_audio_device = 0;
static int budo_sample_rate = 44100;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void budo_sound_beep(int count, int delay_ms) {
    int i;

    if (count <= 0) {
        return;
    }
    if (delay_ms < 0) {
        delay_ms = 0;
    }

    for (i = 0; i < count; i++) {
        printf("\a");
        fflush(stdout);
        if (delay_ms > 0 && i + 1 < count) {
            struct timespec req;
            req.tv_sec = delay_ms / 1000;
            req.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
            nanosleep(&req, NULL);
        }
    }
}

int budo_sound_init(int sample_rate) {
#if !BUDO_SOUND_HAVE_SDL2
    (void)sample_rate;
    fprintf(stderr, "budo_sound_init: SDL2 not available.\n");
    return -1;
#else
    SDL_AudioSpec desired;

    if (sample_rate <= 0) {
        sample_rate = 44100;
    }
    budo_sample_rate = sample_rate;

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "budo_sound_init: SDL audio init failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    SDL_zero(desired);
    desired.freq = budo_sample_rate;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = 2048;

    budo_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (budo_audio_device == 0) {
        fprintf(stderr, "budo_sound_init: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(budo_audio_device, 0);
    return 0;
#endif
}

void budo_sound_shutdown(void) {
#if BUDO_HAVE_SDL2
    if (budo_audio_device != 0) {
        SDL_ClearQueuedAudio(budo_audio_device);
        SDL_CloseAudioDevice(budo_audio_device);
        budo_audio_device = 0;
    }
#endif
}

int budo_sound_play_tone(int frequency_hz, int duration_ms, int volume) {
#if !BUDO_SOUND_HAVE_SDL2
    (void)frequency_hz;
    (void)duration_ms;
    (void)volume;
    fprintf(stderr, "budo_sound_play_tone: SDL2 not available.\n");
    return -1;
#else
    int sample_count;
    float amplitude;
    float *buffer;

    if (budo_audio_device == 0) {
        fprintf(stderr, "budo_sound_play_tone: audio device not initialized.\n");
        return -1;
    }
    if (frequency_hz <= 0 || duration_ms <= 0) {
        return -1;
    }

    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    amplitude = (float)volume / 100.0f;

    sample_count = (int)((long long)budo_sample_rate * duration_ms / 1000);
    if (sample_count <= 0) {
        return -1;
    }

    buffer = malloc((size_t)sample_count * sizeof(float));
    if (!buffer) {
        fprintf(stderr, "budo_sound_play_tone: out of memory.\n");
        return -1;
    }

    for (int i = 0; i < sample_count; i++) {
        float t = (float)i / (float)budo_sample_rate;
        buffer[i] = amplitude * sinf(2.0f * (float)M_PI * (float)frequency_hz * t);
    }

    if (SDL_QueueAudio(budo_audio_device, buffer, (Uint32)(sample_count * (int)sizeof(float))) != 0) {
        fprintf(stderr, "budo_sound_play_tone: SDL_QueueAudio failed: %s\n", SDL_GetError());
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
#endif
}
