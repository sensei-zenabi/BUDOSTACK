#define _POSIX_C_SOURCE 200809L  // Define before any includes to avoid redefinition issues

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>

static volatile sig_atomic_t stop_flag = 0;

void handle_sigint(int sig) {
    (void)sig;  // Avoid unused parameter warning
    stop_flag = 1;
}

int main(int argc, char *argv[]) {
    const char *device = "default";           // Default ALSA capture device
    unsigned int rate = 44100;                // Sample rate (Hz)
    unsigned int channels = 1;                // Mono audio
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;  // 16-bit little-endian
    snd_pcm_t *pcm_handle = NULL;
    snd_pcm_hw_params_t *hw_params = NULL;
    int err;

    // If a device name is provided as argument, use it
    if (argc > 1) {
        device = argv[1];
    }

    // Handle Ctrl+C gracefully
    signal(SIGINT, handle_sigint);

    // Open the ALSA PCM device for capturing (recording)
    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Error: cannot open audio device '%s' (%s)\n",
                device, snd_strerror(err));
        return 1;
    }

    // Allocate hardware parameters object
    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "Error: cannot allocate HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Initialize HW parameters with default values
    if ((err = snd_pcm_hw_params_any(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot initialize HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Set the desired hardware parameters
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

    // (Optional) Set period size for low latency
    // We request a period of 1024 frames.
    snd_pcm_uframes_t period_size = 1024;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params, &period_size, NULL)) < 0) {
        fprintf(stderr, "Warning: cannot set period size (%s). Using default.\n", snd_strerror(err));
        // Not fatal, continue with default period if not supported
    }

    // Write the parameters to the driver
    if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot set HW parameters (%s)\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw_params);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Parameters set, free the params structure
    snd_pcm_hw_params_free(hw_params);

    // Prepare PCM for use (not always needed, but good practice)
    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "Error: cannot prepare audio interface (%s)\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Determine the output width (terminal width or default 80)
    int term_width = 80;
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        if (ws.ws_col > 0) {
            term_width = ws.ws_col;
        }
    }
    // Ensure we have an odd width for a symmetric graph (one center column)
    if (term_width % 2 == 0) {
        term_width -= 1;
    }
    if (term_width < 1) term_width = 1;
    int half_width = term_width / 2;  // number of chars on each side of center
    char *vis_buffer = malloc((term_width + 1) * sizeof(char));
    if (!vis_buffer) {
        fprintf(stderr, "Error: failed to allocate visualization buffer\n");
        snd_pcm_close(pcm_handle);
        return 1;
    }

    // Allocate audio buffer for one period of frames
    snd_pcm_uframes_t frames = period_size;
    int16_t *audio_buffer = malloc(frames * channels * snd_pcm_format_width(format) / 8);
    if (!audio_buffer) {
        fprintf(stderr, "Error: failed to allocate audio buffer\n");
        free(vis_buffer);
        snd_pcm_close(pcm_handle);
        return 1;
    }

    fprintf(stdout, "Capturing audio... Press Ctrl+C to stop.\n");

    // Capture and display loop
    while (!stop_flag) {
        snd_pcm_sframes_t frames_read = snd_pcm_readi(pcm_handle, audio_buffer, frames);
        if (frames_read < 0) {
            // If read error, attempt to recover from overruns/interrupts
            if ((err = snd_pcm_recover(pcm_handle, frames_read, 0)) < 0) {
                fprintf(stderr, "Error: audio capture failed (%s)\n", snd_strerror(err));
                break;  // unrecoverable error
            }
            continue;
        }
        if (frames_read == 0) {
            continue;
        }

        // Calculate peak positive and negative amplitudes in the captured chunk
        int16_t peak_pos = 0;
        int16_t peak_neg = 0;
        for (snd_pcm_sframes_t i = 0; i < frames_read * channels; ++i) {
            int16_t sample = audio_buffer[i];
            if (sample > peak_pos) peak_pos = sample;
            if (sample < peak_neg) peak_neg = sample;
        }
        int peak_neg_mag = (peak_neg == INT16_MIN) ? INT16_MAX : -peak_neg;
        if (peak_neg_mag < 0) peak_neg_mag = 0;

        // Determine how many characters to draw on each side
        int bar_left = (peak_neg_mag * half_width) / 32767;
        int bar_right = (peak_pos * half_width) / 32767;
        if (bar_left > half_width) bar_left = half_width;
        if (bar_right > half_width) bar_right = half_width;

        // Build the visualization line
        memset(vis_buffer, ' ', term_width);
        vis_buffer[half_width] = '|';  // center marker
        for (int j = 1; j <= bar_left; ++j) {
            vis_buffer[half_width - j] = '*';
        }
        for (int j = 1; j <= bar_right; ++j) {
            vis_buffer[half_width + j] = '*';
        }
        vis_buffer[term_width] = '\0';

        printf("%s\n", vis_buffer);
        fflush(stdout);
    }

    fprintf(stdout, "\nStopping capture.\n");
    snd_pcm_close(pcm_handle);
    free(audio_buffer);
    free(vis_buffer);
    return 0;
}
