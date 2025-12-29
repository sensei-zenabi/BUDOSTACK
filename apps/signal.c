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
#define SIGNAL_CONTROL_PATH "/tmp/budostack_signal.control"
#define SIGNAL_MIN_CHANNEL 1
#define SIGNAL_MAX_CHANNEL 32

enum waveform_type { W_SINE, W_SQUARE, W_TRIANGLE, W_SAWTOOTH, W_NOISE };

typedef struct {
    int active;
    enum waveform_type wave;
    char note[8];
    double freq;
    long duration_ms;
    int volume;
    int attack_pct;
    int decay_pct;
    int sustain_pct;
    int release_pct;
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

typedef struct {
    NoteEntry *notes;
    size_t count;
    size_t cap;
} NoteSequence;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s -<cmd> -<waveform> <note> <duration_ms> <volume> [channel]\n"
        "     [attack_pct] [decay_pct] [sustain_pct] [release_pct]\n"
        "     [lowpass_hz] [highpass_hz]\n"
        "  cmd       : enter, play, loop <count> (plays entered notes count\n"
        "              times), stop (stops all notes)\n"
        "  waveforms : sine, square, triangle, sawtooth, noise\n"
        "  note      : standard concert pitch notes (e.g. c2, c3, c4, d4,\n"
        "              e4)\n"
        "  duration  : milliseconds (e.g. 500 = 500ms)\n"
        "  volume    : 0-100 (0 silent, 100 max volume)\n"
        "  channel   : (optional) 1-32 (parallel sounds, default 1)\n"
        "  attack    : (optional) 0-100 percent of duration\n"
        "  decay     : (optional) 0-100 percent of duration\n"
        "  sustain   : (optional) 0-100 percent of duration\n"
        "  release   : (optional) 0-100 percent of duration\n"
        "  adsr      : percentages must total 100\n"
        "  lowpass   : (optional) in Hz\n"
        "  highpass  : (optional) in Hz\n"
        "Notes entered on the same channel play sequentially in the order\n"
        "entered.\n"
        "Examples:\n"
        "  %s -enter -sine c4 500 100 1 10 20 60 10 1000 200\n"
        "  %s -enter -square e4 250 80\n"
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

static int normalize_adsr_percent(long duration_ms, long attack_value, long decay_value,
    long sustain_value, long release_value, int values_are_ms, int *attack_pct,
    int *decay_pct, int *sustain_pct, int *release_pct) {
    if (duration_ms <= 0) {
        return -1;
    }
    if (attack_value < 0 || decay_value < 0 || sustain_value < 0 || release_value < 0) {
        return -1;
    }

    if (values_are_ms) {
        long total_ms = attack_value + decay_value + sustain_value + release_value;
        if (total_ms == 0) {
            *attack_pct = 0;
            *decay_pct = 0;
            *sustain_pct = 100;
            *release_pct = 0;
            return 0;
        }
        if (total_ms > duration_ms) {
            return -1;
        }
        double scale = 100.0 / (double)duration_ms;
        *attack_pct = (int)lround(attack_value * scale);
        *decay_pct = (int)lround(decay_value * scale);
        *sustain_pct = (int)lround(sustain_value * scale);
        *release_pct = (int)lround(release_value * scale);
    } else {
        if (attack_value > 100 || decay_value > 100 || sustain_value > 100 || release_value > 100) {
            return -1;
        }
        if ((attack_value + decay_value + sustain_value + release_value) != 100) {
            return -1;
        }
        *attack_pct = (int)attack_value;
        *decay_pct = (int)decay_value;
        *sustain_pct = (int)sustain_value;
        *release_pct = (int)release_value;
    }

    int total_pct = *attack_pct + *decay_pct + *sustain_pct + *release_pct;
    if (total_pct != 100) {
        int adjust = 100 - total_pct;
        *sustain_pct += adjust;
    }
    if (*attack_pct < 0 || *decay_pct < 0 || *sustain_pct < 0 || *release_pct < 0) {
        return -1;
    }
    if (*attack_pct > 100 || *decay_pct > 100 || *sustain_pct > 100 || *release_pct > 100) {
        return -1;
    }
    if ((*attack_pct + *decay_pct + *sustain_pct + *release_pct) != 100) {
        return -1;
    }
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

static void init_sequence(NoteSequence *sequence) {
    sequence->notes = NULL;
    sequence->count = 0;
    sequence->cap = 0;
}

static void free_sequence(NoteSequence *sequence) {
    free(sequence->notes);
    sequence->notes = NULL;
    sequence->count = 0;
    sequence->cap = 0;
}

static int append_note(NoteSequence *sequence, const NoteEntry *note) {
    if (sequence->count == sequence->cap) {
        size_t next_cap = sequence->cap == 0 ? 4 : sequence->cap * 2;
        NoteEntry *next_notes = realloc(sequence->notes, next_cap * sizeof(*next_notes));
        if (!next_notes) {
            return -1;
        }
        sequence->notes = next_notes;
        sequence->cap = next_cap;
    }
    sequence->notes[sequence->count++] = *note;
    return 0;
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

static void load_state(NoteSequence sequences[], size_t count) {
    FILE *fp = fopen(SIGNAL_STATE_PATH, "r");
    if (!fp) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *cursor = line;
        char *fields[11];
        int field_count = 0;
        while (field_count < 11) {
            fields[field_count++] = cursor;
            char *sep = strchr(cursor, '|');
            if (!sep) {
                break;
            }
            *sep = '\0';
            cursor = sep + 1;
        }
        if (field_count < 10) {
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
        NoteEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.active = 1;
        if (parse_waveform(fields[1], &entry.wave) != 0) {
            continue;
        }
        snprintf(entry.note, sizeof(entry.note), "%s", fields[2]);
        entry.duration_ms = strtol(fields[3], NULL, 10);
        entry.volume = 100;
        long attack_value = 0;
        long decay_value = 0;
        long sustain_value = 0;
        long release_value = 0;
        int values_are_ms = 1;
        if (field_count >= 11) {
            long volume = strtol(fields[4], NULL, 10);
            if (volume < 0 || volume > 100) {
                continue;
            }
            entry.volume = (int)volume;
            attack_value = strtol(fields[5], NULL, 10);
            decay_value = strtol(fields[6], NULL, 10);
            sustain_value = strtol(fields[7], NULL, 10);
            release_value = strtol(fields[8], NULL, 10);
            entry.lowpass_hz = strtod(fields[9], NULL);
            entry.highpass_hz = strtod(fields[10], NULL);
            if (attack_value >= 0 && decay_value >= 0 && sustain_value >= 0 && release_value >= 0 &&
                attack_value <= 100 && decay_value <= 100 && sustain_value <= 100 &&
                release_value <= 100 &&
                (attack_value + decay_value + sustain_value + release_value) == 100) {
                values_are_ms = 0;
            }
        } else {
            attack_value = strtol(fields[4], NULL, 10);
            decay_value = strtol(fields[5], NULL, 10);
            sustain_value = strtol(fields[6], NULL, 10);
            release_value = strtol(fields[7], NULL, 10);
            entry.lowpass_hz = strtod(fields[8], NULL);
            entry.highpass_hz = strtod(fields[9], NULL);
        }
        if (normalize_adsr_percent(entry.duration_ms, attack_value, decay_value, sustain_value,
                release_value, values_are_ms, &entry.attack_pct, &entry.decay_pct,
                &entry.sustain_pct, &entry.release_pct) != 0) {
            continue;
        }
        if (note_to_frequency(entry.note, &entry.freq) != 0) {
            continue;
        }
        if (append_note(&sequences[channel - 1], &entry) != 0) {
            perror("signal: realloc");
            fclose(fp);
            return;
        }
    }

    fclose(fp);
}

static int save_state(const NoteSequence sequences[], size_t count) {
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", SIGNAL_STATE_PATH);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        perror("signal: fopen");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        const NoteSequence *sequence = &sequences[i];
        for (size_t j = 0; j < sequence->count; ++j) {
            const NoteEntry *entry = &sequence->notes[j];
            if (!entry->active) {
                continue;
            }
            fprintf(fp, "%zu|%s|%s|%ld|%d|%d|%d|%d|%d|%.3f|%.3f\n",
                i + 1,
                waveform_name(entry->wave),
                entry->note,
                entry->duration_ms,
                entry->volume,
                entry->attack_pct,
                entry->decay_pct,
                entry->sustain_pct,
                entry->release_pct,
                entry->lowpass_hz,
                entry->highpass_hz);
        }
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

static void set_control_value(const char *value) {
    FILE *fp = fopen(SIGNAL_CONTROL_PATH, "w");
    if (!fp) {
        return;
    }
    fprintf(fp, "%s", value);
    fclose(fp);
}

static int get_control_value(char *buffer, size_t size) {
    FILE *fp = fopen(SIGNAL_CONTROL_PATH, "r");
    if (!fp) {
        return -1;
    }
    if (!fgets(buffer, (int)size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 0;
}

static void make_control_token(char *buffer, size_t size) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    snprintf(buffer, size, "%ld-%ld-%ld", (long)getpid(), now.tv_sec, now.tv_nsec);
}

static int should_stop_playback(const char *token) {
    char current[64];
    if (get_control_value(current, sizeof(current)) != 0) {
        return 0;
    }
    if (strcmp(current, token) != 0) {
        return 1;
    }
    return 0;
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

static uint64_t note_samples(const NoteEntry *entry) {
    if (!entry->active || entry->duration_ms <= 0) {
        return 0;
    }
    uint64_t samples = (uint64_t)((entry->duration_ms / 1000.0) * SAMPLE_RATE);
    return samples == 0 ? 1 : samples;
}

static double reserve_play_slot(double duration_s) {
    FILE *fp = fopen(SIGNAL_PLAY_PATH, "r");
    double delay = 0.0;
    if (fp) {
        double end_time = 0.0;
        if (fscanf(fp, "%lf", &end_time) == 1) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            double current = now.tv_sec + now.tv_nsec / 1e9;
            if (current < end_time) {
                delay = end_time - current;
            }
        }
        fclose(fp);
    }

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    double start_time = now.tv_sec + now.tv_nsec / 1e9 + delay;
    double next_end = start_time + duration_s;
    fp = fopen(SIGNAL_PLAY_PATH, "w");
    if (fp) {
        fprintf(fp, "%.6f", next_end);
        fclose(fp);
    }
    return delay;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }

    const char *cmd = strip_dash(argv[1]);
    if (!cmd) {
        usage(argv[0]);
    }

    if (!strcmp(cmd, "play") || !strcmp(cmd, "loop")) {
        enum { F_RAW, F_TEXT, F_WAV, F_PLAY } mode = F_PLAY;
        int loop_mode = strcmp(cmd, "loop") == 0;
        long loop_count = 0;
        if (loop_mode) {
            if (argc < 3) {
                fprintf(stderr, "signal: loop requires a count.\n");
                usage(argv[0]);
            }
            if (parse_long(argv[2], &loop_count, 1, 1000000) != 0) {
                fprintf(stderr, "signal: loop count must be a positive integer.\n");
                usage(argv[0]);
            }
            if (argc > 3) {
                fprintf(stderr, "signal: loop only accepts a count argument.\n");
                usage(argv[0]);
            }
        }
        if (!loop_mode && argc >= 3) {
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

        NoteSequence sequences[SIGNAL_MAX_CHANNEL];
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            init_sequence(&sequences[i]);
        }
        load_state(sequences, SIGNAL_MAX_CHANNEL);

        uint64_t max_samples = 0;
        size_t active_channels = 0;
        uint64_t channel_totals[SIGNAL_MAX_CHANNEL];
        memset(channel_totals, 0, sizeof(channel_totals));

        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            if (sequences[i].count == 0) {
                continue;
            }
            uint64_t channel_samples = 0;
            for (size_t j = 0; j < sequences[i].count; ++j) {
                NoteEntry *entry = &sequences[i].notes[j];
                uint64_t samples = note_samples(entry);
                if (samples == 0) {
                    entry->active = 0;
                    continue;
                }
                channel_samples += samples;
            }
            channel_totals[i] = channel_samples;
            if (channel_samples > 0) {
                active_channels++;
                if (channel_samples > max_samples) {
                    max_samples = channel_samples;
                }
            }
        }

        if (active_channels == 0 || max_samples == 0) {
            fprintf(stderr, "signal: no notes entered.\n");
            for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                free_sequence(&sequences[i]);
            }
            return EXIT_FAILURE;
        }

        char control_token[64];
        make_control_token(control_token, sizeof(control_token));
        set_control_value(control_token);

        double total_duration_s = max_samples / SAMPLE_RATE;
        double delay_s = reserve_play_slot(total_duration_s);

        FILE *out = stdout;
        if (mode == F_PLAY) {
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Failed to fork for playback: %s\n", strerror(errno));
                for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                    free_sequence(&sequences[i]);
                }
                return EXIT_FAILURE;
            }
            if (pid > 0) {
                if (!loop_mode) {
                    clear_state();
                }
                for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                    free_sequence(&sequences[i]);
                }
                return 0;
            }
            if (delay_s > 0.0) {
                struct timespec sleep_time;
                sleep_time.tv_sec = (time_t)delay_s;
                sleep_time.tv_nsec = (long)((delay_s - sleep_time.tv_sec) * 1e9);
                nanosleep(&sleep_time, NULL);
            }
            out = popen("aplay -q -f S16_LE -c1 -r44100", "w");
            if (!out) {
                fprintf(stderr, "Failed to launch aplay: %s\n", strerror(errno));
                for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                    free_sequence(&sequences[i]);
                }
                return EXIT_FAILURE;
            }
        }
        if (mode != F_PLAY && delay_s > 0.0) {
            struct timespec sleep_time;
            sleep_time.tv_sec = (time_t)delay_s;
            sleep_time.tv_nsec = (long)((delay_s - sleep_time.tv_sec) * 1e9);
            nanosleep(&sleep_time, NULL);
        }

        if (mode == F_WAV) {
            write_wav_header(out, (uint32_t)max_samples);
        }

        if (mode == F_RAW && isatty(STDOUT_FILENO)) {
            fprintf(stderr, "Warning: streaming binary floats to terminal. Redirect to file.\n");
        }

        NoteState states[SIGNAL_MAX_CHANNEL];
        memset(states, 0, sizeof(states));
        size_t note_index[SIGNAL_MAX_CHANNEL];
        uint64_t note_sample_pos[SIGNAL_MAX_CHANNEL];
        uint64_t note_total_samples[SIGNAL_MAX_CHANNEL];
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            note_index[i] = 0;
            note_sample_pos[i] = 0;
            note_total_samples[i] = 0;
            if (sequences[i].count > 0) {
                NoteEntry *entry = &sequences[i].notes[0];
                note_total_samples[i] = note_samples(entry);
                states[i].phase = (2.0 * M_PI / 32.0) * (double)i;
                states[i].noise_seed = (unsigned)time(NULL) ^ (unsigned)(i + 1);
            }
        }

        int stop_requested = 0;
        long loops_remaining = loop_mode ? loop_count : 1;
        while (loops_remaining > 0 && !stop_requested) {
            for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                note_index[i] = 0;
                note_sample_pos[i] = 0;
                note_total_samples[i] = 0;
                if (sequences[i].count > 0) {
                    NoteEntry *entry = &sequences[i].notes[0];
                    note_total_samples[i] = note_samples(entry);
                    states[i].phase = (2.0 * M_PI / 32.0) * (double)i;
                    states[i].lowpass_state = 0.0;
                    states[i].highpass_state = 0.0;
                    states[i].highpass_prev = 0.0;
                    states[i].noise_seed = (unsigned)time(NULL) ^ (unsigned)(i + 1);
                }
            }

            for (uint64_t n = 0; n < max_samples; ++n) {
                if ((n & 1023u) == 0u && should_stop_playback(control_token)) {
                    stop_requested = 1;
                    break;
                }

                double mixed = 0.0;
                size_t active_mix = 0;
                for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                    if (sequences[i].count == 0) {
                        continue;
                    }
                    if (note_index[i] >= sequences[i].count) {
                        continue;
                    }

                    while (note_index[i] < sequences[i].count &&
                        (note_total_samples[i] == 0 || note_sample_pos[i] >= note_total_samples[i])) {
                        note_index[i]++;
                        note_sample_pos[i] = 0;
                        if (note_index[i] < sequences[i].count) {
                            NoteEntry *next = &sequences[i].notes[note_index[i]];
                            note_total_samples[i] = note_samples(next);
                            states[i].phase = (2.0 * M_PI / 32.0) * (double)i;
                            states[i].lowpass_state = 0.0;
                            states[i].highpass_state = 0.0;
                            states[i].highpass_prev = 0.0;
                            states[i].noise_seed = (unsigned)time(NULL) ^ (unsigned)(i + 1 + note_index[i]);
                        }
                    }

                    if (note_index[i] >= sequences[i].count) {
                        continue;
                    }

                    NoteEntry *entry = &sequences[i].notes[note_index[i]];
                    uint64_t total_samples = note_total_samples[i];

                    uint64_t attack = (uint64_t)(total_samples * (entry->attack_pct / 100.0));
                    uint64_t decay = (uint64_t)(total_samples * (entry->decay_pct / 100.0));
                    uint64_t sustain = (uint64_t)(total_samples * (entry->sustain_pct / 100.0));
                    uint64_t release = (uint64_t)(total_samples * (entry->release_pct / 100.0));

                    int sustain_specified = 1;
                    clamp_adsr(total_samples, &attack, &decay, &sustain, &release, sustain_specified);

                    double gain = 1.0;
                    apply_adsr(note_sample_pos[i], attack, decay, sustain, release, total_samples, &gain);

                    double sample = 0.0;
                    double phase = states[i].phase;
                    const double two_pi = 2.0 * M_PI;
                    switch (entry->wave) {
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

                    if (entry->wave != W_NOISE) {
                        double phase_inc = two_pi * entry->freq / SAMPLE_RATE;
                        phase += phase_inc;
                        if (phase >= two_pi) {
                            phase -= two_pi;
                        }
                        states[i].phase = phase;
                    }

                    sample *= gain;
                    sample *= entry->volume / 100.0;
                    sample = apply_filters(sample, &states[i], entry->lowpass_hz, entry->highpass_hz);
                    mixed += sample;
                    active_mix++;
                    note_sample_pos[i]++;
                }

                if (active_mix > 0) {
                    mixed /= (double)active_mix;
                }
                if (mixed > 1.0) {
                    mixed = 1.0;
                } else if (mixed < -1.0) {
                    mixed = -1.0;
                }

                if (mode == F_RAW) {
                    float fval = (float)mixed;
                    if (fwrite(&fval, sizeof(fval), 1, out) != 1) {
                        stop_requested = 1;
                        break;
                    }
                } else if (mode == F_TEXT) {
                    if (fprintf(out, "%f\n", mixed) < 0) {
                        stop_requested = 1;
                        break;
                    }
                } else {
                    int16_t s16 = (int16_t)(mixed * 32767);
                    if (fwrite(&s16, sizeof(s16), 1, out) != 1) {
                        stop_requested = 1;
                        break;
                    }
                }
            }
            loops_remaining--;
            if (loop_mode && loops_remaining > 0 && should_stop_playback(control_token)) {
                stop_requested = 1;
            }
        }

        if (mode == F_PLAY) {
            pclose(out);
        }

        if (!loop_mode) {
            clear_state();
        }
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            free_sequence(&sequences[i]);
        }
        return 0;
    }

    if (!strcmp(cmd, "enter")) {
        if (argc < 6) {
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

        long volume = 0;
        if (parse_long(argv[5], &volume, 0, 100) != 0) {
            fprintf(stderr, "Volume must be an integer between 0 and 100.\n");
            usage(argv[0]);
        }

        long channel = 1;
        int arg_index = 6;
        if (argc > arg_index) {
            if (parse_long(argv[arg_index], &channel, SIGNAL_MIN_CHANNEL, SIGNAL_MAX_CHANNEL) == 0) {
                arg_index++;
            }
        }

        long attack_pct = -1;
        long decay_pct = -1;
        long sustain_pct = -1;
        long release_pct = -1;
        double lowpass_hz = 0.0;
        double highpass_hz = 0.0;

        if (argc > arg_index && parse_long(argv[arg_index], &attack_pct, 0, 100) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &decay_pct, 0, 100) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &sustain_pct, 0, 100) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_long(argv[arg_index], &release_pct, 0, 100) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_double(argv[arg_index], &lowpass_hz, 0.0, 20000.0) == 0) {
            arg_index++;
        }
        if (argc > arg_index && parse_double(argv[arg_index], &highpass_hz, 0.0, 20000.0) == 0) {
            arg_index++;
        }

        long attack_value = attack_pct >= 0 ? attack_pct : 0;
        long decay_value = decay_pct >= 0 ? decay_pct : 0;
        long release_value = release_pct >= 0 ? release_pct : 0;
        long sustain_value = 0;
        if (sustain_pct >= 0) {
            sustain_value = sustain_pct;
            if (attack_value + decay_value + sustain_value + release_value != 100) {
                fprintf(stderr, "ADSR percentages must total 100.\n");
                usage(argv[0]);
            }
        } else {
            long sum = attack_value + decay_value + release_value;
            if (sum > 100) {
                fprintf(stderr, "ADSR percentages must total 100.\n");
                usage(argv[0]);
            }
            sustain_value = 100 - sum;
        }

        NoteSequence sequences[SIGNAL_MAX_CHANNEL];
        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            init_sequence(&sequences[i]);
        }
        load_state(sequences, SIGNAL_MAX_CHANNEL);

        NoteEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.active = 1;
        entry.wave = wave;
        snprintf(entry.note, sizeof(entry.note), "%s", note);
        entry.freq = freq;
        entry.duration_ms = duration_ms;
        entry.volume = (int)volume;
        if (normalize_adsr_percent(duration_ms, attack_value, decay_value, sustain_value,
                release_value, 0, &entry.attack_pct, &entry.decay_pct,
                &entry.sustain_pct, &entry.release_pct) != 0) {
            fprintf(stderr, "ADSR percentages must total 100.\n");
            usage(argv[0]);
        }
        entry.lowpass_hz = lowpass_hz;
        entry.highpass_hz = highpass_hz;

        if (append_note(&sequences[channel - 1], &entry) != 0) {
            perror("signal: realloc");
            for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                free_sequence(&sequences[i]);
            }
            return EXIT_FAILURE;
        }

        if (save_state(sequences, SIGNAL_MAX_CHANNEL) != 0) {
            for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
                free_sequence(&sequences[i]);
            }
            return EXIT_FAILURE;
        }

        for (size_t i = 0; i < SIGNAL_MAX_CHANNEL; ++i) {
            free_sequence(&sequences[i]);
        }
        return 0;
    }

    if (!strcmp(cmd, "stop")) {
        clear_state();
        unlink(SIGNAL_PLAY_PATH);
        set_control_value("stop");
        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
}
