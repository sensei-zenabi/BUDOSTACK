#define _POSIX_C_SOURCE 200809L

#include "../lib/bss_engine.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef BUDOSTACK_HAVE_ALSA
#define BUDOSTACK_HAVE_ALSA 0
#endif

#if BUDOSTACK_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#define BSS_SAMPLE_RATE 48000U

static void print_usage(void) {
    fprintf(stderr, "Usage: _BSS [--background] [--loop <count>] <duration_ms> <voice1> [voice2] [voice3]\n");
    fprintf(stderr, "  voice format: waveform:freq[:vol][:pulse][:atk_ms][:decay_ms][:sus][:rel_ms]\n");
    fprintf(stderr, "  waveforms: tri, saw, pulse, noise (freq in Hz).\n");
    fprintf(stderr, "  example: _BSS 750 tri:440:0.5:0.5:10:80:0.6:120 saw:660\n");
    fprintf(stderr, "  --background runs playback in a forked child process.\n");
    fprintf(stderr, "  --loop <count> repeats playback; use 0 for infinite looping.\n");
}

static int parse_double(const char *text, double *value) {
    if (!text || !value)
        return -1;

    errno = 0;
    char *endptr = NULL;
    double temp = strtod(text, &endptr);
    if (errno != 0 || endptr == text || (endptr && *endptr != '\0'))
        return -1;

    *value = temp;
    return 0;
}

static int parse_waveform(const char *token, BSSWaveform *waveform) {
    if (!token || !waveform)
        return -1;

    if (strcmp(token, "tri") == 0 || strcmp(token, "triangle") == 0) {
        *waveform = BSS_WAVE_TRIANGLE;
        return 0;
    }
    if (strcmp(token, "saw") == 0 || strcmp(token, "sawtooth") == 0) {
        *waveform = BSS_WAVE_SAW;
        return 0;
    }
    if (strcmp(token, "pulse") == 0 || strcmp(token, "square") == 0) {
        *waveform = BSS_WAVE_PULSE;
        return 0;
    }
    if (strcmp(token, "noise") == 0) {
        *waveform = BSS_WAVE_NOISE;
        return 0;
    }

    return -1;
}

static int token_is_number(const char *token) {
    double value = 0.0;
    return parse_double(token, &value) == 0;
}

static int parse_voice_spec(const char *spec, BSSVoice *voice) {
    if (!spec || !voice)
        return -1;

    BSSVoice parsed = {
        .waveform = BSS_WAVE_TRIANGLE,
        .frequency = 440.0,
        .volume = 0.4,
        .pulse_width = 0.5,
        .attack_s = 0.01,
        .decay_s = 0.08,
        .sustain_level = 0.7,
        .release_s = 0.12
    };

    char *copy = strdup(spec);
    if (!copy) {
        perror("_BSS: strdup");
        return -1;
    }

    char *saveptr = NULL;
    char *token = strtok_r(copy, ":", &saveptr);
    if (!token) {
        free(copy);
        return -1;
    }

    if (token_is_number(token)) {
        if (parse_double(token, &parsed.frequency) != 0) {
            free(copy);
            return -1;
        }
    } else {
        if (parse_waveform(token, &parsed.waveform) != 0) {
            fprintf(stderr, "_BSS: invalid waveform '%s'\n", token);
            free(copy);
            return -1;
        }
        token = strtok_r(NULL, ":", &saveptr);
        if (!token || parse_double(token, &parsed.frequency) != 0) {
            fprintf(stderr, "_BSS: missing or invalid frequency in '%s'\n", spec);
            free(copy);
            return -1;
        }
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token && parse_double(token, &parsed.volume) != 0) {
        fprintf(stderr, "_BSS: invalid volume in '%s'\n", spec);
        free(copy);
        return -1;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token && parse_double(token, &parsed.pulse_width) != 0) {
        fprintf(stderr, "_BSS: invalid pulse width in '%s'\n", spec);
        free(copy);
        return -1;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token) {
        double attack_ms = 0.0;
        if (parse_double(token, &attack_ms) != 0) {
            fprintf(stderr, "_BSS: invalid attack in '%s'\n", spec);
            free(copy);
            return -1;
        }
        parsed.attack_s = attack_ms / 1000.0;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token) {
        double decay_ms = 0.0;
        if (parse_double(token, &decay_ms) != 0) {
            fprintf(stderr, "_BSS: invalid decay in '%s'\n", spec);
            free(copy);
            return -1;
        }
        parsed.decay_s = decay_ms / 1000.0;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token && parse_double(token, &parsed.sustain_level) != 0) {
        fprintf(stderr, "_BSS: invalid sustain in '%s'\n", spec);
        free(copy);
        return -1;
    }

    token = strtok_r(NULL, ":", &saveptr);
    if (token) {
        double release_ms = 0.0;
        if (parse_double(token, &release_ms) != 0) {
            fprintf(stderr, "_BSS: invalid release in '%s'\n", spec);
            free(copy);
            return -1;
        }
        parsed.release_s = release_ms / 1000.0;
    }

    free(copy);
    *voice = parsed;
    return 0;
}

#if BUDOSTACK_HAVE_ALSA
static int play_pcm(const int16_t *buffer, size_t frames, unsigned int sample_rate) {
    snd_pcm_t *handle = NULL;
    int err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "_BSS: unable to open ALSA device: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_set_params(handle,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             1,
                             sample_rate,
                             1,
                             500000);
    if (err < 0) {
        fprintf(stderr, "_BSS: unable to set ALSA params: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    const int16_t *cursor = buffer;
    size_t remaining = frames;
    while (remaining > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(handle, cursor, remaining);
        if (written < 0) {
            written = snd_pcm_recover(handle, (int)written, 1);
        }
        if (written < 0) {
            fprintf(stderr, "_BSS: ALSA write failed: %s\n", snd_strerror((int)written));
            snd_pcm_close(handle);
            return -1;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    return 0;
}
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    int background = 0;
    long loop_count = 1;
    int arg_index = 1;

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-h") == 0 || strcmp(argv[arg_index], "--help") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[arg_index], "-b") == 0 || strcmp(argv[arg_index], "--background") == 0) {
            background = 1;
            ++arg_index;
            continue;
        }
        if (strcmp(argv[arg_index], "--loop") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "_BSS: --loop requires a count\n");
                print_usage();
                return EXIT_FAILURE;
            }
            errno = 0;
            char *endptr = NULL;
            loop_count = strtol(argv[arg_index + 1], &endptr, 10);
            if (errno != 0 || !endptr || *endptr != '\0' || loop_count < 0) {
                fprintf(stderr, "_BSS: invalid loop count '%s'\n", argv[arg_index + 1]);
                print_usage();
                return EXIT_FAILURE;
            }
            arg_index += 2;
            continue;
        }
        break;
    }

    if (argc - arg_index < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    errno = 0;
    char *endptr = NULL;
    long duration_ms = strtol(argv[arg_index], &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0' || duration_ms <= 0 || duration_ms > INT_MAX) {
        fprintf(stderr, "_BSS: invalid duration '%s'\n", argv[arg_index]);
        print_usage();
        return EXIT_FAILURE;
    }

    size_t voice_count = (size_t)(argc - arg_index - 1);
    if (voice_count > BSS_MAX_VOICES) {
        fprintf(stderr, "_BSS: supports up to %d voices\n", BSS_MAX_VOICES);
        return EXIT_FAILURE;
    }

    BSSEngine engine;
    bss_init(&engine, BSS_SAMPLE_RATE);

    for (size_t i = 0; i < voice_count; ++i) {
        BSSVoice voice;
        if (parse_voice_spec(argv[arg_index + 1 + (int)i], &voice) != 0) {
            print_usage();
            return EXIT_FAILURE;
        }
        if (bss_configure_voice(&engine, i, &voice) != 0) {
            fprintf(stderr, "_BSS: failed to configure voice %zu\n", i + 1);
            return EXIT_FAILURE;
        }
    }

    if (background) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("_BSS: fork");
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            return EXIT_SUCCESS;
        }
    }

    double duration_s = (double)duration_ms / 1000.0;
    size_t frames = (size_t)lrint(duration_s * (double)BSS_SAMPLE_RATE);
    if (frames == 0) {
        frames = 1;
    }

    if (frames > SIZE_MAX / sizeof(int16_t)) {
        fprintf(stderr, "_BSS: duration too long\n");
        return EXIT_FAILURE;
    }

    int16_t *buffer = calloc(frames, sizeof(int16_t));
    if (!buffer) {
        perror("_BSS: calloc");
        return EXIT_FAILURE;
    }

#if BUDOSTACK_HAVE_ALSA
    long remaining = loop_count;
    do {
        bss_render_note(&engine, buffer, frames, duration_s);
        if (play_pcm(buffer, frames, BSS_SAMPLE_RATE) != 0) {
            free(buffer);
            return EXIT_FAILURE;
        }
        if (remaining > 0)
            --remaining;
    } while (remaining != 0);
#else
    fprintf(stderr, "_BSS: ALSA support not available in this build.\n");
    free(buffer);
    return EXIT_FAILURE;
#endif

    free(buffer);
    return EXIT_SUCCESS;
}
