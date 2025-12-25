#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100.0
#define SIGNAL_STATE_PATH "/tmp/budostack_signal.state"
#define SIGNAL_PLAY_PATH "/tmp/budostack_signal.play"
#define SIGNAL_MIN_CHANNEL 1
#define SIGNAL_MAX_CHANNEL 32

enum waveform_type { W_SINE, W_SQUARE, W_TRIANGLE, W_SAWTOOTH, W_NOISE };

typedef struct {
    int active;
    enum waveform_type wave;
    char note[8];
    double freq;
    long duration_ms;
    long attack_ms;
    long decay_ms;
    long sustain_ms;
    long release_ms;
    double lowpass_hz;
    double highpass_hz;
} NoteEntry;

typedef struct {
    double phase;
    double lowpass_state;
    double highpass_state;
    double highpass_prev;
    unsigned int noise_seed;
} NoteState;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -<cmd> -<waveform> <note> <duration_ms> [channel] "
        "[attack_ms] [decay_ms] [sustain_ms] [release_ms] [lowpass_hz] [highpass_hz]\n"
        "  cmd       : enter, play (plays already entered notes, does not require below args)\n"
        "  waveforms : sine, square, triangle, sawtooth, noise\n"
        "  note      : standard concert pitch notes (e.g. c2, c3, c4, d4, e4)\n"
        "  duration  : milliseconds (e.g. 500 = 500ms)\n"
        "  format    : raw, text, wav\n"
        "  channel   : (optional) 1-32 (to enable parallel sounds, default 1)\n"
        "  attack    : (optional) in milliseconds\n"
        "  decay     : (optional) in milliseconds\n"
        "  sustain   : (optional) in milliseconds\n"
        "  release   : (optional) in milliseconds\n"
        "  lowpass   : (optional) in Hz\n"
        "  highpass  : (optional) in Hz\n"
        "Examples:\n"
        "  %s -enter -sine c4 500 1 20 30 300 150 1000 200\n"
        "  %s -enter -square e4 250 2\n"
        "  %s -play wav > chord.wav\n",
        prog, prog, prog, prog);
    exit(EXIT_FAILURE);
}

static const char *strip_dash(const char *arg) {
    if (!arg) {
        return NULL;
    }
    if (arg[0] == '-') {
        return arg + 1;
    }
    return arg;
}

static int parse_long(const char *arg, long *out, long min, long max) {
    char *endptr = NULL;
    const char *text = strip_dash(arg);
    if (!text || *text == '\0') {
        return -1;
    }
    errno = 0;
    long value = strtol(text, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        return -1;
    }
    if (value < min || value > max) {
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_double(const char *arg, double *out, double min, double max) {
    char *endptr = NULL;
    const char *text = strip_dash(arg);
    if (!text || *text == '\0') {
        return -1;
    }
    errno = 0;
    double value = strtod(text, &endptr);
    if (errno != 0 || !endptr || *endptr != '\0') {
        return -1;
    }
    if (value < min || value > max) {
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_waveform(const char *arg, enum waveform_type *out) {
    const char *w = strip_dash(arg);
    if (!w) {
        return -1;
    }
    if (!strcmp(w, "sine")) {
        *out = W_SINE;
    } else if (!strcmp(w, "square")) {
        *out = W_SQUARE;
    } else if (!strcmp(w, "triangle")) {
        *out = W_TRIANGLE;
    } else if (!strcmp(w, "sawtooth")) {
        *out = W_SAWTOOTH;
    } else if (!strcmp(w, "noise")) {
        *out = W_NOISE;
    } else {
        return -1;
    }
    return 0;
}

static const char *waveform_name(enum waveform_type wave) {
    switch (wave) {
        case W_SINE:
            return "sine";
        case W_SQUARE:
            return "square";
        case W_TRIANGLE:
            return "triangle";
        case W_SAWTOOTH:
            return "sawtooth";
        case W_NOISE:
            return "noise";
    }
    return "sine";
}

static int note_to_frequency(const char *note, double *freq) {
    char buffer[8];
    size_t len = 0;
    while (note[len] == '.') {
        len++;
    }
    if (note[len] == '\0') {
        return -1;
    }
    snprintf(buffer, sizeof(buffer), "%s", note + len);

    char letter = buffer[0];
    if (letter == '\0') {
        return -1;
    }
    int semitone;
    switch (letter) {
        case 'C': case 'c': semitone = 0; break;
        case 'D': case 'd': semitone = 2; break;
        case 'E': case 'e': semitone = 4; break;
        case 'F': case 'f': semitone = 5; break;
        case 'G': case 'g': semitone = 7; break;
        case 'A': case 'a': semitone = 9; break;
        case 'B': case 'b': semitone = 11; break;
        default: return -1;
    }

    size_t pos = 1;
    if (buffer[pos] == '#' || buffer[pos] == 'b') {
        if (buffer[pos] == '#') {
            semitone += 1;
        } else {
            semitone -= 1;
        }
        pos++;
    }

    if (buffer[pos] == '\0') {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    long octave = strtol(buffer + pos, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        return -1;
    }

    int midi = (int)((octave + 1) * 12 + semitone);
    double value = 440.0 * pow(2.0, (midi - 69) / 12.0);
    if (value <= 0.0 || !isfinite(value)) {
        return -1;
    }

    *freq = value;
    return 0;
}

static void load_state(NoteEntry entries[], size_t count) {
    FILE *fp = fopen(SIGNAL_STATE_PATH, "r");
    if (!fp) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *cursor = line;
        char *fields[10];
        int idx = 0;
        while (idx < 10) {
            fields[idx++] = cursor;
            char *sep = strchr(cursor, '|');
            if (!sep) {
                break;
            }
            *sep = '\0';
            cursor = sep + 1;
        }
        if (idx < 10) {
            continue;
        }

        char *endptr = NULL;
        errno = 0;
        long channel = strtol(fields[0], &endptr, 10);
        if (errno != 0 || !endptr || *endptr != '\0') {
            continue;
        }
        if (channel < SIGNAL_MIN_CHANNEL || channel > (long)count) {
            continue;
        }
        NoteEntry *entry = &entries[channel - 1];
        entry->active = 1;
        if (parse_waveform(fields[1], &entry->wave) != 0) {
            entry->active = 0;
            continue;
        }
        snprintf(entry->note, sizeof(entry->note), "%s", fields[2]);
        entry->duration_ms = strtol(fields[3], NULL, 10);
        entry->attack_ms = strtol(fields[4], NULL, 10);
        entry->decay_ms = strtol(fields[5], NULL, 10);
        entry->sustain_ms = strtol(fields[6], NULL, 10);
        entry->release_ms = strtol(fields[7], NULL, 10);
        entry->lowpass_hz = strtod(fields[8], NULL);
        entry->highpass_hz = strtod(fields[9], NULL);
        if (note_to_frequency(entry->note, &entry->freq) != 0) {
            entry->active = 0;
            continue;
        }
    }

    fclose(fp);
}

static int save_state(const NoteEntry entries[], size_t count) {
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", SIGNAL_STATE_PATH);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        perror("signal: fopen");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        const NoteEntry *entry = &entries[i];
        if (!entry->active) {
            continue;
        }
        fprintf(fp, "%zu|%s|%s|%ld|%ld|%ld|%ld|%ld|%.3f|%.3f\n",
            i + 1,
            waveform_name(entry->wave),
            entry->note,
            entry->duration_ms,
            entry->attack_ms,
            entry->decay_ms,
            entry->sustain_ms,
            entry->release_ms,
            entry->lowpass_hz,
            entry->highpass_hz);
    }

    if (fclose(fp) != 0) {
        perror("signal: fclose");
        return -1;
    }

    if (rename(tmp_path, SIGNAL_STATE_PATH) != 0) {
        perror("signal: rename");
        return -1;
    }

    return 0;
}

static void clear_state(void) {
    unlink(SIGNAL_STATE_PATH);
}

static int write_wav_header(FILE *f, uint32_t total_samples) {
    uint32_t data_bytes = total_samples * 2;

    fwrite("RIFF", 1, 4, f);
    uint32_t chunk_size = 36 + data_bytes;
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, 4, 1, f);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 2, 1, f);
    uint16_t num_channels = 1;
    fwrite(&num_channels, 2, 1, f);
    uint32_t sample_rate = (uint32_t)SAMPLE_RATE;
    fwrite(&sample_rate, 4, 1, f);
    uint32_t byte_rate = sample_rate * num_channels * 2;
    fwrite(&byte_rate, 4, 1, f);
    uint16_t block_align = num_channels * 2;
    fwrite(&block_align, 2, 1, f);
    uint16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);

    return 0;
}

static void apply_adsr(uint64_t sample, uint64_t attack, uint64_t decay, uint64_t sustain, uint64_t release,
    uint64_t total, double *gain) {
    if (total == 0) {
        *gain = 0.0;
        return;
    }
    if (sample < attack) {
        *gain = (attack > 0) ? ((double)sample / (double)attack) : 1.0;
        return;
    }

    sample -= attack;
    if (sample < decay) {
        double t = (decay > 0) ? ((double)sample / (double)decay) : 1.0;
        *gain = 1.0 - t * 0.3;
        return;
    }

    sample -= decay;
    if (sample < sustain) {
        *gain = 0.7;
        return;
    }

    sample -= sustain;
    if (sample < release) {
        double t = (release > 0) ? ((double)sample / (double)release) : 1.0;
        *gain = 0.7 * (1.0 - t);
        return;
    }

    *gain = 0.0;
}

static void clamp_adsr(uint64_t total, uint64_t *attack, uint64_t *decay,
    uint64_t *sustain, uint64_t *release, int sustain_specified) {
    uint64_t sum = *attack + *decay + *sustain + *release;
    if (!sustain_specified) {
        if (total > *attack + *decay + *release) {
            *sustain = total - *attack - *decay - *release;
        } else {
            *sustain = 0;
        }
        sum = *attack + *decay + *sustain + *release;
    }

    if (sum < total && sustain_specified) {
        *sustain += total - sum;
        sum = total;
    }

    if (sum <= total) {
        return;
    }

    uint64_t overflow = sum - total;
    if (*sustain >= overflow) {
        *sustain -= overflow;
        return;
    }
    overflow -= *sustain;
    *sustain = 0;

    if (*release >= overflow) {
        *release -= overflow;
        return;
    }
    overflow -= *release;
    *release = 0;

    if (*decay >= overflow) {
        *decay -= overflow;
        return;
    }
    overflow -= *decay;
    *decay = 0;

    if (*attack >= overflow) {
        *attack -= overflow;
    } else {
        *attack = 0;
    }
}

static double apply_filters(double sample, NoteState *state, double lowpass_hz, double highpass_hz) {
    double result = sample;
    double dt = 1.0 / SAMPLE_RATE;

    if (lowpass_hz > 0.0) {
        double rc = 1.0 / (2.0 * M_PI * lowpass_hz);
        double alpha = dt / (rc + dt);
        state->lowpass_state += alpha * (result - state->lowpass_state);
        result = state->lowpass_state;
    }

    if (highpass_hz > 0.0) {
        double rc = 1.0 / (2.0 * M_PI * highpass_hz);
        double alpha = rc / (rc + dt);
        double next = alpha * (state->highpass_state + result - state->highpass_prev);
        state->highpass_prev = result;
        state->highpass_state = next;
        result = next;
    }

    return result;
}

static double next_noise(NoteState *state) {
    state->noise_seed = state->noise_seed * 1664525u + 1013904223u;
    return ((state->noise_seed >> 8) / (double)UINT32_MAX) * 2.0 - 1.0;
}

static void enforce_play_gap(double duration_s) {
    FILE *fp = fopen(SIGNAL_PLAY_PATH, "r");
    if (fp) {
        double end_time = 0.0;
        if (fscanf(fp, "%lf", &end_time) == 1) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            double current = now.tv_sec + now.tv_nsec / 1e9;
            if (current < end_time) {
                double delay = end_time - current;
                struct timespec sleep_time;
                sleep_time.tv_sec = (time_t)delay;
                sleep_time.tv_nsec = (long)((delay - sleep_time.tv_sec) * 1e9);
                nanosleep(&sleep_time, NULL);
            }
        }
        fclose(fp);
    }

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    double next_end = now.tv_sec + now.tv_nsec / 1e9 + duration_s;
    fp = fopen(SIGNAL_PLAY_PATH, "w");
    if (fp) {
        fprintf(fp, "%.6f", next_end);
        fclose(fp);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }

    const char *cmd = strip_dash(argv[1]);
    if (!cmd) {
        usage(argv[0]);
    }

    if (!strcmp(cmd, "play")) {
        enum { F_RAW, F_TEXT, F_WAV, F_PLAY } mode = F_PLAY;
        if (argc >= 3) {
            const char *fmt = argv[2];
            if (!strcmp(fmt, "raw")) {
                mode = F_RAW;
            } else if (!strcmp(fmt, "text")) {
                mode = F_TEXT;
            } else if (!strcmp(fmt, "wav")) {
                mode = F_WAV;
            } else {
                fprintf(stderr, "Unknown format: %s\n", fmt);
                usage(argv[0]);
            }
        }

        NoteEntry entries[SIGNAL_MAX_CHANNEL];
        memset(entries, 0, sizeof(entries));
        load_state(entries, SIGNAL_MAX_CHANNEL);

        uint64_t max_samples = 0;
        size_t active_count = 0;
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            if (!entries[i].active) {
                continue;
            }
            if (entries[i].duration_ms <= 0) {
                entries[i].active = 0;
                continue;
            }
            uint64_t samples = (uint64_t)((entries[i].duration_ms / 1000.0) * SAMPLE_RATE);
            if (samples == 0) {
                samples = 1;
            }
            if (samples > max_samples) {
                max_samples = samples;
            }
            active_count++;
        }

        if (active_count == 0) {
            fprintf(stderr, "signal: no notes entered.\n");
            return EXIT_FAILURE;
        }

        double total_duration_s = max_samples / SAMPLE_RATE;
        enforce_play_gap(total_duration_s);

        FILE *out = stdout;
        if (mode == F_PLAY) {
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Failed to fork for playback: %s\n", strerror(errno));
                return EXIT_FAILURE;
            }
            if (pid > 0) {
                clear_state();
                return 0;
            }
            out = popen("aplay -q -f S16_LE -c1 -r44100", "w");
            if (!out) {
                fprintf(stderr, "Failed to launch aplay: %s\n", strerror(errno));
                return EXIT_FAILURE;
            }
        }

        if (mode == F_WAV) {
            write_wav_header(out, (uint32_t)max_samples);
        }

        if (mode == F_RAW && isatty(STDOUT_FILENO)) {
            fprintf(stderr, "Warning: streaming binary floats to terminal. Redirect to file.\n");
        }

        NoteState states[SIGNAL_MAX_CHANNEL];
        memset(states, 0, sizeof(states));
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            if (!entries[i].active) {
                continue;
            }
            states[i].phase = (2.0 * M_PI / 32.0) * (double)i;
            states[i].noise_seed = (unsigned)time(NULL) ^ (unsigned)(i + 1);
        }

        for (uint64_t n = 0; n < max_samples; ++n) {
            double mixed = 0.0;
            for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                if (!entries[i].active) {
                    continue;
                }
                uint64_t total_samples = (uint64_t)((entries[i].duration_ms / 1000.0) * SAMPLE_RATE);
                if (total_samples == 0) {
                    total_samples = 1;
                }
                if (n >= total_samples) {
                    continue;
                }

                uint64_t attack = (uint64_t)((entries[i].attack_ms / 1000.0) * SAMPLE_RATE);
                uint64_t decay = (uint64_t)((entries[i].decay_ms / 1000.0) * SAMPLE_RATE);
                uint64_t sustain = (uint64_t)((entries[i].sustain_ms / 1000.0) * SAMPLE_RATE);
                uint64_t release = (uint64_t)((entries[i].release_ms / 1000.0) * SAMPLE_RATE);

                int sustain_specified = entries[i].sustain_ms > 0;
                clamp_adsr(total_samples, &attack, &decay, &sustain, &release, sustain_specified);

                double gain = 1.0;
                apply_adsr(n, attack, decay, sustain, release, total_samples, &gain);

                double sample = 0.0;
                double phase = states[i].phase;
                const double two_pi = 2.0 * M_PI;
                switch (entries[i].wave) {
                    case W_SINE:
                        sample = sin(phase);
                        break;
                    case W_SQUARE:
                        sample = (phase < M_PI) ? 1.0 : -1.0;
                        break;
                    case W_TRIANGLE: {
                        double t = phase / two_pi;
                        double saw = 2.0 * (t - floor(t + 0.5));
                        sample = 2.0 * fabs(saw) - 1.0;
                        break;
                    }
                    case W_SAWTOOTH: {
                        double t = phase / two_pi;
                        sample = 2.0 * (t - floor(t + 0.5));
                        break;
                    }
                    case W_NOISE:
                        sample = next_noise(&states[i]);
                        break;
                }

                if (entries[i].wave != W_NOISE) {
                    double phase_inc = two_pi * entries[i].freq / SAMPLE_RATE;
                    phase += phase_inc;
                    if (phase >= two_pi) {
                        phase -= two_pi;
                    }
                    states[i].phase = phase;
                }

                sample *= gain;
                sample = apply_filters(sample, &states[i], entries[i].lowpass_hz, entries[i].highpass_hz);
                mixed += sample;
            }

            if (active_count > 0) {
                mixed /= (double)active_count;
            }
            if (mixed > 1.0) {
                mixed = 1.0;
            } else if (mixed < -1.0) {
                mixed = -1.0;
            }

            if (mode == F_RAW) {
                float fval = (float)mixed;
                if (fwrite(&fval, sizeof(fval), 1, out) != 1) {
                    break;
                }
            } else if (mode == F_TEXT) {
                if (fprintf(out, "%f\n", mixed) < 0) {
                    break;
                }
            } else {
                int16_t s16 = (int16_t)(mixed * 32767);
                if (fwrite(&s16, sizeof(s16), 1, out) != 1) {
                    break;
                }
            }
        }

        if (mode == F_PLAY) {
            pclose(out);
        }

        clear_state();
        return 0;
    }

    if (!strcmp(cmd, "enter")) {
        if (argc < 5) {
            usage(argv[0]);
        }

        enum waveform_type wave;
        if (parse_waveform(argv[2], &wave) != 0) {
            fprintf(stderr, "Unknown waveform: %s\n", argv[2]);
            usage(argv[0]);
        }

        const char *note = argv[3];
        double freq = 0.0;
        if (note_to_frequency(note, &freq) != 0) {
            fprintf(stderr, "Invalid note: %s\n", note);
            usage(argv[0]);
        }

        long duration_ms = 0;
        if (parse_long(argv[4], &duration_ms, 1, 600000) != 0) {
            fprintf(stderr, "Duration must be a positive number of milliseconds.\n");
            usage(argv[0]);
        }

        long channel = 1;
        int arg_index = 5;
        if (argc > arg_index) {
            if (parse_long(argv[arg_index], &channel, SIGNAL_MIN_CHANNEL, SIGNAL_MAX_CHANNEL) == 0) {
                arg_index++;
            }
        }

        long attack_ms = 0;
        long decay_ms = 0;
        long sustain_ms = 0;
        long release_ms = 0;
        double lowpass_hz = 0.0;
        double highpass_hz = 0.0;

        if (argc > arg_index && parse_long(argv[arg_index], &attack_ms, 0, 600000) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &decay_ms, 0, 600000) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &sustain_ms, 0, 600000) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &release_ms, 0, 600000) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_double(argv[arg_index], &lowpass_hz, 0.0, 20000.0) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_double(argv[arg_index], &highpass_hz, 0.0, 20000.0) == 0) {
            arg_index++;
        }

        NoteEntry entries[SIGNAL_MAX_CHANNEL];
        memset(entries, 0, sizeof(entries));
        load_state(entries, SIGNAL_MAX_CHANNEL);

        NoteEntry *entry = &entries[channel - 1];
        entry->active = 1;
        entry->wave = wave;
        snprintf(entry->note, sizeof(entry->note), "%s", note);
        entry->freq = freq;
        entry->duration_ms = duration_ms;
        entry->attack_ms = attack_ms;
        entry->decay_ms = decay_ms;
        entry->sustain_ms = sustain_ms;
        entry->release_ms = release_ms;
        entry->lowpass_hz = lowpass_hz;
        entry->highpass_hz = highpass_hz;

        if (save_state(entries, SIGNAL_MAX_CHANNEL) != 0) {
            return EXIT_FAILURE;
        }

        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
}
