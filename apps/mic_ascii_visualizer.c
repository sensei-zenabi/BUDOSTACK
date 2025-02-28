#define _POSIX_C_SOURCE 200809L  // Must be defined before any headers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <complex.h>
#include <termios.h>
#include <sys/select.h>
#include <limits.h>  // for ULONG_MAX

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- FFT Implementation ---
// A simple recursive Cooley–Tukey FFT for arrays of size n (assumed power of 2)
void fft(complex double *x, int n) {
    if (n <= 1)
        return;
    int half = n / 2;
    complex double *even = malloc(half * sizeof(complex double));
    complex double *odd  = malloc(half * sizeof(complex double));
    if (!even || !odd) {
        fprintf(stderr, "Error: malloc failed in fft\n");
        exit(1);
    }
    for (int i = 0; i < half; i++) {
        even[i] = x[2 * i];
        odd[i]  = x[2 * i + 1];
    }
    fft(even, half);
    fft(odd, half);
    for (int k = 0; k < half; k++) {
        complex double t = cexp(-2.0 * I * M_PI * k / n) * odd[k];
        x[k] = even[k] + t;
        x[k + half] = even[k] - t;
    }
    free(even);
    free(odd);
}

// --- Terminal Mode Handling ---
static struct termios orig_termios;
void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}
void enable_raw_mode(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// --- Alternate Screen Cleanup ---
void cleanup_alternate_screen(void) {
    // Restore normal screen buffer
    printf("\033[?1049l");
    fflush(stdout);
}

// --- Signal Handler ---
static volatile sig_atomic_t stop_flag = 0;
void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

// --- Global Definitions for Histogram View ---
#define NUM_BINS 40
unsigned long current_hist[NUM_BINS] = {0}; // Running histogram (current)
unsigned long current_hist_total = 0;
unsigned long hist_min = ULONG_MAX;
unsigned long hist_max = 0;
unsigned long stored_hist[NUM_BINS] = {0};  // Baseline histogram (loaded from file)
unsigned long stored_hist_min = 0, stored_hist_max = 0;
int stored_hist_loaded = 0;  // Flag: 1 if a stored histogram has been loaded

//////////////////////
// Main Application //
//////////////////////
int main(int argc, char *argv[]) {
    const char *device = "default";           // Default ALSA capture device
    unsigned int rate = 44100;                // Sample rate in Hz
    unsigned int channels = 1;                // Mono audio
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;  // 16-bit little-endian
    snd_pcm_t *pcm_handle = NULL;
    snd_pcm_hw_params_t *hw_params = NULL;
    int err;

    if (argc > 1)
        device = argv[1];

    signal(SIGINT, handle_sigint);
    atexit(cleanup_alternate_screen);
    enable_raw_mode();

    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Error: cannot open audio device '%s' (%s)\n", device, snd_strerror(err));
        return 1;
    }
    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "Error: cannot allocate HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return 1;
    }
    if ((err = snd_pcm_hw_params_any(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot initialize HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    if ((err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Error: cannot set interleaved mode (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    if ((err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, format)) < 0) {
        fprintf(stderr, "Error: cannot set audio format (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    if ((err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels)) < 0) {
        fprintf(stderr, "Error: cannot set channel count (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, NULL)) < 0) {
        fprintf(stderr, "Error: cannot set sample rate to %u Hz (%s)\n", rate, snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    // Request a period of 1024 frames (for low latency)
    snd_pcm_uframes_t period_size = 1024;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params, &period_size, NULL)) < 0) {
        fprintf(stderr, "Warning: cannot set period size (%s). Using default.\n", snd_strerror(err));
    }
    if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot set HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }
    snd_pcm_hw_params_free(hw_params);
    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "Error: cannot prepare audio interface (%s)\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Obtain terminal dimensions
    int term_width = 80, term_height = 24;
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        if (ws.ws_col > 0) term_width = ws.ws_col;
        if (ws.ws_row > 0) term_height = ws.ws_row;
    }
    if (term_width % 2 == 0)
        term_width--;  // Ensure odd width

    /* Reserve two rows:
       - Second-to-last row: statistics
       - Last row: menu/instructions
       In FFT and Waterfall modes, the last row of the graph area is used for x‑axis labels.
       In CSum Histogram view, the top row is used for the error measure.
    */
    int graph_height = term_height - 2;

    // Allocate graph buffer: one string per row in the graph area
    char **graph_lines = malloc(graph_height * sizeof(char *));
    if (!graph_lines) {
        fprintf(stderr, "Error: cannot allocate graph buffer.\n");
        snd_pcm_close(pcm_handle);
        return 1;
    }
    for (int i = 0; i < graph_height; i++) {
        graph_lines[i] = malloc((term_width + 1) * sizeof(char));
        if (!graph_lines[i]) {
            fprintf(stderr, "Error: cannot allocate graph line.\n");
            for (int j = 0; j < i; j++) free(graph_lines[j]);
            free(graph_lines);
            snd_pcm_close(pcm_handle);
            return 1;
        }
        memset(graph_lines[i], ' ', term_width);
        graph_lines[i][term_width] = '\0';
    }

    // Allocate audio buffer for one period
    snd_pcm_uframes_t frames = period_size;
    int16_t *audio_buffer = malloc(frames * channels * snd_pcm_format_width(format) / 8);
    if (!audio_buffer) {
        fprintf(stderr, "Error: failed to allocate audio buffer\n");
        for (int i = 0; i < graph_height; i++) free(graph_lines[i]);
        free(graph_lines);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Temporary buffer for visualization (waveform view)
    char *vis_line = malloc((term_width + 1) * sizeof(char));
    if (!vis_line) {
        fprintf(stderr, "Error: failed to allocate visualization line buffer\n");
        free(audio_buffer);
        for (int i = 0; i < graph_height; i++) free(graph_lines[i]);
        free(graph_lines);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Buffers for statistics, menu/instructions, and x-axis labels (FFT/Waterfall view)
    char stats_line[term_width + 1];
    char menu_line[term_width + 1];
    char xaxis_line[term_width + 1];

    // Set initial FFT window size (default: 1024)
    unsigned int fft_window_size = 1024;
    // For accumulating a full FFT window, maintain a sliding buffer.
    int16_t *fft_data = NULL;
    unsigned int current_fft_size = 0; // current allocated size

    // Enable alternate screen mode
    printf("\033[?1049h");
    fflush(stdout);

    /* View modes:
       1 = Waveform
       2 = FFT
       3 = Waterfall
       4 = CSum Histogram (new)
    */
    int view_mode = 1;

    while (!stop_flag) {
        // Non-blocking keyboard input using select()
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 0};
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == '1')
                    view_mode = 1;
                else if (ch == '2')
                    view_mode = 2;
                else if (ch == '3')
                    view_mode = 3;
                else if (ch == '4')
                    view_mode = 4;  // new CSum Histogram view
                // FFT window adjustments now use keys 8 (increase) and 9 (decrease)
                else if (ch == '8' && (view_mode == 2 || view_mode == 3 || view_mode == 4)) {
                    if (fft_window_size < 32768) {
                        fft_window_size *= 2;
                        free(fft_data);
                        fft_data = malloc(fft_window_size * sizeof(int16_t));
                        if (fft_data)
                            memset(fft_data, 0, fft_window_size * sizeof(int16_t));
                        current_fft_size = fft_window_size;
                    }
                } else if (ch == '9' && (view_mode == 2 || view_mode == 3 || view_mode == 4)) {
                    if (fft_window_size > 128) {
                        fft_window_size /= 2;
                        free(fft_data);
                        fft_data = malloc(fft_window_size * sizeof(int16_t));
                        if (fft_data)
                            memset(fft_data, 0, fft_window_size * sizeof(int16_t));
                        current_fft_size = fft_window_size;
                    }
                }
                // Reset function: reset all views and data collection when 'R' is pressed
                else if (ch == 'R' || ch == 'r') {
                    // Reset histogram data
                    for (int i = 0; i < NUM_BINS; i++)
                        current_hist[i] = 0;
                    current_hist_total = 0;
                    hist_min = ULONG_MAX;
                    hist_max = 0;
                    // Reset FFT sliding buffer
                    free(fft_data);
                    fft_data = NULL;
                    current_fft_size = 0;
                    // Reset graph display buffer
                    for (int i = 0; i < graph_height; i++) {
                        free(graph_lines[i]);
                        graph_lines[i] = malloc((term_width + 1) * sizeof(char));
                        memset(graph_lines[i], ' ', term_width);
                        graph_lines[i][term_width] = '\0';
                    }
                }
                // Save function: in CSum Histogram view, save histogram to a .chist file when 'S' is pressed
                else if ((ch == 'S' || ch == 's') && view_mode == 4) {
                    disable_raw_mode();
                    char filename[256];
                    printf("\nEnter filename to save (.chist): ");
                    fflush(stdout);
                    if (fgets(filename, sizeof(filename), stdin)) {
                        char *newline = strchr(filename, '\n');
                        if (newline) *newline = '\0';
                        if (!strstr(filename, ".chist"))
                            strcat(filename, ".chist");
                        FILE *fp = fopen(filename, "w");
                        if (fp) {
                            fprintf(fp, "%d\n", NUM_BINS);
                            fprintf(fp, "%lu %lu\n", hist_min, hist_max);
                            for (int i = 0; i < NUM_BINS; i++) {
                                fprintf(fp, "%lu\n", current_hist[i]);
                            }
                            fclose(fp);
                            printf("Histogram saved to %s\n", filename);
                        } else {
                            printf("Error: cannot open file %s for writing\n", filename);
                        }
                    }
                    enable_raw_mode();
                }
                // Load function: in CSum Histogram view, load a stored .chist histogram when 'L' is pressed
                else if ((ch == 'L' || ch == 'l') && view_mode == 4) {
                    disable_raw_mode();
                    char filename[256];
                    printf("\nEnter filename to load (.chist): ");
                    fflush(stdout);
                    if (fgets(filename, sizeof(filename), stdin)) {
                        char *newline = strchr(filename, '\n');
                        if (newline) *newline = '\0';
                        FILE *fp = fopen(filename, "r");
                        if (fp) {
                            int num_bins;
                            if (fscanf(fp, "%d", &num_bins) == 1 && num_bins == NUM_BINS) {
                                if (fscanf(fp, "%lu %lu", &stored_hist_min, &stored_hist_max) == 2) {
                                    for (int i = 0; i < NUM_BINS; i++) {
                                        if (fscanf(fp, "%lu", &stored_hist[i]) != 1) break;
                                    }
                                    stored_hist_loaded = 1;
                                    printf("Histogram loaded from %s\n", filename);
                                } else {
                                    printf("Error: invalid file format\n");
                                }
                            } else {
                                printf("Error: invalid number of bins or file format\n");
                            }
                            fclose(fp);
                        } else {
                            printf("Error: cannot open file %s for reading\n", filename);
                        }
                    }
                    enable_raw_mode();
                }
            }
        }

        // Read audio data from PCM interface
        snd_pcm_sframes_t frames_read = snd_pcm_readi(pcm_handle, audio_buffer, frames);
        if (frames_read < 0) {
            if ((err = snd_pcm_recover(pcm_handle, frames_read, 0)) < 0) {
                fprintf(stderr, "Error: audio capture failed (%s)\n", snd_strerror(err));
                break;
            }
            continue;
        }
        if (frames_read == 0)
            continue;

        if (view_mode == 1) {
            /* --- Waveform (Amplitude) View --- */
            int16_t peak_pos = 0, peak_neg = 0;
            for (snd_pcm_sframes_t i = 0; i < frames_read * channels; i++) {
                int16_t sample = audio_buffer[i];
                if (sample > peak_pos)
                    peak_pos = sample;
                if (sample < peak_neg)
                    peak_neg = sample;
            }
            int peak_neg_mag = (peak_neg == INT16_MIN) ? INT16_MAX : -peak_neg;
            int current_peak = (peak_pos > peak_neg_mag) ? peak_pos : peak_neg_mag;
            int half_width = term_width / 2;
            int bar_left = (current_peak * half_width) / 32767;
            int bar_right = (current_peak * half_width) / 32767;
            if (bar_left > half_width) bar_left = half_width;
            if (bar_right > half_width) bar_right = half_width;
            memset(vis_line, ' ', term_width);
            vis_line[half_width] = '|';
            for (int j = 1; j <= bar_left; j++)
                vis_line[half_width - j] = '*';
            for (int j = 1; j <= bar_right; j++)
                vis_line[half_width + j] = '*';
            vis_line[term_width] = '\0';
            // Scroll graph buffer upward and append new line
            free(graph_lines[0]);
            for (int i = 1; i < graph_height; i++)
                graph_lines[i - 1] = graph_lines[i];
            graph_lines[graph_height - 1] = strdup(vis_line);
            if (!graph_lines[graph_height - 1]) {
                fprintf(stderr, "Error: strdup failed\n");
                break;
            }
        } else if (view_mode == 2) {
            /* --- FFT View --- */
            if (!fft_data || current_fft_size != fft_window_size) {
                free(fft_data);
                fft_data = malloc(fft_window_size * sizeof(int16_t));
                if (!fft_data) {
                    fprintf(stderr, "Error: malloc failed for fft_data\n");
                    break;
                }
                memset(fft_data, 0, fft_window_size * sizeof(int16_t));
                current_fft_size = fft_window_size;
            }
            // Slide fft_data and append new samples
            unsigned int new_samples = frames_read * channels;
            if (new_samples > fft_window_size)
                new_samples = fft_window_size;
            memmove(fft_data, fft_data + new_samples, (fft_window_size - new_samples) * sizeof(int16_t));
            memcpy(fft_data + (fft_window_size - new_samples), audio_buffer, new_samples * sizeof(int16_t));
            
            // Prepare FFT input
            complex double *fft_in = malloc(fft_window_size * sizeof(complex double));
            if (!fft_in) {
                fprintf(stderr, "Error: malloc failed for FFT input\n");
                break;
            }
            for (unsigned int i = 0; i < fft_window_size; i++)
                fft_in[i] = fft_data[i] + 0.0 * I;
            fft(fft_in, fft_window_size);
            int num_bins = fft_window_size / 2;
            int bins_per_col = (num_bins > term_width) ? num_bins / term_width : 1;
            double *col_magnitudes = calloc(term_width, sizeof(double));
            if (!col_magnitudes) {
                fprintf(stderr, "Error: calloc failed for col_magnitudes\n");
                free(fft_in);
                break;
            }
            for (int col = 0; col < term_width; col++) {
                double sum = 0.0;
                int start = col * bins_per_col;
                int end = start + bins_per_col;
                for (int k = start; k < end && k < num_bins; k++)
                    sum += cabs(fft_in[k]);
                col_magnitudes[col] = sum / bins_per_col;
            }
            free(fft_in);
            double max_val = 0.0;
            for (int j = 0; j < term_width; j++)
                if (col_magnitudes[j] > max_val)
                    max_val = col_magnitudes[j];
            // Use (graph_height - 1) rows for FFT bars; reserve last row for x-axis labels.
            for (int row = 0; row < graph_height - 1; row++) {
                for (int col = 0; col < term_width; col++) {
                    double norm = (max_val > 0.0) ? (col_magnitudes[col] / max_val) : 0.0;
                    int bar_height = (int)(norm * (graph_height - 1));
                    graph_lines[row][col] = ((graph_height - 1 - row) <= bar_height) ? '*' : ' ';
                }
                graph_lines[row][term_width] = '\0';
            }
            free(col_magnitudes);
            // Build x-axis labels
            memset(xaxis_line, '-', term_width);
            xaxis_line[term_width] = '\0';
            int num_labels = 5;
            for (int i = 0; i < num_labels; i++) {
                int pos = i * (term_width - 1) / (num_labels - 1);
                double freq = ((double)pos / (term_width - 1)) * (rate / 2.0);
                char label[16];
                snprintf(label, sizeof(label), "%.0fHz", freq);
                int label_len = strlen(label);
                if (pos + label_len > term_width)
                    pos = term_width - label_len;
                strncpy(&xaxis_line[pos], label, label_len);
            }
            strncpy(graph_lines[graph_height - 1], xaxis_line, term_width);
            graph_lines[graph_height - 1][term_width] = '\0';
        } else if (view_mode == 3) {
            /* --- Waterfall (Colored Spectrogram) View --- */
            if (!fft_data || current_fft_size != fft_window_size) {
                free(fft_data);
                fft_data = malloc(fft_window_size * sizeof(int16_t));
                if (!fft_data) {
                    fprintf(stderr, "Error: malloc failed for fft_data\n");
                    break;
                }
                memset(fft_data, 0, fft_window_size * sizeof(int16_t));
                current_fft_size = fft_window_size;
            }
            unsigned int new_samples = frames_read * channels;
            if (new_samples > fft_window_size)
                new_samples = fft_window_size;
            memmove(fft_data, fft_data + new_samples, (fft_window_size - new_samples) * sizeof(int16_t));
            memcpy(fft_data + (fft_window_size - new_samples), audio_buffer, new_samples * sizeof(int16_t));
            
            complex double *fft_in = malloc(fft_window_size * sizeof(complex double));
            if (!fft_in) {
                fprintf(stderr, "Error: malloc failed for FFT input\n");
                break;
            }
            for (unsigned int i = 0; i < fft_window_size; i++)
                fft_in[i] = fft_data[i] + 0.0 * I;
            fft(fft_in, fft_window_size);
            int num_bins = fft_window_size / 2;
            int bins_per_col = (num_bins > term_width) ? num_bins / term_width : 1;
            double *col_magnitudes = calloc(term_width, sizeof(double));
            if (!col_magnitudes) {
                fprintf(stderr, "Error: calloc failed for col_magnitudes\n");
                free(fft_in);
                break;
            }
            for (int col = 0; col < term_width; col++) {
                double sum = 0.0;
                int start = col * bins_per_col;
                int end = start + bins_per_col;
                for (int k = start; k < end && k < num_bins; k++)
                    sum += cabs(fft_in[k]);
                col_magnitudes[col] = sum / bins_per_col;
            }
            free(fft_in);
            double max_val = 0.0;
            for (int j = 0; j < term_width; j++)
                if (col_magnitudes[j] > max_val)
                    max_val = col_magnitudes[j];
            // Build a colored waterfall line.
            char waterfall_line[term_width * 20];
            waterfall_line[0] = '\0';
            for (int col = 0; col < term_width; col++) {
                double norm = (max_val > 0.0) ? (col_magnitudes[col] / max_val) : 0.0;
                const char *color;
                if (norm < 0.2)
                    color = "\033[34m"; // blue
                else if (norm < 0.4)
                    color = "\033[36m"; // cyan
                else if (norm < 0.6)
                    color = "\033[32m"; // green
                else if (norm < 0.8)
                    color = "\033[33m"; // yellow
                else
                    color = "\033[31m"; // red
                strcat(waterfall_line, color);
                strcat(waterfall_line, "█");
                strcat(waterfall_line, "\033[0m"); // reset color
            }
            free(col_magnitudes);
            // Scroll the graph buffer upward and append new waterfall line.
            free(graph_lines[0]);
            for (int i = 1; i < graph_height - 1; i++)
                graph_lines[i - 1] = graph_lines[i];
            graph_lines[graph_height - 2] = strdup(waterfall_line);
            if (!graph_lines[graph_height - 2]) {
                fprintf(stderr, "Error: strdup failed\n");
                break;
            }
            // Build x-axis labels as in FFT view.
            memset(xaxis_line, '-', term_width);
            xaxis_line[term_width] = '\0';
            int num_labels = 5;
            for (int i = 0; i < num_labels; i++) {
                int pos = i * (term_width - 1) / (num_labels - 1);
                double freq = ((double)pos / (term_width - 1)) * (rate / 2.0);
                char label[16];
                snprintf(label, sizeof(label), "%.0fHz", freq);
                int label_len = strlen(label);
                if (pos + label_len > term_width)
                    pos = term_width - label_len;
                strncpy(&xaxis_line[pos], label, label_len);
            }
            strncpy(graph_lines[graph_height - 1], xaxis_line, term_width);
            graph_lines[graph_height - 1][term_width] = '\0';
        } else if (view_mode == 4) {
            /* --- CSum Histogram View ---
               We only modify this part to clear leftover columns after drawing the bins.
            */
            if (!fft_data || current_fft_size != fft_window_size) {
                free(fft_data);
                fft_data = malloc(fft_window_size * sizeof(int16_t));
                if (!fft_data) {
                    fprintf(stderr, "Error: malloc failed for fft_data\n");
                    break;
                }
                memset(fft_data, 0, fft_window_size * sizeof(int16_t));
                current_fft_size = fft_window_size;
            }
            unsigned int new_samples = frames_read * channels;
            if (new_samples > fft_window_size)
                new_samples = fft_window_size;
            memmove(fft_data, fft_data + new_samples, (fft_window_size - new_samples) * sizeof(int16_t));
            memcpy(fft_data + (fft_window_size - new_samples), audio_buffer, new_samples * sizeof(int16_t));
            
            complex double *fft_in = malloc(fft_window_size * sizeof(complex double));
            if (!fft_in) {
                fprintf(stderr, "Error: malloc failed for FFT input\n");
                break;
            }
            for (unsigned int i = 0; i < fft_window_size; i++)
                fft_in[i] = fft_data[i] + 0.0 * I;
            fft(fft_in, fft_window_size);
            int num_bins = fft_window_size / 2;
            int bins_per_col = (num_bins > term_width) ? num_bins / term_width : 1;
            double *col_magnitudes = calloc(term_width, sizeof(double));
            if (!col_magnitudes) {
                fprintf(stderr, "Error: calloc failed for col_magnitudes\n");
                free(fft_in);
                break;
            }
            for (int col = 0; col < term_width; col++) {
                double sum = 0.0;
                int start = col * bins_per_col;
                int end = start + bins_per_col;
                for (int k = start; k < end && k < num_bins; k++)
                    sum += cabs(fft_in[k]);
                col_magnitudes[col] = sum / bins_per_col;
            }
            double max_val = 0.0;
            for (int j = 0; j < term_width; j++)
                if (col_magnitudes[j] > max_val)
                    max_val = col_magnitudes[j];
            // Build a colored waterfall_line (as in view_mode 3)
            char waterfall_line[term_width * 20];
            waterfall_line[0] = '\0';
            for (int col = 0; col < term_width; col++) {
                double norm = (max_val > 0.0) ? (col_magnitudes[col] / max_val) : 0.0;
                const char *color;
                if (norm < 0.2)
                    color = "\033[34m";
                else if (norm < 0.4)
                    color = "\033[36m";
                else if (norm < 0.6)
                    color = "\033[32m";
                else if (norm < 0.8)
                    color = "\033[33m";
                else
                    color = "\033[31m";
                strcat(waterfall_line, color);
                strcat(waterfall_line, "█");
                strcat(waterfall_line, "\033[0m");
            }
            free(col_magnitudes);
            free(fft_in);
            // Compute checksum from the generated waterfall_line
            unsigned long waterfall_checksum = 0;
            for (int j = 0; j < term_width; j++)
                waterfall_checksum += (unsigned char)waterfall_line[j];
            // Update running histogram based on the checksum.
            if (waterfall_checksum < hist_min) hist_min = waterfall_checksum;
            if (waterfall_checksum > hist_max) hist_max = waterfall_checksum;
            int bin = 0;
            if (hist_max > hist_min)
                bin = (int)(((waterfall_checksum - hist_min) * NUM_BINS) / (hist_max - hist_min + 1));
            if (bin < 0) bin = 0;
            if (bin >= NUM_BINS) bin = NUM_BINS - 1;
            current_hist[bin]++;
            current_hist_total++;

            // Prepare to display histogram.
            int bin_width = term_width / NUM_BINS;
            // Compute maximum count in any bin for normalization.
            unsigned long max_bin = 0;
            for (int i = 0; i < NUM_BINS; i++) {
                if (current_hist[i] > max_bin)
                    max_bin = current_hist[i];
            }
            // First row: error measure (if stored histogram is loaded).
            double error_measure = 0.0;
            if (stored_hist_loaded) {
                double diff = 0.0;
                for (int i = 0; i < NUM_BINS; i++) {
                    diff += fabs((double)current_hist[i] - (double)stored_hist[i]);
                }
                error_measure = diff;
            }
            snprintf(graph_lines[0], term_width + 1, "CSum Hist (Error: %.2f)", error_measure);

            // Fill rows 1 to (graph_height-1) with the histogram bars
            for (int row = 1; row < graph_height; row++) {
                for (int b = 0; b < NUM_BINS; b++) {
                    int bar_height = 0;
                    if (max_bin > 0)
                        bar_height = (int)((current_hist[b] * (graph_height - 1)) / max_bin);
                    // Draw bin b in the range of columns for this bin
                    for (int col = b * bin_width; col < (b + 1) * bin_width && col < term_width; col++) {
                        graph_lines[row][col] = ((graph_height - row) <= bar_height) ? '*' : ' ';
                    }
                }
                // >>> THIS IS THE NEW LOOP TO CLEAR REMAINING COLUMNS <<<
                // If NUM_BINS * bin_width < term_width, clear the leftover columns
                int used_cols = NUM_BINS * bin_width;
                for (int col = used_cols; col < term_width; col++) {
                    graph_lines[row][col] = ' ';
                }
                graph_lines[row][term_width] = '\0';
            }

            // Optionally, you can update the bottom row (x-axis) similar to other views.
            memset(xaxis_line, '-', term_width);
            xaxis_line[term_width] = '\0';
            strncpy(graph_lines[graph_height - 1], xaxis_line, term_width);
            graph_lines[graph_height - 1][term_width] = '\0';
        }

        // Compute dB level from waveform samples (for statistics)
        int16_t peak_pos = 0, peak_neg = 0;
        for (snd_pcm_sframes_t i = 0; i < frames_read * channels; i++) {
            int16_t sample = audio_buffer[i];
            if (sample > peak_pos)
                peak_pos = sample;
            if (sample < peak_neg)
                peak_neg = sample;
        }
        int peak_neg_mag = (peak_neg == INT16_MIN) ? INT16_MAX : -peak_neg;
        int current_peak = (peak_pos > peak_neg_mag) ? peak_pos : peak_neg_mag;
        double db_level = (current_peak > 0) ? 20.0 * log10((double)current_peak / 32767.0) : -100.0;
        // For non-histogram views, compute checksum from graph_lines.
        unsigned long checksum = 0;
        if (view_mode != 4) {
            for (int i = 0; i < graph_height; i++) {
                for (int j = 0; j < term_width; j++)
                    checksum += (unsigned char)graph_lines[i][j];
            }
        }
        // Build statistics line (second-to-last row)
        if (view_mode != 4) {
            snprintf(stats_line, term_width + 1,
                     "Dev:%s Rate:%uHz Per:%lu Ch:%u Fmt:S16_LE dB:%6.2f Csum:0x%08lx FFT_Win:%u",
                     device, rate, (unsigned long)period_size, channels, db_level, checksum, fft_window_size);
        } else {
            // In histogram view, display additional info.
            snprintf(stats_line, term_width + 1,
                     "Dev:%s Rate:%uHz Per:%lu Ch:%u Fmt:S16_LE dB:%6.2f FFT_Win:%u",
                     device, rate, (unsigned long)period_size, channels, db_level, fft_window_size);
        }
        // Build menu/instructions line (last row)
        const char *view_str = (view_mode == 1) ? "Waveform" : 
                               (view_mode == 2) ? "FFT" : 
                               (view_mode == 3) ? "Waterfall" : "CSumHist";
        snprintf(menu_line, term_width + 1,
                 "View:%s (Press 1:Waveform  2:FFT  3:Waterfall  4:CSumHist  8:Increase  9:Decrease FFT win  R:Reset  S:Save  L:Load)",
                 view_str);
        
        // Clear screen and reposition cursor to top-left
        printf("\033[H");
        for (int i = 0; i < graph_height; i++)
            printf("%s\n", graph_lines[i]);
        printf("%s\n", stats_line);
        printf("%s\n", menu_line);
        fflush(stdout);
    }

    // Cleanup: alternate screen restored via cleanup_alternate_screen()
    printf("\033[0m\nStopping capture.\n");
    snd_pcm_close(pcm_handle);
    free(audio_buffer);
    free(vis_line);
    for (int i = 0; i < graph_height; i++)
        free(graph_lines[i]);
    free(graph_lines);
    free(fft_data);
    return 0;
}
