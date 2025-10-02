// _WAIT.c
#define _POSIX_C_SOURCE 199309L   // enable nanosleep from POSIX.1b

#include <stdio.h>
#include <stdlib.h>
#include <time.h>   // nanosleep

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <milliseconds>\n", argv[0]);
        return 1;
    }

    char *endptr;
    long ms = strtol(argv[1], &endptr, 10);

    if (*endptr != '\0' || ms < 0) {
        fprintf(stderr, "Invalid value: %s\n", argv[1]);
        return 1;
    }

    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    if (nanosleep(&ts, NULL) != 0) {
        perror("nanosleep");
        return 1;
    }

    return 0;
}

