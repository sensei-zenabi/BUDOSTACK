#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef BUDOSTACK_HAVE_ALSA
#define BUDOSTACK_HAVE_ALSA 0
#endif

#if BUDOSTACK_HAVE_ALSA
#include <alsa/asoundlib.h>

#define MIN_FFT_SIZE 256
#define MAX_FFT_SIZE 8192
#define SPECTRUM_MAX_REFRESH_RATE 4
#define SPECTRUM_REFRESH_INTERVAL_NS (1000000000LL / SPECTRUM_MAX_REFRESH_RATE)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct complex_sample {
    double re;
    double im;
};

struct analyzer_state {
    size_t fft_size;
    size_t bin_count;
    double *window;
    int16_t *audio_buffer;
    struct complex_sample *fft_buffer;
    double *magnitudes;
    double *history;
    size_t history_capacity;
    size_t history_head;
    size_t history_count;
    double amplitude_normalizer;
};

static struct termios orig_termios;
static int raw_mode_enabled = 0;
static char *line_buffer = NULL;
static size_t line_capacity = 0;

static void ensure_line_capacity(int columns);
static int should_draw_frame(struct timespec *last_draw, int *initialized);

static void disable_raw_mode(void)
{
    if (!raw_mode_enabled) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
    raw_mode_enabled = 0;
}

static void enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disable_raw_mode);
    write(STDOUT_FILENO, "\x1b[?25l", 6);
    raw_mode_enabled = 1;
}

static void clear_screen(void)
{
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

static void get_terminal_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        *rows = 24;
        *cols = 80;
        return;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

static void free_analyzer(struct analyzer_state *state)
{
    free(state->window);
    free(state->audio_buffer);
    free(state->fft_buffer);
    free(state->magnitudes);
    free(state->history);
    state->window = NULL;
    state->audio_buffer = NULL;
    state->fft_buffer = NULL;
    state->magnitudes = NULL;
    state->history = NULL;
    state->fft_size = 0;
    state->bin_count = 0;
    state->history_capacity = 0;
    state->history_head = 0;
    state->history_count = 0;
    state->amplitude_normalizer = 1.0;
}

static int reconfigure_fft(struct analyzer_state *state, size_t new_fft_size)
{
    double *new_window = malloc(new_fft_size * sizeof(double));
    int16_t *new_audio = malloc(new_fft_size * sizeof(int16_t));
    struct complex_sample *new_fft = malloc(new_fft_size * sizeof(struct complex_sample));
    double *new_magnitudes = malloc((new_fft_size / 2) * sizeof(double));

    if (!new_window || !new_audio || !new_fft || !new_magnitudes) {
        free(new_window);
        free(new_audio);
        free(new_fft);
        free(new_magnitudes);
        return -1;
    }

    for (size_t i = 0; i < new_fft_size; ++i) {
        double theta = (double)i / (double)(new_fft_size - 1);
        new_window[i] = 0.5 - 0.5 * cos(2.0 * M_PI * theta);
    }

    free(state->window);
    free(state->audio_buffer);
    free(state->fft_buffer);
    free(state->magnitudes);

    state->window = new_window;
    state->audio_buffer = new_audio;
    state->fft_buffer = new_fft;
    state->magnitudes = new_magnitudes;
    state->fft_size = new_fft_size;
    state->bin_count = new_fft_size / 2;
    state->amplitude_normalizer = (double)new_fft_size * 32768.0;
    memset(state->audio_buffer, 0, new_fft_size * sizeof(int16_t));
    memset(state->fft_buffer, 0, new_fft_size * sizeof(struct complex_sample));
    memset(state->magnitudes, 0, (new_fft_size / 2) * sizeof(double));

    return 0;
}

static int reallocate_history(struct analyzer_state *state, size_t new_capacity)
{
    double *new_history = NULL;
    if (new_capacity > 0 && state->bin_count > 0) {
        new_history = calloc(new_capacity * state->bin_count, sizeof(double));
        if (!new_history) {
            return -1;
        }
    }

    free(state->history);
    state->history = new_history;
    state->history_capacity = new_capacity;
    state->history_count = 0;
    state->history_head = new_capacity ? new_capacity - 1 : 0;

    return 0;
}

static size_t compute_history_capacity(int rows)
{
    if (rows <= 4) {
        return 0;
    }
    size_t visible_rows = (size_t)(rows - 4);
    if (visible_rows > SIZE_MAX / 2) {
        return SIZE_MAX;
    }
    return visible_rows * 2;
}

static void fft_transform(struct complex_sample *buffer, size_t n)
{
    if (n <= 1) {
        return;
    }

    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            struct complex_sample tmp = buffer[i];
            buffer[i] = buffer[j];
            buffer[j] = tmp;
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / (double)len;
        double wlen_cos = cos(angle);
        double wlen_sin = sin(angle);
        for (size_t i = 0; i < n; i += len) {
            double w_cos = 1.0;
            double w_sin = 0.0;
            for (size_t k = 0; k < len / 2; ++k) {
                struct complex_sample u = buffer[i + k];
                struct complex_sample t;
                size_t even_index = i + k + len / 2;
                t.re = w_cos * buffer[even_index].re - w_sin * buffer[even_index].im;
                t.im = w_cos * buffer[even_index].im + w_sin * buffer[even_index].re;
                buffer[i + k].re = u.re + t.re;
                buffer[i + k].im = u.im + t.im;
                buffer[even_index].re = u.re - t.re;
                buffer[even_index].im = u.im - t.im;
                double next_w_cos = w_cos * wlen_cos - w_sin * wlen_sin;
                double next_w_sin = w_cos * wlen_sin + w_sin * wlen_cos;
                w_cos = next_w_cos;
                w_sin = next_w_sin;
            }
        }
    }
}

static size_t map_bin(size_t bin_count, size_t column, size_t columns, int use_log)
{
    if (bin_count == 0) {
        return 0;
    }
    if (columns <= 1 || bin_count == 1) {
        return 0;
    }
    double t = (double)column / (double)(columns - 1);
    if (!use_log) {
        double idx = t * (double)(bin_count - 1);
        size_t result = (size_t)llround(idx);
        if (result >= bin_count) {
            result = bin_count - 1;
        }
        return result;
    }
    double value = exp(t * log((double)bin_count));
    size_t mapped = 0;
    if (value > 1.0) {
        mapped = (size_t)llround(value - 1.0);
    }
    if (mapped >= bin_count) {
        mapped = bin_count - 1;
    }
    return mapped;
}

static int amplitude_to_color(double value)
{
    static const int palette[] = {
        16, 17, 18, 19, 20, 21, 27, 33, 39, 45, 51, 50, 49, 48, 82, 118, 154, 190, 220, 214, 208,
        202, 196, 199, 201, 207, 213, 219, 225, 231
    };
    static const size_t palette_len = sizeof(palette) / sizeof(palette[0]);
    if (value < 0.0) {
        value = 0.0;
    }
    if (value > 1.0) {
        value = 1.0;
    }
    size_t index = (size_t)lrint(value * (double)(palette_len - 1));
    if (index >= palette_len) {
        index = palette_len - 1;
    }
    return palette[index];
}

static double column_to_frequency(const struct analyzer_state *state,
    size_t column,
    size_t columns,
    int use_log_frequency,
    unsigned int sample_rate)
{
    if (columns == 0 || state->fft_size == 0) {
        return 0.0;
    }
    size_t bin = map_bin(state->bin_count, column, columns, use_log_frequency);
    if (bin >= state->bin_count) {
        bin = state->bin_count ? state->bin_count - 1 : 0;
    }
    double bin_width = (double)sample_rate / (double)state->fft_size;
    return bin_width * (double)bin;
}

static double magnitude_to_display_value(const struct analyzer_state *state,
    const double *magnitudes,
    size_t bin,
    int use_log_amplitude)
{
    if (!magnitudes || state->amplitude_normalizer <= 0.0 || bin >= state->bin_count) {
        return 0.0;
    }
    double amplitude = magnitudes[bin] / state->amplitude_normalizer;
    if (amplitude < 0.0) {
        amplitude = 0.0;
    }
    if (amplitude > 1.0) {
        amplitude = 1.0;
    }
    if (use_log_amplitude) {
        return log1p(amplitude * 9.0) / log1p(9.0);
    }
    return amplitude;
}

static void draw_waterfall_row(const struct analyzer_state *state,
    const double *magnitudes,
    int columns,
    int use_log_frequency,
    int use_log_amplitude)
{
    printf("\r\x1b[0m\x1b[2K");
    if (columns <= 0 || state->bin_count == 0) {
        printf("\r\n");
        return;
    }

    int prev_color = -1;
    static const unsigned char glyph_bytes[3] = { 0xE2, 0x96, 0x88 }; /* '█' */

    for (int col = 0; col < columns; ++col) {
        size_t bin = map_bin(state->bin_count, (size_t)col, (size_t)columns, use_log_frequency);
        if (bin >= state->bin_count) {
            bin = state->bin_count - 1;
        }

        int color = amplitude_to_color(magnitude_to_display_value(state, magnitudes, bin, use_log_amplitude));
        if (color != prev_color) {
            printf("\x1b[38;5;%dm\x1b[48;5;%dm", color, color);
            prev_color = color;
        }

        fwrite(glyph_bytes, 1, 3, stdout);
    }

    if (prev_color != -1) {
        printf("\x1b[0m");
    }
    printf("\r\n");
}

static void format_frequency_label(double frequency, char *buffer, size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    if (frequency >= 1000000.0) {
        double mhz = frequency / 1000000.0;
        if (mhz >= 10.0) {
            snprintf(buffer, capacity, "%.0fMHz", mhz);
        } else {
            snprintf(buffer, capacity, "%.1fMHz", mhz);
        }
    } else if (frequency >= 1000.0) {
        double khz = frequency / 1000.0;
        if (khz >= 10.0) {
            snprintf(buffer, capacity, "%.0fkHz", khz);
        } else {
            snprintf(buffer, capacity, "%.1fkHz", khz);
        }
    } else {
        snprintf(buffer, capacity, "%.0fHz", frequency);
    }
}

static void draw_frequency_axis_baseline(const struct analyzer_state *state,
    int columns,
    int use_log_frequency,
    unsigned int sample_rate)
{
    printf("\r\x1b[0m\x1b[2K");
    if (columns <= 0) {
        printf("\r\n");
        return;
    }
    ensure_line_capacity(columns);
    memset(line_buffer, '-', (size_t)columns);
    int tick_count = columns / 12;
    if (tick_count < 2) {
        tick_count = 2;
    } else if (tick_count > 12) {
        tick_count = 12;
    }
    for (int i = 0; i <= tick_count; ++i) {
        double fraction = (double)i / (double)tick_count;
        size_t column = (size_t)llround(fraction * (double)(columns - 1));
        if (column >= (size_t)columns) {
            column = (size_t)columns - 1;
        }
        line_buffer[column] = '+';
    }
    line_buffer[columns] = '\0';
    printf("%s\r\n", line_buffer);
    (void)state;
    (void)use_log_frequency;
    (void)sample_rate;
}

static void draw_frequency_axis_labels(const struct analyzer_state *state,
    int columns,
    int use_log_frequency,
    unsigned int sample_rate)
{
    printf("\r\x1b[0m\x1b[2K");
    if (columns <= 0) {
        printf("\r\n");
        return;
    }
    ensure_line_capacity(columns);
    memset(line_buffer, ' ', (size_t)columns);
    int tick_count = columns / 12;
    if (tick_count < 2) {
        tick_count = 2;
    } else if (tick_count > 12) {
        tick_count = 12;
    }
    size_t last_end = 0;
    int first_label = 1;
    for (int i = 0; i <= tick_count; ++i) {
        double fraction = (double)i / (double)tick_count;
        size_t column = (size_t)llround(fraction * (double)(columns - 1));
        if (column >= (size_t)columns) {
            column = (size_t)columns - 1;
        }
        double frequency = column_to_frequency(state, column, (size_t)columns, use_log_frequency, sample_rate);
        char label[32];
        format_frequency_label(frequency, label, sizeof(label));
        size_t label_len = strlen(label);
        if (label_len == 0) {
            continue;
        }
        size_t start = 0;
        if (label_len < 2) {
            start = column;
        } else {
            if (column >= label_len / 2) {
                start = column - (label_len / 2);
            } else {
                start = 0;
            }
        }
        if (start + label_len > (size_t)columns) {
            if (label_len > (size_t)columns) {
                continue;
            }
            start = (size_t)columns - label_len;
        }
        if (!first_label && start <= last_end) {
            start = last_end + 1;
            if (start + label_len > (size_t)columns) {
                continue;
            }
        }
        memcpy(line_buffer + start, label, label_len);
        if (column < start || column >= start + label_len) {
            if (column < (size_t)columns) {
                line_buffer[column] = '|';
            }
        }
        last_end = start + label_len;
        first_label = 0;
    }
    line_buffer[columns] = '\0';
    printf("%s\r\n", line_buffer);
}

static void push_history(struct analyzer_state *state, const double *magnitudes)
{
    if (state->history_capacity == 0 || state->bin_count == 0) {
        return;
    }
    state->history_head = (state->history_head + 1) % state->history_capacity;
    memcpy(state->history + (state->history_head * state->bin_count), magnitudes,
        state->bin_count * sizeof(double));
    if (state->history_count < state->history_capacity) {
        state->history_count++;
    }
}

static void ensure_line_capacity(int columns)
{
    if (columns < 0) {
        return;
    }
    size_t needed = (size_t)columns + 1;
    if (needed <= line_capacity) {
        return;
    }
    char *new_buffer = realloc(line_buffer, needed);
    if (!new_buffer) {
        fprintf(stderr, "Failed to allocate line buffer\n");
        exit(EXIT_FAILURE);
    }
    line_buffer = new_buffer;
    line_capacity = needed;
}

static int should_draw_frame(struct timespec *last_draw, int *initialized)
{
    if (!last_draw || !initialized) {
        return 1;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return 1;
    }

    if (!*initialized) {
        *initialized = 1;
        *last_draw = now;
        return 1;
    }

    long long sec_diff = (long long)now.tv_sec - (long long)last_draw->tv_sec;
    long long nsec_diff = (long long)now.tv_nsec - (long long)last_draw->tv_nsec;
    long long elapsed = (sec_diff * 1000000000LL) + nsec_diff;
    if (elapsed < 0) {
        elapsed = 0;
    }

    if (elapsed >= SPECTRUM_REFRESH_INTERVAL_NS) {
        *last_draw = now;
        return 1;
    }

    return 0;
}

static void write_padded_line(const char *text, int columns, int newline)
{
    printf("\r\x1b[0m\x1b[2K");
    if (columns <= 0) {
        if (newline) {
            printf("\r\n");
        }
        return;
    }
    ensure_line_capacity(columns);
    size_t copy_len = 0;
    if (text && *text) {
        copy_len = strlen(text);
        if ((int)copy_len > columns) {
            copy_len = (size_t)columns;
        }
        memcpy(line_buffer, text, copy_len);
    }
    if ((int)copy_len < columns) {
        memset(line_buffer + copy_len, ' ', (size_t)columns - copy_len);
    }
    line_buffer[columns] = '\0';
    if (newline) {
        printf("%s\r\n", line_buffer);
    } else {
        printf("%s", line_buffer);
    }
}

static void draw_ui(const struct analyzer_state *state,
    int rows,
    int columns,
    int use_log_frequency,
    int use_log_amplitude,
    int recording,
    const char *status,
    unsigned int sample_rate)
{
    if (columns < 0) {
        columns = 0;
    }
    ensure_line_capacity(columns);

    printf("\x1b[H\x1b[0m\x1b[J");
    char header_line[512];
    int header_len = snprintf(header_line,
        sizeof(header_line),
        "Spectrum Analyzer | FFT: %zu | Sample Rate: %u Hz | Freq: %s | Amp: %s | Record: %s",
        state->fft_size,
        sample_rate,
        use_log_frequency ? "LOG" : "LIN",
        use_log_amplitude ? "LOG" : "LIN",
        recording ? "ON" : "OFF");
    if (header_len < 0) {
        header_line[0] = '\0';
    }
    if (status && *status) {
        size_t current_len = strlen(header_line);
        if (current_len < sizeof(header_line) - 1) {
            snprintf(header_line + current_len,
                sizeof(header_line) - current_len,
                " | %s",
                status);
        }
    }
    write_padded_line(header_line, columns, 1);

    size_t waterfall_rows = rows > 4 ? (size_t)(rows - 4) : 0;
    size_t max_slices = waterfall_rows;
    size_t slices_available = state->history_count < max_slices ? state->history_count : max_slices;
    size_t padding_rows = waterfall_rows > slices_available ? waterfall_rows - slices_available : 0;

    for (size_t i = 0; i < padding_rows; ++i) {
        printf("\r\x1b[2K\r\n");
    }

    if (slices_available > 0 && state->history_capacity > 0) {
        size_t start_index = (state->history_head + state->history_capacity + 1 - slices_available)
            % state->history_capacity;
        size_t remaining = slices_available;

        while (remaining > 0) {
            const double *magnitudes = state->history + (start_index * state->bin_count);
            draw_waterfall_row(state, magnitudes, columns, use_log_frequency, use_log_amplitude);
            start_index = (start_index + 1) % state->history_capacity;
            remaining--;
        }
    }

    draw_frequency_axis_baseline(state, columns, use_log_frequency, sample_rate);
    draw_frequency_axis_labels(state, columns, use_log_frequency, sample_rate);

    char footer_line[512];
    int footer_len = snprintf(footer_line,
        sizeof(footer_line),
        " R:Record[%s]  +/-:FFT %zu  L:Freq(%s)  A:Amp(%s)  Q:Quit",
        recording ? "ON" : "OFF",
        state->fft_size,
        use_log_frequency ? "LOG" : "LIN",
        use_log_amplitude ? "LOG" : "LIN");
    if (footer_len < 0) {
        footer_line[0] = '\0';
    }
    write_padded_line(footer_line, columns, 0);
    fflush(stdout);
}

static void set_status(char *status_buffer, size_t status_capacity, int *timeout, const char *message)
{
    if (!status_buffer || status_capacity == 0) {
        return;
    }
    if (!message) {
        status_buffer[0] = '\0';
        if (timeout) {
            *timeout = 0;
        }
        return;
    }
    snprintf(status_buffer, status_capacity, "%s", message);
    if (timeout) {
        *timeout = 150;
    }
}

static int read_audio_block(snd_pcm_t *pcm, struct analyzer_state *state, char *status_buffer, size_t status_capacity, int *status_timeout)
{
    size_t frames_remaining = state->fft_size;
    size_t offset = 0;

    while (frames_remaining > 0) {
        snd_pcm_sframes_t read_frames = snd_pcm_readi(pcm, state->audio_buffer + offset, frames_remaining);
        if (read_frames == -EPIPE) {
            snd_pcm_prepare(pcm);
            set_status(status_buffer, status_capacity, status_timeout, "Audio overrun (recovering)");
            continue;
        }
        if (read_frames < 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Audio read error: %s", snd_strerror((int)read_frames));
            set_status(status_buffer, status_capacity, status_timeout, msg);
            return -1;
        }
        frames_remaining -= (size_t)read_frames;
        offset += (size_t)read_frames;
    }
    return 0;
}

static void compute_magnitudes(struct analyzer_state *state)
{
    for (size_t i = 0; i < state->fft_size; ++i) {
        double sample = (double)state->audio_buffer[i] * state->window[i];
        state->fft_buffer[i].re = sample;
        state->fft_buffer[i].im = 0.0;
    }
    fft_transform(state->fft_buffer, state->fft_size);
    for (size_t i = 0; i < state->bin_count; ++i) {
        double re = state->fft_buffer[i].re;
        double im = state->fft_buffer[i].im;
        state->magnitudes[i] = sqrt((re * re) + (im * im));
    }
}

int main(void)
{
    setlocale(LC_ALL, "");
    int rows = 0;
    int cols = 0;
    get_terminal_size(&rows, &cols);
    if (cols < 80) {
        fprintf(stderr, "spectrum: terminal width must be at least 80 columns (got %d)\n", cols);
        return EXIT_FAILURE;
    }

    enable_raw_mode();
    clear_screen();

    snd_pcm_t *pcm_handle = NULL;
    int err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        disable_raw_mode();
        fprintf(stderr, "spectrum: unable to open capture device: %s\n", snd_strerror(err));
        return EXIT_FAILURE;
    }

    snd_pcm_hw_params_t *hw_params = NULL;
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, 1);
    unsigned int sample_rate = 48000;
    snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &sample_rate, 0);
    snd_pcm_hw_params(pcm_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    snd_pcm_prepare(pcm_handle);

    struct analyzer_state state;
    memset(&state, 0, sizeof(state));

    size_t fft_size = 1024;
    if (fft_size < MIN_FFT_SIZE) {
        fft_size = MIN_FFT_SIZE;
    }
    if (fft_size > MAX_FFT_SIZE) {
        fft_size = MAX_FFT_SIZE;
    }

    if (reconfigure_fft(&state, fft_size) != 0) {
        disable_raw_mode();
        snd_pcm_close(pcm_handle);
        fprintf(stderr, "spectrum: failed to initialize FFT buffers\n");
        return EXIT_FAILURE;
    }

    size_t desired_history = compute_history_capacity(rows);
    if (reallocate_history(&state, desired_history) != 0) {
        disable_raw_mode();
        snd_pcm_close(pcm_handle);
        free_analyzer(&state);
        fprintf(stderr, "spectrum: unable to allocate history buffer\n");
        return EXIT_FAILURE;
    }

    int use_log_frequency = 0;
    int use_log_amplitude = 0;
    int recording = 0;
    FILE *record_file = NULL;
    char status_buffer[256];
    status_buffer[0] = '\0';
    int status_timeout = 0;

    struct pollfd input_fd;
    input_fd.fd = STDIN_FILENO;
    input_fd.events = POLLIN;

    int running = 1;
    struct timespec last_draw_time;
    last_draw_time.tv_sec = 0;
    last_draw_time.tv_nsec = 0;
    int draw_time_initialized = 0;

    while (running) {
        if (read_audio_block(pcm_handle, &state, status_buffer, sizeof(status_buffer), &status_timeout) == 0) {
            compute_magnitudes(&state);
            push_history(&state, state.magnitudes);
            if (recording && record_file) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                fprintf(record_file, "%lld.%03ld",
                    (long long)ts.tv_sec,
                    ts.tv_nsec / 1000000);
                for (size_t i = 0; i < state.bin_count; ++i) {
                    fprintf(record_file, " %.6f", state.magnitudes[i]);
                }
                fputc('\n', record_file);
                fflush(record_file);
            }
        }

        get_terminal_size(&rows, &cols);
        int should_draw = should_draw_frame(&last_draw_time, &draw_time_initialized);
        if (cols < 80) {
            if (should_draw) {
                printf("\x1b[H\x1b[0m\x1b[J");
                printf("\r\x1b[2KSpectrum Analyzer requires at least 80 columns. Current width: %d\r\n", cols);
                printf("\r\x1b[2K\r\n");
                printf("\r\x1b[2KPlease resize the terminal.\r\n");
                printf("\r\x1b[2K");
                fflush(stdout);
            }
        } else {
            size_t new_history = compute_history_capacity(rows);
            if (new_history != state.history_capacity) {
                if (reallocate_history(&state, new_history) != 0) {
                    disable_raw_mode();
                    snd_pcm_close(pcm_handle);
                    free_analyzer(&state);
                    fprintf(stderr, "spectrum: unable to adjust history buffer\n");
                    if (record_file) {
                        fclose(record_file);
                    }
                    return EXIT_FAILURE;
                }
            }
            if (should_draw) {
                draw_ui(&state, rows, cols, use_log_frequency, use_log_amplitude, recording, status_buffer, sample_rate);
            }
        }

        if (status_timeout > 0) {
            status_timeout--;
            if (status_timeout == 0) {
                status_buffer[0] = '\0';
            }
        }

        int poll_result = poll(&input_fd, 1, 0);
        if (poll_result > 0 && (input_fd.revents & POLLIN)) {
            unsigned char ch;
            ssize_t read_bytes = read(STDIN_FILENO, &ch, 1);
            if (read_bytes == 1) {
                switch (ch) {
                case 'q':
                case 'Q':
                    running = 0;
                    break;
                case 'r':
                case 'R':
                    if (!recording) {
                        record_file = fopen("spectrum.txt", "a");
                        if (!record_file) {
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "Failed to open spectrum.txt");
                        } else {
                            recording = 1;
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "Recording started");
                            fprintf(record_file, "# Spectrum recording start (FFT %zu, Rate %u)\n", state.fft_size, sample_rate);
                        }
                    } else {
                        recording = 0;
                        if (record_file) {
                            fprintf(record_file, "# Spectrum recording stop\n");
                            fclose(record_file);
                            record_file = NULL;
                        }
                        set_status(status_buffer, sizeof(status_buffer), &status_timeout, "Recording stopped");
                    }
                    break;
                case '+':
                case '=': {
                    size_t new_fft = state.fft_size * 2;
                    if (new_fft > MAX_FFT_SIZE) {
                        new_fft = MAX_FFT_SIZE;
                    }
                    if (new_fft != state.fft_size) {
                        if (reconfigure_fft(&state, new_fft) != 0) {
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "Failed to resize FFT");
                        } else {
                            reallocate_history(&state, compute_history_capacity(rows));
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "FFT size increased");
                        }
                    }
                    break;
                }
                case '-':
                case '_': {
                    size_t new_fft = state.fft_size / 2;
                    if (new_fft < MIN_FFT_SIZE) {
                        new_fft = MIN_FFT_SIZE;
                    }
                    if (new_fft != state.fft_size) {
                        if (reconfigure_fft(&state, new_fft) != 0) {
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "Failed to resize FFT");
                        } else {
                            reallocate_history(&state, compute_history_capacity(rows));
                            set_status(status_buffer, sizeof(status_buffer), &status_timeout, "FFT size decreased");
                        }
                    }
                    break;
                }
                case 'l':
                case 'L':
                    use_log_frequency = !use_log_frequency;
                    set_status(status_buffer, sizeof(status_buffer), &status_timeout, use_log_frequency ? "Log frequency" : "Linear frequency");
                    break;
                case 'a':
                case 'A':
                    use_log_amplitude = !use_log_amplitude;
                    set_status(status_buffer, sizeof(status_buffer), &status_timeout, use_log_amplitude ? "Log amplitude" : "Linear amplitude");
                    break;
                default:
                    break;
                }
            }
        }
    }

    if (recording && record_file) {
        fprintf(record_file, "# Spectrum recording stop\n");
        fclose(record_file);
        record_file = NULL;
    }

    free(line_buffer);
    line_buffer = NULL;
    line_capacity = 0;

    clear_screen();
    disable_raw_mode();

    snd_pcm_close(pcm_handle);
    free_analyzer(&state);
    return EXIT_SUCCESS;
}
#else
int main(void)
{
    fprintf(stderr, "spectrum: built without ALSA support\n");
    return EXIT_FAILURE;
}
#endif

