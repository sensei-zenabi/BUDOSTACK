#define _POSIX_C_SOURCE 200809L  // Must be defined before any headers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <math.h>

static volatile sig_atomic_t stop_flag = 0;

void cleanup_alternate_screen(void) {
    // Disable alternate screen mode before exit
    printf("\033[?1049l");
    fflush(stdout);
}

void handle_sigint(int sig) {
    (void)sig;  // Suppress unused parameter warning
    stop_flag = 1;
}

int main(int argc, char *argv[]) {
    const char *device = "default";           // Default ALSA capture device
    unsigned int rate = 44100;                // Sample rate in Hz
    unsigned int channels = 1;                // Mono audio
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;  // 16-bit little-endian
    snd_pcm_t *pcm_handle = NULL;
    snd_pcm_hw_params_t *hw_params = NULL;
    int err;

    if (argc > 1) {
        device = argv[1];
    }

    // Set up signal handling and register cleanup function
    signal(SIGINT, handle_sigint);
    atexit(cleanup_alternate_screen);

    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Error: cannot open audio device '%s' (%s)\n",
                device, snd_strerror(err));
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

    // Obtain terminal dimensions (width and height)
    int term_width = 80;
    int term_height = 24;
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        if (ws.ws_col > 0) term_width = ws.ws_col;
        if (ws.ws_row > 0) term_height = ws.ws_row;
    }
    if (term_width % 2 == 0)
        term_width--;  // Ensure odd width for symmetry

    int graph_height = term_height - 2;  // Reserve bottom two rows for metrics

    // Allocate a graph buffer: an array of strings (one per graph row)
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
            for (int j = 0; j < i; j++)
                free(graph_lines[j]);
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

    // Temporary buffer for the new visualization line
    char *vis_line = malloc((term_width + 1) * sizeof(char));
    if (!vis_line) {
        fprintf(stderr, "Error: failed to allocate visualization line buffer\n");
        free(audio_buffer);
        for (int i = 0; i < graph_height; i++) free(graph_lines[i]);
        free(graph_lines);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Enable alternate screen mode so the output doesn't scroll offscreen
    printf("\033[?1049h");
    fflush(stdout);

    // Main capture and display loop
    while (!stop_flag) {
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

        // Compute peak amplitude over the current period
        int16_t peak_pos = 0;
        int16_t peak_neg = 0;
        for (snd_pcm_sframes_t i = 0; i < frames_read * channels; i++) {
            int16_t sample = audio_buffer[i];
            if (sample > peak_pos)
                peak_pos = sample;
            if (sample < peak_neg)
                peak_neg = sample;
        }
        int peak_neg_mag = (peak_neg == INT16_MIN) ? INT16_MAX : -peak_neg;
        int current_peak = (peak_pos > peak_neg_mag) ? peak_pos : peak_neg_mag;

        // Compute a horizontal bar (centered) based on the current peak
        int half_width = term_width / 2;
        int bar_left = (current_peak * half_width) / 32767;
        int bar_right = (current_peak * half_width) / 32767;
        if (bar_left > half_width)
            bar_left = half_width;
        if (bar_right > half_width)
            bar_right = half_width;

        // Build the visualization line: start with spaces, set center marker, and fill bars
        memset(vis_line, ' ', term_width);
        vis_line[half_width] = '|';
        for (int j = 1; j <= bar_left; j++) {
            vis_line[half_width - j] = '*';
        }
        for (int j = 1; j <= bar_right; j++) {
            vis_line[half_width + j] = '*';
        }
        vis_line[term_width] = '\0';

        // Scroll the graph buffer upward:
        free(graph_lines[0]);
        for (int i = 1; i < graph_height; i++) {
            graph_lines[i - 1] = graph_lines[i];
        }
        graph_lines[graph_height - 1] = strdup(vis_line);
        if (!graph_lines[graph_height - 1]) {
            fprintf(stderr, "Error: strdup failed\n");
            break;
        }

        // Compute dB level using 20*log10(current_peak / 32767)
        double db_level = (current_peak > 0) ? 20.0 * log10((double)current_peak / 32767.0) : -100.0;

        // Compute a simple checksum over the entire graph buffer (sum of character codes)
        unsigned long checksum = 0;
        for (int i = 0; i < graph_height; i++) {
            for (int j = 0; j < term_width; j++) {
                checksum += (unsigned char)graph_lines[i][j];
            }
        }

        // Clear the alternate screen and move the cursor to home position
        printf("\033[H");
        // Print the graph (scrolling region)
        for (int i = 0; i < graph_height; i++) {
            printf("%s\n", graph_lines[i]);
        }
        // Print metrics in the bottom two rows
        printf("dB level: %6.2f dB\n", db_level);
        printf("Checksum: 0x%08lx\n", checksum);
        fflush(stdout);
    }

    // Cleanup: alternate screen mode is disabled by cleanup_alternate_screen()
    printf("\033[0m\nStopping capture.\n");
    snd_pcm_close(pcm_handle);
    free(audio_buffer);
    free(vis_line);
    for (int i = 0; i < graph_height; i++) {
        free(graph_lines[i]);
    }
    free(graph_lines);
    return 0;
}
