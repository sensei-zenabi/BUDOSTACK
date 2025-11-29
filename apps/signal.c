#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>    // for isatty, STDOUT_FILENO
#include <errno.h>     // for errno

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100.0

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -<waveform> <freq_Hz> <duration_s> <channel> [format]\n"
        "  waveforms: sine, square, triangle, sawtooth, noise\n"
        "  freq      : positive number (e.g. 1200)\n"
        "  duration  : seconds (e.g. 5.0)\n"
        "  format    : raw, text, wav\n"
        "  channel   : 1-32 (to enable parallel sounds)\n"
        "If format is omitted, the tone will play on the default ALSA device in"
        " the background.\n"
        "Examples:\n"
        "  %s -sine 440 5 1          # play a 440 Hz sine for 5 s on channel 1\n"
        "  %s -square 1000 2 2 raw   # stream raw floats to stdout\n"
        "  %s -sine 1200 3 3 wav > tone.wav\n",
        prog, prog, prog, prog);
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

int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6) {
        usage(argv[0]);
    }

    // Parse waveform
    const char *w = argv[1][0]=='-' ? argv[1]+1 : argv[1];
    enum { W_SINE, W_SQUARE, W_TRIANGLE, W_SAWTOOTH, W_NOISE } wave;
    if      (!strcmp(w,"sine"))     wave = W_SINE;
    else if (!strcmp(w,"square"))   wave = W_SQUARE;
    else if (!strcmp(w,"triangle")) wave = W_TRIANGLE;
    else if (!strcmp(w,"sawtooth")) wave = W_SAWTOOTH;
    else if (!strcmp(w,"noise"))    wave = W_NOISE;
    else {
        fprintf(stderr, "Unknown waveform: %s\n", w);
        usage(argv[0]);
    }

    // Parse frequency
    double freq = atof(argv[2]);
    if (freq <= 0.0) {
        fprintf(stderr, "Frequency must be positive.\n");
        usage(argv[0]);
    }

    // Parse duration
    double duration = atof(argv[3]);
    if (duration <= 0.0) {
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

    uint64_t total_samples = (uint64_t)(duration * SAMPLE_RATE);
    if (total_samples == 0) {
        total_samples = 1;
    }

    // Seed RNG for noise
    srand((unsigned)time(NULL) ^ (unsigned)channel);

    // Set up phase increment
    const double two_pi = 2.0 * M_PI;
    const double phase_inc = two_pi * freq / SAMPLE_RATE;
    double phase = (two_pi / 32.0) * (double)(channel - 1);

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

    // If WAV, write header
    if (mode == F_WAV) {
        write_wav_header(out, (uint32_t)total_samples);
    }

    // Warn if raw to terminal
    if (mode == F_RAW && isatty(STDOUT_FILENO)) {
        fprintf(stderr,
            "Warning: streaming binary floats to terminal. Redirect to file:\n"
            "  %s %s %s %s raw > output.raw\n",
            argv[0], argv[1], argv[2], argv[3]);
    }

    // Generate and output samples
    for (uint64_t n = 0; n < total_samples; ++n) {
        float fval;
        switch (wave) {
          case W_SINE:
            fval = (float)sin(phase);
            break;
          case W_SQUARE:
            fval = (phase < M_PI ? 1.0f : -1.0f);
            break;
          case W_SAWTOOTH: {
            double t = phase / two_pi;
            fval = (float)(2.0 * (t - floor(t + 0.5)));
            break;
          }
          case W_TRIANGLE: {
            double t = phase / two_pi;
            double saw = 2.0 * (t - floor(t + 0.5));
            fval = (float)(2.0 * fabs(saw) - 1.0);
            break;
          }
          case W_NOISE:
            fval = (float)((rand() / (double)RAND_MAX) * 2.0 - 1.0);
            break;
        }

        if (mode == F_RAW) {
            if (fwrite(&fval, sizeof(fval), 1, out) != 1) break;
        }
        else if (mode == F_TEXT) {
            if (fprintf(out, "%f\n", fval) < 0) break;
        }
        else {
            // F_WAV or F_PLAY both want 16-bit PCM
            int16_t s16 = (int16_t)(fval * 32767);
            if (fwrite(&s16, sizeof(s16), 1, out) != 1) break;
        }

        phase += phase_inc;
        if (phase >= two_pi) phase -= two_pi;
    }

    if (mode == F_PLAY) {
        pclose(out);
    }

    return 0;
}
