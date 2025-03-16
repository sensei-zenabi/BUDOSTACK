/*
 * trend.c (Visualization Updated for Aligned Y-Axis, Statistical Grid Lines, and UI Instructions)
 *
 * This version of the terminal trend tool improves the visualization by:
 *   1. Aligning the y‑axis labels using a fixed width computed from the largest number.
 *   2. Drawing grid lines corresponding to the mean and ±1 standard deviation (std)
 *      of the visible samples from active channels.
 *   3. Including UI instructions so that the available key functions are visible.
 *
 * Note: The recording functionality (triggered by key 'R') has been removed.
 *
 * Compile with: cc -std=c11 -pthread trend.c -o trend
 *
 * Usage:
 *   ./trend [hostname] [port]
 *   (Defaults: hostname = "localhost", port = 12345)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <threads.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

/* For networking */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* For terminal control */
#include <termios.h>

#define NUM_TRENDS      5
#define MAX_SAMPLES     1200    // Maximum samples per channel (e.g. 120 sec @ 10 Hz)
#define DISPLAY_WIDTH   80
#define DISPLAY_HEIGHT  20
#define SAMPLE_RATE     10      // nominal samples per second (used for display timing)
#define DT              (1.0 / SAMPLE_RATE)

// ANSI color codes for channels 1-5.
const char *channel_colors[NUM_TRENDS] = {
    "\033[31m", // Red
    "\033[32m", // Green
    "\033[33m", // Yellow
    "\033[34m", // Blue
    "\033[35m"  // Magenta
};
#define COLOR_RESET "\033[0m"

// Global variable to store original terminal settings.
struct termios orig_termios;

// Disable canonical mode and echo to process individual key presses.
void disable_input_buffering(void) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);  // disable canonical mode and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

// Restore original terminal settings.
void restore_input_buffering(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// Enable alternate screen buffer to avoid cluttering the main terminal.
void enable_alternate_screen(void) {
    printf("\033[?1049h");
}

// Restore main screen from alternate screen buffer.
void disable_alternate_screen(void) {
    printf("\033[?1049l");
}

// Helper function: return current wall‑clock time (in seconds with fractions)
double get_wallclock_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    double t;       // Timestamp (in seconds relative to program start)
    double value;   // Sampled value
} Sample;

typedef struct {
    Sample samples[MAX_SAMPLES];
    int head;       // Next write index (circular buffer)
    int count;      // Number of valid samples
} TrendBuffer;

/* Global state and configuration */
volatile bool run = true;         // Set to false to exit

int time_window = 30;             // Time window (sec) for display; default 30, range 5..120
// All 5 channels enabled by default
bool trend_active[NUM_TRENDS] = { true, true, true, true, true };

TrendBuffer trends[NUM_TRENDS];
mtx_t lock;   // Mutex to protect shared state (trend buffers, config, etc.)

/* Networking globals */
int sock_fd = -1;  // Socket file descriptor for server connection

/*
 * add_sample:
 * Adds a new sample (timestamp and value) to the circular buffer for a channel.
 */
void add_sample(TrendBuffer *buffer, double t, double value) {
    buffer->samples[buffer->head].t = t;
    buffer->samples[buffer->head].value = value;
    buffer->head = (buffer->head + 1) % MAX_SAMPLES;
    if (buffer->count < MAX_SAMPLES)
        buffer->count++;
}

/*
 * network_thread:
 * Connects to the server and continuously reads lines.
 * Expected line format (routed by server.c): "inN from clientX: <value>"
 * On successful parse, the sample is added to the corresponding trend buffer.
 */
int network_thread(void *arg) {
    (void)arg; // unused
    char buf[1024];
    int buf_used = 0;
    while (run) {
        ssize_t n = recv(sock_fd, buf + buf_used, sizeof(buf) - buf_used - 1, 0);
        if (n <= 0) {
            if(n < 0)
                perror("recv");
            else
                fprintf(stderr, "Server closed connection.\n");
            run = false;
            break;
        }
        buf_used += n;
        buf[buf_used] = '\0';
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';  // Terminate the line
            // Process the line if it begins with "in"
            if (strncmp(line_start, "in", 2) == 0) {
                int in_ch = -1;
                int dummy_client = -1;
                double value = 0;
                /* Expected format: "in%d from client%d: %lf" */
                if (sscanf(line_start, "in%d from client%d: %lf", &in_ch, &dummy_client, &value) == 3) {
                    if (in_ch >= 0 && in_ch < NUM_TRENDS) {
                        double current_time = get_wallclock_time();
                        mtx_lock(&lock);
                        add_sample(&trends[in_ch], current_time, value);
                        mtx_unlock(&lock);
                    }
                }
            }
            line_start = newline + 1;
        }
        // Move any remaining partial line to beginning of buffer
        int remaining = buf_used - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_used = remaining;
    }
    return 0;
}

/*
 * input_thread:
 * Reads keyboard input from stdin (in non‑canonical mode).
 * Toggles channels (keys '1'-'5') and adjusts time window (keys '8' and '9').
 */
int input_thread(void *arg) {
    (void)arg; // unused
    int ch;
    while (run) {
        ch = getchar();
        if (ch != EOF) {
            mtx_lock(&lock);
            if (ch >= '1' && ch <= '5') {
                int idx = ch - '1';
                trend_active[idx] = !trend_active[idx];
            } else if (ch == '8') {
                if (time_window + 5 <= 120)
                    time_window += 5;
            } else if (ch == '9') {
                if (time_window - 5 >= 5)
                    time_window -= 5;
            }
            mtx_unlock(&lock);
        }
    }
    return 0;
}

/*
 * clear_screen:
 * Clears the terminal using ANSI escape sequences.
 */
void clear_screen() {
    // Use alternate screen buffer to avoid cluttering the main terminal.
    printf("\033[2J\033[H");
}

/*
 * display_trends:
 * Draws the ASCII graph for the 5 channels with improved visualization.
 * Changes made:
 *   - Y‑axis labels are printed with a fixed width to ensure the vertical line stays aligned.
 *   - Grid lines are drawn at the mean and ±1 std positions computed from visible samples.
 *   - UI instructions are printed at the bottom so the user sees available controls.
 */
void display_trends() {
    clear_screen();
    double current_time = get_wallclock_time();
    double t_min = current_time - time_window;

    // Compute global min/max from active channels over the time window.
    double global_min = 1e9, global_max = -1e9;
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        if (!trend_active[ch]) continue;
        TrendBuffer *buf = &trends[ch];
        for (int i = 0; i < buf->count; i++) {
            int idx = (buf->head + MAX_SAMPLES - buf->count + i) % MAX_SAMPLES;
            double sample_time = buf->samples[idx].t;
            if (sample_time >= t_min && sample_time <= current_time) {
                double v = buf->samples[idx].value;
                if (v < global_min) global_min = v;
                if (v > global_max) global_max = v;
            }
        }
    }
    if (global_min == 1e9 || global_max == -1e9) {
        global_min = 0;
        global_max = 100;
    }
    if (global_min == global_max) {
        global_min -= 1;
        global_max += 1;
    }
    // Add ±10% buffer for display scaling.
    double range = global_max - global_min;
    global_min -= 0.1 * range;
    global_max += 0.1 * range;

    // Calculate mean and standard deviation of visible samples.
    double sum = 0.0, sum_sq = 0.0;
    int sample_count = 0;
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        if (!trend_active[ch]) continue;
        TrendBuffer *buf = &trends[ch];
        for (int i = 0; i < buf->count; i++) {
            int idx = (buf->head + MAX_SAMPLES - buf->count + i) % MAX_SAMPLES;
            double sample_time = buf->samples[idx].t;
            if (sample_time >= t_min && sample_time <= current_time) {
                double v = buf->samples[idx].value;
                sum += v;
                sum_sq += v * v;
                sample_count++;
            }
        }
    }
    double mean = (sample_count > 0) ? (sum / sample_count) : ((global_min + global_max) / 2);
    double std = 0.0;
    if (sample_count > 0) {
        double variance = (sum_sq / sample_count) - (mean * mean);
        std = (variance > 0) ? sqrt(variance) : 0;
    }
    
    // Compute row indices for mean and ±1 std.
    int row_mean = (int)((global_max - mean) / (global_max - global_min) * (DISPLAY_HEIGHT - 1));
    int row_p1 = (int)((global_max - (mean + std)) / (global_max - global_min) * (DISPLAY_HEIGHT - 1));
    int row_m1 = (int)((global_max - (mean - std)) / (global_max - global_min) * (DISPLAY_HEIGHT - 1));

    // Create a display buffer for the graph (initialize with spaces).
    char display[DISPLAY_HEIGHT][DISPLAY_WIDTH + 1];
    for (int r = 0; r < DISPLAY_HEIGHT; r++) {
        for (int c = 0; c < DISPLAY_WIDTH; c++) {
            display[r][c] = ' ';
        }
        display[r][DISPLAY_WIDTH] = '\0';
    }
    
    // Overlay grid lines for mean and ±1 std.
    if (row_mean >= 0 && row_mean < DISPLAY_HEIGHT) {
        for (int c = 0; c < DISPLAY_WIDTH; c++) {
            display[row_mean][c] = '=';
        }
    }
    if (row_p1 >= 0 && row_p1 < DISPLAY_HEIGHT) {
        for (int c = 0; c < DISPLAY_WIDTH; c++) {
            display[row_p1][c] = '-';
        }
    }
    if (row_m1 >= 0 && row_m1 < DISPLAY_HEIGHT) {
        for (int c = 0; c < DISPLAY_WIDTH; c++) {
            display[row_m1][c] = '-';
        }
    }
    
    // Plot each active channel's samples onto the display buffer.
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        if (!trend_active[ch]) continue;
        TrendBuffer *buf = &trends[ch];
        for (int i = 0; i < buf->count; i++) {
            int idx = (buf->head + MAX_SAMPLES - buf->count + i) % MAX_SAMPLES;
            double t_sample = buf->samples[idx].t;
            if (t_sample < t_min || t_sample > current_time)
                continue;
            double value = buf->samples[idx].value;
            int col = (int)((t_sample - t_min) / time_window * (DISPLAY_WIDTH - 1));
            int row = (int)((global_max - value) / (global_max - global_min) * (DISPLAY_HEIGHT - 1));
            if (col >= 0 && col < DISPLAY_WIDTH && row >= 0 && row < DISPLAY_HEIGHT)
                display[row][col] = '1' + ch;
        }
    }
    
    // Compute fixed width for the y-axis labels (using global extremes).
    char buf_min[32], buf_max[32];
    snprintf(buf_min, sizeof(buf_min), "%.2f", global_min);
    snprintf(buf_max, sizeof(buf_max), "%.2f", global_max);
    int label_width = (int) (strlen(buf_min) > strlen(buf_max) ? strlen(buf_min) : strlen(buf_max));

    // Print the graph with y-axis labels.
    for (int r = 0; r < DISPLAY_HEIGHT; r++) {
        double y_value = global_max - (global_max - global_min) * r / (DISPLAY_HEIGHT - 1);
        // Use fixed width to align the vertical axis.
        printf("%*.*f | ", label_width, 2, y_value);
        for (int c = 0; c < DISPLAY_WIDTH; c++) {
            char ch = display[r][c];
            if (ch >= '1' && ch <= '5') {
                int channel = ch - '1';
                printf("%s%c%s", channel_colors[channel], ch, COLOR_RESET);
            } else {
                printf("%c", ch);
            }
        }
        printf("\n");
    }
    // Draw the x-axis.
    printf("%*s +", label_width, "");
    for (int c = 0; c < DISPLAY_WIDTH; c++) {
        printf("-");
    }
    printf("\n");
    // Print time labels.
    printf("%*s ", label_width, "");
    printf("%-6.1f", t_min);
    for (int c = 0; c < DISPLAY_WIDTH - 12; c++) {
        printf(" ");
    }
    printf("%6.1f\n", current_time);

    // Additional info: time window and active channels.
    printf("Time window: %d sec. Active trends: ", time_window);
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        if (trend_active[ch])
            printf("%s%d%s ", channel_colors[ch], ch + 1, COLOR_RESET);
    }
    printf("\n");
    
    // Display statistical info.
    printf("Mean: %.2f, Std: %.2f\n", mean, std);
    // Display legend for channel colors.
    printf("Legend: ");
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        printf("%s%d%s ", channel_colors[ch], ch + 1, COLOR_RESET);
    }
    printf("\n");
    
    // UI instructions for available key functions.
    printf("Controls: 1-5: Toggle channels, 8: Increase time window, 9: Decrease time window, Ctrl+C: Exit\n");
}

/*
 * handle_sigint:
 * Signal handler to gracefully exit on SIGINT (Ctrl+C).
 */
void handle_sigint(int sig) {
    (void)sig;
    run = false;
}

/*
 * connect_to_server:
 * Connects to the server at the given hostname and port.
 * Returns a socket file descriptor on success, or -1 on failure.
 */
int connect_to_server(const char *hostname, const char *port) {
    struct addrinfo hints, *servinfo, *p;
    int rv, s;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_STREAM;
    
    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            close(s);
            perror("connect");
            continue;
        }
        break;
    }
    
    if (p == NULL) {
        fprintf(stderr, "Failed to connect to %s:%s\n", hostname, port);
        s = -1;
    }
    
    freeaddrinfo(servinfo);
    return s;
}

/*
 * main:
 * - Connects to the server.
 * - Initializes buffers and synchronization primitives.
 * - Puts the terminal into non‑canonical mode.
 * - Enables the alternate screen buffer.
 * - Spawns network and keyboard input threads.
 * - Enters a loop that periodically refreshes the display.
 */
int main(int argc, char *argv[]) {
    const char *hostname = "localhost";
    const char *port = "12345";
    
    if (argc >= 2)
        hostname = argv[1];
    if (argc >= 3)
        port = argv[2];
    
    sock_fd = connect_to_server(hostname, port);
    if (sock_fd < 0) {
        exit(EXIT_FAILURE);
    }
    
    // Read server greeting (if any) and print it.
    char greet[256];
    ssize_t n = recv(sock_fd, greet, sizeof(greet)-1, 0);
    if(n > 0) {
        greet[n] = '\0';
        printf("%s", greet);
    }
    
    // Initialize trend buffers.
    for (int ch = 0; ch < NUM_TRENDS; ch++) {
        trends[ch].head = 0;
        trends[ch].count = 0;
    }
    
    if (mtx_init(&lock, mtx_plain) != thrd_success) {
        fprintf(stderr, "Mutex init failed\n");
        return EXIT_FAILURE;
    }
    
    // Set terminal to non‑canonical mode to process key presses immediately.
    disable_input_buffering();
    // Enable alternate screen buffer for improved display.
    enable_alternate_screen();
    
    // Ensure terminal settings are restored on exit.
    atexit(restore_input_buffering);
    atexit(disable_alternate_screen);
    
    signal(SIGINT, handle_sigint);
    
    thrd_t tid_input, tid_net;
    if (thrd_create(&tid_input, input_thread, NULL) != thrd_success) {
        fprintf(stderr, "Failed to create input thread\n");
        return EXIT_FAILURE;
    }
    if (thrd_create(&tid_net, network_thread, NULL) != thrd_success) {
        fprintf(stderr, "Failed to create network thread\n");
        return EXIT_FAILURE;
    }
    
    // Main display loop.
    while (run) {
        mtx_lock(&lock);
        display_trends();
        mtx_unlock(&lock);
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (long)(DT * 1e9);
        thrd_sleep(&ts, NULL);
    }
    
    // Cleanup
    close(sock_fd);
    mtx_destroy(&lock);
    return EXIT_SUCCESS;
}
