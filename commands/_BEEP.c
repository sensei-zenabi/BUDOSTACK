#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef BUDOSTACK_HAVE_ALSA
#define BUDOSTACK_HAVE_ALSA 0
#endif

#if BUDOSTACK_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

static void sleep_ms(unsigned int milliseconds) {
    struct timespec request = {milliseconds / 1000U, (long)(milliseconds % 1000U) * 1000000L};
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
        /* retry until the full duration has elapsed */
    }
}

static int parse_duration(const char *arg, unsigned int *duration_ms) {
    if (arg == NULL || duration_ms == NULL) {
        return -1;
    }

    if (*arg == '\0') {
        fprintf(stderr, "_BEEP: duration is empty\n");
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long value = strtoul(arg, &endptr, 10);

    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_BEEP: invalid duration '%s'\n", arg);
        return -1;
    }

    if (value == 0 || value > UINT_MAX) {
        fprintf(stderr, "_BEEP: duration out of range '%s'\n", arg);
        return -1;
    }

    *duration_ms = (unsigned int)value;
    return 0;
}

static int parse_note(const char *input, double *frequency) {
    if (input == NULL || frequency == NULL) {
        return -1;
    }

    size_t len = strlen(input);
    if (len < 2) {
        fprintf(stderr, "_BEEP: note is too short '%s'\n", input);
        return -1;
    }

    char letter = (char)toupper((unsigned char)input[0]);
    int semitone = 0;

    switch (letter) {
    case 'C':
        semitone = 0;
        break;
    case 'D':
        semitone = 2;
        break;
    case 'E':
        semitone = 4;
        break;
    case 'F':
        semitone = 5;
        break;
    case 'G':
        semitone = 7;
        break;
    case 'A':
        semitone = 9;
        break;
    case 'B':
        semitone = 11;
        break;
    default:
        fprintf(stderr, "_BEEP: unknown note letter '%c'\n", input[0]);
        return -1;
    }

    size_t index = 1;
    int octave_adjust = 0;

    if (index < len && input[index] == '#') {
        ++semitone;
        ++index;
        if (semitone >= 12) {
            semitone -= 12;
            ++octave_adjust;
        }
    } else if (index < len && input[index] == 'b') {
        --semitone;
        ++index;
        if (semitone < 0) {
            semitone += 12;
            --octave_adjust;
        }
    }

    if (index >= len) {
        fprintf(stderr, "_BEEP: octave missing in '%s'\n", input);
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    long octave = strtol(input + index, &endptr, 10);
    if (errno != 0 || endptr == input + index || *endptr != '\0') {
        fprintf(stderr, "_BEEP: invalid octave in '%s'\n", input);
        return -1;
    }

    octave += octave_adjust;

    if (octave < -1 || octave > 9) {
        fprintf(stderr, "_BEEP: octave out of supported range in '%s'\n", input);
        return -1;
    }

    int midi_note = (int)(12 * (octave + 1) + semitone);
    double exponent = ((double)midi_note - 69.0) / 12.0;
    *frequency = 440.0 * pow(2.0, exponent);
    return 0;
}

static int fallback_bell(unsigned int duration_ms) {
    int fds[] = {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO};
    int fallback_fd = -1;

    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i) {
        if (isatty(fds[i])) {
            fallback_fd = fds[i];
            break;
        }
    }

    if (fallback_fd >= 0) {
        ssize_t written = write(fallback_fd, "\a", 1);
        if (written == 1) {
            if (fallback_fd == STDOUT_FILENO) {
                fflush(stdout);
            } else if (fallback_fd == STDERR_FILENO) {
                fflush(stderr);
            }
            sleep_ms(duration_ms);
            return 0;
        }
    }

    int tty_fd = open("/dev/tty", O_WRONLY | O_CLOEXEC);
    if (tty_fd >= 0) {
        ssize_t written = write(tty_fd, "\a", 1);
        close(tty_fd);
        if (written == 1) {
            sleep_ms(duration_ms);
            return 0;
        }
    }

    return -1;
}

static int play_tone(double frequency, unsigned int duration_ms) {
    if (frequency <= 0.0) {
        fprintf(stderr, "_BEEP: invalid frequency %.2f\n", frequency);
        return -1;
    }

#if BUDOSTACK_HAVE_ALSA
    const unsigned int sample_rate = 48000U;
    const double amplitude = 0.2;
    const double two_pi = 6.28318530717958647692;
    snd_pcm_t *handle = NULL;

    int err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "_BEEP: unable to open ALSA device: %s\n", snd_strerror(err));
        fprintf(stderr, "_BEEP: falling back to terminal bell\n");
        return fallback_bell(duration_ms);
    }

    err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, sample_rate, 1,
                              500000);
    if (err < 0) {
        fprintf(stderr, "_BEEP: unable to configure ALSA device: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        fprintf(stderr, "_BEEP: falling back to terminal bell\n");
        return fallback_bell(duration_ms);
    }

    double duration_seconds = (double)duration_ms / 1000.0;
    size_t total_frames = (size_t)llround(duration_seconds * (double)sample_rate);
    if (total_frames == 0) {
        total_frames = 1;
    }

    const size_t chunk_frames = 1024U;
    int16_t buffer[chunk_frames];
    size_t frames_written = 0;

    while (frames_written < total_frames) {
        size_t frames_to_generate = total_frames - frames_written;
        if (frames_to_generate > chunk_frames) {
            frames_to_generate = chunk_frames;
        }

        for (size_t i = 0; i < frames_to_generate; ++i) {
            double sample_index = (double)(frames_written + i);
            double sample = sin(two_pi * frequency * sample_index / (double)sample_rate);
            double scaled = sample * amplitude;
            if (scaled > 1.0) {
                scaled = 1.0;
            } else if (scaled < -1.0) {
                scaled = -1.0;
            }
            buffer[i] = (int16_t)lrint(scaled * 32767.0);
        }

        const int16_t *write_ptr = buffer;
        size_t frames_remaining = frames_to_generate;

        while (frames_remaining > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(handle, write_ptr, frames_remaining);
            if (written == -EPIPE) {
                snd_pcm_prepare(handle);
                continue;
            }
            if (written < 0) {
                fprintf(stderr, "_BEEP: ALSA write error: %s\n", snd_strerror((int)written));
                snd_pcm_close(handle);
                fprintf(stderr, "_BEEP: falling back to terminal bell\n");
                return fallback_bell(duration_ms);
            }

            frames_written += (size_t)written;
            write_ptr += written;
            frames_remaining -= (size_t)written;
        }
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    return 0;
#else
    fprintf(stderr, "_BEEP: built without ALSA support; using terminal bell\n");
    return fallback_bell(duration_ms);
#endif
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: _BEEP -<note> -<duration_ms>\n");
        return EXIT_FAILURE;
    }

    const char *note_arg = argv[1];
    const char *duration_arg = argv[2];

    if (note_arg[0] != '-' || note_arg[1] == '\0') {
        fprintf(stderr, "_BEEP: note argument must be in the format -<note>\n");
        return EXIT_FAILURE;
    }

    if (duration_arg[0] != '-' || duration_arg[1] == '\0') {
        fprintf(stderr, "_BEEP: duration argument must be in the format -<duration_ms>\n");
        return EXIT_FAILURE;
    }

    double frequency = 0.0;
    unsigned int duration_ms = 0;

    if (parse_note(note_arg + 1, &frequency) != 0) {
        return EXIT_FAILURE;
    }

    if (parse_duration(duration_arg + 1, &duration_ms) != 0) {
        return EXIT_FAILURE;
    }

    if (play_tone(frequency, duration_ms) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
