// _TIMER.c
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TIMER_STATE_PATH "/tmp/budostack_timer.state"

struct TimerState {
    int running;
    double elapsed_ms;
    struct timespec start_time;
};

static int save_state(const struct TimerState *state) {
    FILE *file = fopen(TIMER_STATE_PATH, "w");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    if (fprintf(file, "%d %.10f %ld %ld\n", state->running, state->elapsed_ms,
                (long)state->start_time.tv_sec, (long)state->start_time.tv_nsec) < 0) {
        perror("fprintf");
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0) {
        perror("fclose");
        return -1;
    }

    return 0;
}

static int load_state(struct TimerState *state) {
    FILE *file = fopen(TIMER_STATE_PATH, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            state->running = 0;
            state->elapsed_ms = 0.0;
            state->start_time.tv_sec = 0;
            state->start_time.tv_nsec = 0;
            return 0;
        }
        perror("fopen");
        return -1;
    }

    long sec = 0;
    long nsec = 0;
    if (fscanf(file, "%d %lf %ld %ld", &state->running, &state->elapsed_ms, &sec, &nsec) != 4) {
        fprintf(stderr, "Failed to read timer state\n");
        fclose(file);
        return -1;
    }

    state->start_time.tv_sec = sec;
    state->start_time.tv_nsec = nsec;

    if (fclose(file) != 0) {
        perror("fclose");
        return -1;
    }

    return 0;
}

static double diff_ms(const struct timespec *start, const struct timespec *end) {
    long sec_diff = end->tv_sec - start->tv_sec;
    long nsec_diff = end->tv_nsec - start->tv_nsec;
    if (nsec_diff < 0) {
        nsec_diff += 1000000000L;
        sec_diff -= 1;
    }
    return (double)sec_diff * 1000.0 + (double)nsec_diff / 1000000.0;
}

static int command_start(struct TimerState *state) {
    if (clock_gettime(CLOCK_MONOTONIC, &state->start_time) != 0) {
        perror("clock_gettime");
        return -1;
    }

    state->running = 1;
    return save_state(state);
}

static int command_stop(struct TimerState *state) {
    if (!state->running) {
        return 0;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        return -1;
    }

    state->elapsed_ms += diff_ms(&state->start_time, &now);
    state->running = 0;
    return save_state(state);
}

static int command_reset(struct TimerState *state) {
    state->elapsed_ms = 0.0;
    if (state->running) {
        if (clock_gettime(CLOCK_MONOTONIC, &state->start_time) != 0) {
            perror("clock_gettime");
            return -1;
        }
    } else {
        state->start_time.tv_sec = 0;
        state->start_time.tv_nsec = 0;
    }
    return save_state(state);
}

static int command_get(struct TimerState *state) {
    double total_ms = state->elapsed_ms;
    if (state->running) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime");
            return -1;
        }
        total_ms += diff_ms(&state->start_time, &now);
    }

    if (printf("%.1f\n", total_ms) < 0) {
        perror("printf");
        return -1;
    }

    return 0;
}

static void print_usage(const char *name) {
    fprintf(stderr, "Usage: %s [--start | --stop | --get | --reset]\n", name);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    struct TimerState state;
    if (load_state(&state) != 0) {
        return 1;
    }

    if (strcmp(argv[1], "--start") == 0) {
        if (command_start(&state) != 0) {
            return 1;
        }
    } else if (strcmp(argv[1], "--stop") == 0) {
        if (command_stop(&state) != 0) {
            return 1;
        }
    } else if (strcmp(argv[1], "--reset") == 0) {
        if (command_reset(&state) != 0) {
            return 1;
        }
    } else if (strcmp(argv[1], "--get") == 0) {
        if (command_get(&state) != 0) {
            return 1;
        }
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

