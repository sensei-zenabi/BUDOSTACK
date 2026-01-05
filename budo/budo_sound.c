#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "budo_sound.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUDO_SOUND_MIN_CHANNEL 1
#define BUDO_SOUND_MAX_CHANNEL 32
#define BUDO_SOUND_MIN_VOLUME 0
#define BUDO_SOUND_MAX_VOLUME 100

int budo_sound_play(int channel, const char *path, int volume) {
    if (channel < BUDO_SOUND_MIN_CHANNEL || channel > BUDO_SOUND_MAX_CHANNEL) {
        fprintf(stderr, "budo_sound_play: channel must be between %d and %d\n",
                BUDO_SOUND_MIN_CHANNEL,
                BUDO_SOUND_MAX_CHANNEL);
        return -1;
    }
    if (!path || path[0] == '\0') {
        fprintf(stderr, "budo_sound_play: audio file path cannot be empty\n");
        return -1;
    }
    if (volume < BUDO_SOUND_MIN_VOLUME || volume > BUDO_SOUND_MAX_VOLUME) {
        fprintf(stderr, "budo_sound_play: volume must be between %d and %d\n",
                BUDO_SOUND_MIN_VOLUME,
                BUDO_SOUND_MAX_VOLUME);
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        perror("budo_sound_play: realpath");
        return -1;
    }

    if (access(resolved, R_OK) != 0) {
        perror("budo_sound_play: access");
        return -1;
    }

    if (printf("\x1b]777;sound=play;channel=%d;path=%s;volume=%d\a",
               channel,
               resolved,
               volume) < 0) {
        perror("budo_sound_play: printf");
        return -1;
    }

    if (fflush(stdout) != 0) {
        perror("budo_sound_play: fflush");
        return -1;
    }

    return 0;
}

int budo_sound_stop(int channel) {
    if (channel < BUDO_SOUND_MIN_CHANNEL || channel > BUDO_SOUND_MAX_CHANNEL) {
        fprintf(stderr, "budo_sound_stop: channel must be between %d and %d\n",
                BUDO_SOUND_MIN_CHANNEL,
                BUDO_SOUND_MAX_CHANNEL);
        return -1;
    }

    if (printf("\x1b]777;sound=stop;channel=%d\a", channel) < 0) {
        perror("budo_sound_stop: printf");
        return -1;
    }

    if (fflush(stdout) != 0) {
        perror("budo_sound_stop: fflush");
        return -1;
    }

    return 0;
}
