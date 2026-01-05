#define _POSIX_C_SOURCE 200809L

#include "budo_sound.h"

#include <stdio.h>
#include <time.h>

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
