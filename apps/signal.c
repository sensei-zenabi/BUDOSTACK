#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>    // for isatty, STDOUT_FILENO
#include <errno.h>     // for errno
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100.0
#define SUSTAIN_LEVEL 0.8

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -<cmd> -<waveform> <note> <duration_s> [channel] [format] "
        "-<attack> -<decay> -<sustain> -<release> -<lowpass> -<highpass>\n"
        "  cmd       : enter, play (plays already entered notes, does not require below args)\n"
        "  waveforms : sine, square, triangle, sawtooth, noise\n"
        "  note      : standard concert pitch notes (e.g. c2, c3, c4, d4, e4)\n"
        "  duration  : milliseconds (e.g. 500 = 500ms)\n"
        "  format    : raw, text, wav\n"
        "  channel   : (optional) 1-32 (to enable parallel sounds, default 1 if not defined)\n"
        "  attack    : (optional) in milliseconds\n"
        "  decay     : (optional) in milliseconds\n"
        "  sustain   : (optional) in milliseconds\n"
        "  release   : (optional) in milliseconds\n"
        "  lowpass   : (optional) in Hz\n"
        "  highpass  : (optional) in Hz\n"
        "If format is omitted, playback uses the default ALSA device in the background.\n",
        prog);
    exit(EXIT_FAILURE);
}

static int write_wav_header(FILE *f, uint32_t total_samples) {
    uint32_t data_bytes = total_samples * 2;   // 2 bytes per sample

    fwrite("RIFF", 1, 4, f);
    uint32_t chunk_size = 36 + data_bytes;
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, 4, 1, f);
    uint16_t audio_format = 1;       // PCM
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

enum waveform_kind {
    W_SINE,
    W_SQUARE,
    W_TRIANGLE,
    W_SAWTOOTH,
    W_NOISE
};

struct signal_params {
    enum waveform_kind wave;
    double frequency;
    double duration_ms;
    int channel;
    double attack_ms;
    double decay_ms;
    double sustain_ms;
    double release_ms;
    double lowpass_hz;
    double highpass_hz;
};

static const char *queue_path(void) {
    static char path[PATH_MAX];
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || dir[0] == '\0') {
        dir = "/tmp";
    }
    snprintf(path, sizeof(path), "%s/budostack_signal_queue.txt", dir);
    return path;
}

static bool parse_waveform(const char *input, enum waveform_kind *wave) {
    if (!strcmp(input, "sine")) {
        *wave = W_SINE;
    } else if (!strcmp(input, "square")) {
        *wave = W_SQUARE;
    } else if (!strcmp(input, "triangle")) {
        *wave = W_TRIANGLE;
    } else if (!strcmp(input, "sawtooth")) {
        *wave = W_SAWTOOTH;
    } else if (!strcmp(input, "noise")) {
        *wave = W_NOISE;
    } else {
        return false;
    }
    return true;
}

static const char *waveform_name(enum waveform_kind wave) {
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

static bool parse_double(const char *text, double *value) {
    char *end = NULL;
    errno = 0;
    double parsed = strtod(text, &end);
    if (errno != 0 || !end || end == text || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_int(const char *text, int *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0') {
        return false;
    }
    if (parsed > INT_MAX || parsed < INT_MIN) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_note_frequency(const char *note, double *freq_out) {
    size_t index = 0;
    int dot_shift = 0;
    while (note[index] == '.') {
        dot_shift++;
        index++;
    }

    if (note[index] == '\0') {
        return false;
    }

    char letter = (char)tolower((unsigned char)note[index]);
    int semitone = 0;
    switch (letter) {
      case 'c':
        semitone = 0;
        break;
      case 'd':
        semitone = 2;
        break;
      case 'e':
        semitone = 4;
        break;
      case 'f':
        semitone = 5;
        break;
      case 'g':
        semitone = 7;
        break;
      case 'a':
        semitone = 9;
        break;
      case 'b':
        semitone = 11;
        break;
      default:
        return false;
    }
    index++;

    if (note[index] == '#' || note[index] == 'b') {
        if (note[index] == '#') {
            semitone += 1;
        } else {
            semitone -= 1;
        }
        index++;
    }

    if (note[index] == '\0') {
        return false;
    }

    int octave = 0;
    if (!parse_int(note + index, &octave)) {
        return false;
    }
    octave -= dot_shift;

    int midi = (octave + 1) * 12 + semitone;
    *freq_out = 440.0 * pow(2.0, (midi - 69) / 12.0);
    return true;
}

static bool is_format_token(const char *text) {
    return !strcmp(text, "raw") || !strcmp(text, "text") || !strcmp(text, "wav");
}

static uint64_t ms_to_samples(double ms) {
    if (ms <= 0.0) {
        return 0;
    }
    double seconds = ms / 1000.0;
    uint64_t samples = (uint64_t)(seconds * SAMPLE_RATE);
    if (samples == 0) {
        samples = 1;
    }
    return samples;
}

static void apply_filters(double *value, double *lp_state, double *hp_state,
    double *hp_prev, double lowpass_hz, double highpass_hz) {
    double sample = *value;

    if (lowpass_hz > 0.0) {
        double rc = 1.0 / (2.0 * M_PI * lowpass_hz);
        double dt = 1.0 / SAMPLE_RATE;
        double alpha = dt / (rc + dt);
        *lp_state = *lp_state + alpha * (sample - *lp_state);
        sample = *lp_state;
    }

    if (highpass_hz > 0.0) {
        double rc = 1.0 / (2.0 * M_PI * highpass_hz);
        double dt = 1.0 / SAMPLE_RATE;
        double alpha = rc / (rc + dt);
        double filtered = alpha * (*hp_state + sample - *hp_prev);
        *hp_prev = sample;
        *hp_state = filtered;
        sample = filtered;
    }

    *value = sample;
}

static double envelope_gain(uint64_t index, uint64_t attack_samples,
    uint64_t decay_samples, uint64_t sustain_samples, uint64_t release_samples) {
    uint64_t attack_end = attack_samples;
    uint64_t decay_end = attack_end + decay_samples;
    uint64_t sustain_end = decay_end + sustain_samples;

    if (attack_samples > 0 && index < attack_end) {
        return (double)index / (double)attack_samples;
    }
    if (decay_samples > 0 && index < decay_end) {
        double progress = (double)(index - attack_end) / (double)decay_samples;
        return 1.0 + (SUSTAIN_LEVEL - 1.0) * progress;
    }
    if (index < sustain_end) {
        return SUSTAIN_LEVEL;
    }
    if (release_samples > 0) {
        uint64_t release_index = index - sustain_end;
        if (release_index < release_samples) {
            double progress = (double)release_index / (double)release_samples;
            return SUSTAIN_LEVEL * (1.0 - progress);
        }
    }
    return 0.0;
}

static int render_signal(FILE *out, enum waveform_kind wave, double freq,
    uint64_t total_samples, const struct signal_params *params, int mode,
    bool warn_terminal) {
    const double two_pi = 2.0 * M_PI;
    const double phase_inc = two_pi * freq / SAMPLE_RATE;
    double phase = (two_pi / 32.0) * (double)(params->channel - 1);

    uint64_t attack_samples = ms_to_samples(params->attack_ms);
    uint64_t decay_samples = ms_to_samples(params->decay_ms);
    uint64_t sustain_samples = ms_to_samples(params->sustain_ms);
    uint64_t release_samples = ms_to_samples(params->release_ms);

    uint64_t remaining = total_samples;
    if (attack_samples > remaining) {
        attack_samples = remaining;
    }
    remaining -= attack_samples;
    if (decay_samples > remaining) {
        decay_samples = remaining;
    }
    remaining -= decay_samples;
    if (sustain_samples > remaining) {
        sustain_samples = remaining;
    }
    remaining -= sustain_samples;
    if (release_samples > remaining) {
        release_samples = remaining;
    }
    remaining -= release_samples;
    sustain_samples += remaining;

    if (mode == 0 && warn_terminal) {
        fprintf(stderr,
            "Warning: streaming binary floats to terminal. Redirect to file.\n");
    }

    double lp_state = 0.0;
    double hp_state = 0.0;
    double hp_prev = 0.0;

    for (uint64_t n = 0; n < total_samples; ++n) {
        double fval = 0.0;
        switch (wave) {
          case W_SINE:
            fval = sin(phase);
            break;
          case W_SQUARE:
            fval = (phase < M_PI ? 1.0 : -1.0);
            break;
          case W_SAWTOOTH: {
            double t = phase / two_pi;
            fval = 2.0 * (t - floor(t + 0.5));
            break;
          }
          case W_TRIANGLE: {
            double t = phase / two_pi;
            double saw = 2.0 * (t - floor(t + 0.5));
            fval = 2.0 * fabs(saw) - 1.0;
            break;
          }
          case W_NOISE:
            fval = (rand() / (double)RAND_MAX) * 2.0 - 1.0;
            break;
        }

        double gain = envelope_gain(n, attack_samples, decay_samples, sustain_samples,
            release_samples);
        fval *= gain;
        apply_filters(&fval, &lp_state, &hp_state, &hp_prev, params->lowpass_hz,
            params->highpass_hz);

        if (mode == 0) {
            float f_out = (float)fval;
            if (fwrite(&f_out, sizeof(f_out), 1, out) != 1) {
                return -1;
            }
        } else if (mode == 1) {
            if (fprintf(out, "%f\n", fval) < 0) {
                return -1;
            }
        } else {
            int16_t s16 = (int16_t)(fval * 32767);
            if (fwrite(&s16, sizeof(s16), 1, out) != 1) {
                return -1;
            }
        }

        phase += phase_inc;
        if (phase >= two_pi) {
            phase -= two_pi;
        }
    }

    return 0;
}

static int enqueue_signal(const struct signal_params *params) {
    FILE *out = fopen(queue_path(), "a");
    if (!out) {
        fprintf(stderr, "Failed to open queue file: %s\n", strerror(errno));
        return -1;
    }

    fprintf(out, "%s %.8f %.3f %d %.3f %.3f %.3f %.3f %.3f %.3f\n",
        waveform_name(params->wave),
        params->frequency,
        params->duration_ms,
        params->channel,
        params->attack_ms,
        params->decay_ms,
        params->sustain_ms,
        params->release_ms,
        params->lowpass_hz,
        params->highpass_hz);

    fclose(out);
    return 0;
}

static int load_queue(struct signal_params **out_params, size_t *out_count) {
    FILE *in = fopen(queue_path(), "r");
    if (!in) {
        fprintf(stderr, "No queued notes. Use 'signal -enter' first.\n");
        return -1;
    }

    struct signal_params *params = NULL;
    size_t count = 0;
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        char wave_name[32];
        struct signal_params note = {0};
        if (sscanf(line, "%31s %lf %lf %d %lf %lf %lf %lf %lf %lf",
            wave_name,
            &note.frequency,
            &note.duration_ms,
            &note.channel,
            &note.attack_ms,
            &note.decay_ms,
            &note.sustain_ms,
            &note.release_ms,
            &note.lowpass_hz,
            &note.highpass_hz) != 10) {
            fprintf(stderr, "Skipping malformed queue entry: %s", line);
            continue;
        }

        if (!parse_waveform(wave_name, &note.wave)) {
            fprintf(stderr, "Skipping unknown waveform in queue: %s\n", wave_name);
            continue;
        }

        struct signal_params *resized = realloc(params, (count + 1) * sizeof(*params));
        if (!resized) {
            fprintf(stderr, "Out of memory while reading queue.\n");
            free(params);
            fclose(in);
            return -1;
        }
        params = resized;
        params[count++] = note;
    }

    fclose(in);
    if (count == 0) {
        free(params);
        fprintf(stderr, "No valid notes found in queue.\n");
        return -1;
    }
    *out_params = params;
    *out_count = count;
    return 0;
}

static int play_queue(const char *format) {
    struct signal_params *params = NULL;
    size_t count = 0;
    if (load_queue(&params, &count) != 0) {
        return EXIT_FAILURE;
    }

    enum { F_RAW, F_TEXT, F_WAV, F_PLAY } mode = F_PLAY;
    if (format) {
        if (!strcmp(format, "raw")) {
            mode = F_RAW;
        } else if (!strcmp(format, "text")) {
            mode = F_TEXT;
        } else if (!strcmp(format, "wav")) {
            mode = F_WAV;
        } else {
            fprintf(stderr, "Unknown format: %s\n", format);
            free(params);
            return EXIT_FAILURE;
        }
    }

    FILE *out = stdout;
    if (mode == F_PLAY) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Failed to fork for playback: %s\n", strerror(errno));
            free(params);
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            free(params);
            return 0;
        }

        out = popen("aplay -q -f S16_LE -c1 -r44100", "w");
        if (!out) {
            fprintf(stderr, "Failed to launch aplay: %s\n", strerror(errno));
            free(params);
            return EXIT_FAILURE;
        }
    }

    uint64_t total_samples = 0;
    for (size_t i = 0; i < count; ++i) {
        total_samples += ms_to_samples(params[i].duration_ms);
    }
    if (total_samples == 0) {
        total_samples = 1;
    }

    if (mode == F_WAV) {
        write_wav_header(out, (uint32_t)total_samples);
    }

    for (size_t i = 0; i < count; ++i) {
        uint64_t note_samples = ms_to_samples(params[i].duration_ms);
        if (note_samples == 0) {
            note_samples = 1;
        }
        render_signal(out, params[i].wave, params[i].frequency, note_samples,
            &params[i], mode == F_RAW ? 0 : (mode == F_TEXT ? 1 : 2),
            mode == F_RAW && isatty(STDOUT_FILENO));
    }

    if (mode == F_PLAY) {
        pclose(out);
    }

    free(params);
    unlink(queue_path());
    return 0;
}

static int legacy_mode(int argc, char *argv[]) {
    if (argc != 5 && argc != 6) {
        usage(argv[0]);
    }

    // Parse waveform
    const char *w = argv[1][0]=='-' ? argv[1]+1 : argv[1];
    enum waveform_kind wave;
    if (!parse_waveform(w, &wave)) {
        fprintf(stderr, "Unknown waveform: %s\n", w);
        usage(argv[0]);
    }

    // Parse frequency
    double freq = atof(argv[2]);
    if (freq <= 0.0) {
        fprintf(stderr, "Frequency must be positive.\n");
        usage(argv[0]);
    }

    // Parse duration (seconds)
    double duration_s = atof(argv[3]);
    if (duration_s <= 0.0) {
        fprintf(stderr, "Duration must be positive.\n");
        usage(argv[0]);
    }

    // Parse channel
    int channel = atoi(argv[4]);
    if (channel < 1 || channel > 32) {
        fprintf(stderr, "Channel must be between 1 and 32.\n");
        usage(argv[0]);
    }

    // Determine output mode
    enum { F_RAW, F_TEXT, F_WAV, F_PLAY } mode;
    const char *fmt = (argc == 6 ? argv[5] : NULL);
    if (!fmt) {
        mode = F_PLAY;
    } else if (!strcmp(fmt, "raw")) {
        mode = F_RAW;
    } else if (!strcmp(fmt, "text")) {
        mode = F_TEXT;
    } else if (!strcmp(fmt, "wav")) {
        mode = F_WAV;
    } else {
        fprintf(stderr, "Unknown format: %s\n", fmt);
        usage(argv[0]);
    }

    uint64_t total_samples = (uint64_t)(duration_s * SAMPLE_RATE);
    if (total_samples == 0) {
        total_samples = 1;
    }

    // Seed RNG for noise
    srand((unsigned)time(NULL) ^ (unsigned)channel);

    // Open output stream
    FILE *out = stdout;
    if (mode == F_PLAY) {
        // play via aplay: 16-bit little-endian mono 44.1 kHz
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Failed to fork for playback: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            return 0;
        }

        out = popen("aplay -q -f S16_LE -c1 -r44100", "w");
        if (!out) {
            fprintf(stderr, "Failed to launch aplay: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
    }

    if (mode == F_WAV) {
        write_wav_header(out, (uint32_t)total_samples);
    }

    struct signal_params params = {
        .wave = wave,
        .frequency = freq,
        .duration_ms = duration_s * 1000.0,
        .channel = channel,
        .attack_ms = 0.0,
        .decay_ms = 0.0,
        .sustain_ms = duration_s * 1000.0,
        .release_ms = 0.0,
        .lowpass_hz = 0.0,
        .highpass_hz = 0.0
    };

    render_signal(out, wave, freq, total_samples, &params,
        mode == F_RAW ? 0 : (mode == F_TEXT ? 1 : 2),
        mode == F_RAW && isatty(STDOUT_FILENO));

    if (mode == F_PLAY) {
        pclose(out);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }

    const char *cmd_raw = argv[1][0] == '-' ? argv[1] + 1 : argv[1];
    if (!strcmp(cmd_raw, "play")) {
        const char *format = NULL;
        if (argc >= 3) {
            const char *maybe_format = argv[2][0] == '-' ? argv[2] + 1 : argv[2];
            if (is_format_token(maybe_format)) {
                format = maybe_format;
            } else {
                fprintf(stderr, "Unknown format: %s\n", argv[2]);
                usage(argv[0]);
            }
        }
        return play_queue(format);
    }

    if (!strcmp(cmd_raw, "enter")) {
        if (argc < 5) {
            usage(argv[0]);
        }

        const char *wave_raw = argv[2][0] == '-' ? argv[2] + 1 : argv[2];
        struct signal_params params = {0};
        if (!parse_waveform(wave_raw, &params.wave)) {
            fprintf(stderr, "Unknown waveform: %s\n", wave_raw);
            usage(argv[0]);
        }

        if (!parse_note_frequency(argv[3], &params.frequency)) {
            fprintf(stderr, "Unknown note format: %s\n", argv[3]);
            usage(argv[0]);
        }

        if (!parse_double(argv[4], &params.duration_ms) || params.duration_ms <= 0.0) {
            fprintf(stderr, "Duration must be a positive number of milliseconds.\n");
            usage(argv[0]);
        }

        int argi = 5;
        params.channel = 1;
        if (argi < argc && argv[argi][0] != '-' && !is_format_token(argv[argi])) {
            if (!parse_int(argv[argi], &params.channel)) {
                fprintf(stderr, "Channel must be an integer.\n");
                usage(argv[0]);
            }
            argi++;
        }

        if (params.channel < 1 || params.channel > 32) {
            fprintf(stderr, "Channel must be between 1 and 32.\n");
            usage(argv[0]);
        }

        if (argi < argc && is_format_token(argv[argi])) {
            argi++;
        }

        while (argi < argc) {
            const char *opt = argv[argi][0] == '-' ? argv[argi] + 1 : argv[argi];
            if (argi + 1 >= argc) {
                fprintf(stderr, "Missing value for option: %s\n", argv[argi]);
                usage(argv[0]);
            }
            const char *val = argv[argi + 1];
            double parsed = 0.0;
            if (!parse_double(val, &parsed)) {
                fprintf(stderr, "Invalid numeric value: %s\n", val);
                usage(argv[0]);
            }

            if (!strcmp(opt, "attack")) {
                params.attack_ms = parsed;
            } else if (!strcmp(opt, "decay")) {
                params.decay_ms = parsed;
            } else if (!strcmp(opt, "sustain")) {
                params.sustain_ms = parsed;
            } else if (!strcmp(opt, "release")) {
                params.release_ms = parsed;
            } else if (!strcmp(opt, "lowpass")) {
                params.lowpass_hz = parsed;
            } else if (!strcmp(opt, "highpass")) {
                params.highpass_hz = parsed;
            } else {
                fprintf(stderr, "Unknown option: %s\n", opt);
                usage(argv[0]);
            }
            argi += 2;
        }

        srand((unsigned)time(NULL) ^ (unsigned)params.channel);
        if (enqueue_signal(&params) != 0) {
            return EXIT_FAILURE;
        }
        return 0;
    }

    return legacy_mode(argc, argv);
}
