/*
 * apps/input_video.c
 *
 * Advanced ANSI Camera App with Performance Modes, FPS Adjustment, Low Latency,
 * and Toggle-able Object Detection.
 *
 * Design Principles:
 *   - Written in plain C (-std=c11) using only standard cross-platform libraries.
 *   - Single-file implementation (no additional header files).
 *   - Uses ANSI 24-bit color escape codes.
 *   - Adapts to the current terminal size.
 *   - Processes keyboard input in raw mode (non-canonical, no echo) for mode toggling.
 *   - Implements asynchronous frame capture using POSIX threads.
 *   - Adds a new toggle for object detection via the 'D' key.
 *
 * Modification:
 *   - Establishes a TCP connection to a server (default 127.0.0.1:12345 or as passed in via argv).
 *   - Sends the x and y coordinates (from object detection) as messages in the format:
 *         "out0: <x>\n"  and  "out1: <y>\n"
 *     mimicking the client template implementation.
 *
 * Compilation:
 *   cc -std=c11 -Wall -Wextra -pedantic -pthread -o apps/input_video apps/input_video.c object_recognition.c
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>         // For read, write, close, etc.
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>           // For nanosleep, clock_gettime
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/select.h>
#include <math.h>
#include <pthread.h>
#include <arpa/inet.h>      // For inet_pton, htons, etc.
#include <sys/socket.h>     // For socket functions
#include <ctype.h>

// ---------------------- TCP Client Constants ----------------------
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 12345
#define TCP_BUFFER_SIZE 128

// ---------------------- Global Variables ----------------------

// Graceful exit flag.
volatile sig_atomic_t stop = 0;

// Global quality mode (1: fast, 2: balanced, 3: quality)
volatile int quality_mode = 1;

// Global target FPS, initial value 10.
volatile int target_fps = 10;

// Global flag to enable/disable object detection.
// 1 = enabled, 0 = disabled.
volatile int object_detection_enabled = 1;

// Terminal raw mode original settings.
struct termios orig_termios;

// Shared frame buffer for captured frame.
// This buffer is updated by the capture thread and read by the rendering (main) thread.
unsigned char *shared_frame = NULL;
size_t shared_frame_size = 0;
int frame_ready = 0;  // 1 if a new frame is available

// Mutex and condition variable for synchronizing frame access.
pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t frame_cond = PTHREAD_COND_INITIALIZER;

// ---------------------- V4L2 Buffer Structures ----------------------

struct buffer {
    void   *start;
    size_t  length;
};

// Define frame dimensions.
#define FRAME_WIDTH  320
#define FRAME_HEIGHT 240

// ---------------------- Signal & Raw Mode Handling ----------------------

void handle_sigint(int sig) {
    (void)sig;
    stop = 1;
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// ---------------------- Time Utilities ----------------------

static void sleep_microseconds(useconds_t usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// ---------------------- Terminal Utilities ----------------------

void clear_terminal(void) {
    // Move cursor to home and clear screen.
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

void get_terminal_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        *cols = 80;
        *rows = 24;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

// ---------------------- YUYV to RGB Conversion ----------------------

// Converts a single YUYV pixel pair to RGB values.
void yuyv_to_rgb(const unsigned char *frame, int frame_width,
                 int x, int y, unsigned char *r, unsigned char *g, unsigned char *b) {
    int pair_index = x / 2;
    int base = (y * frame_width + pair_index * 2) * 2;
    unsigned char Y;
    unsigned char U = frame[base + 1];
    unsigned char V = frame[base + 3];
    if ((x & 1) == 0)
        Y = frame[base];
    else
        Y = frame[base + 2];
    int C = (int)Y - 16;
    int D = (int)U - 128;
    int E = (int)V - 128;
    int R_temp = (298 * C + 409 * E + 128) >> 8;
    int G_temp = (298 * C - 100 * D - 208 * E + 128) >> 8;
    int B_temp = (298 * C + 516 * D + 128) >> 8;
    if (R_temp < 0)
        R_temp = 0;
    if (R_temp > 255)
        R_temp = 255;
    if (G_temp < 0)
        G_temp = 0;
    if (G_temp > 255)
        G_temp = 255;
    if (B_temp < 0)
        B_temp = 0;
    if (B_temp > 255)
        B_temp = 255;
    *r = (unsigned char)R_temp;
    *g = (unsigned char)G_temp;
    *b = (unsigned char)B_temp;
}

void get_rgb_average(const unsigned char *frame, int frame_width, int frame_height,
                     double fx, double fy, unsigned char *r, unsigned char *g, unsigned char *b) {
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    int x1 = (x0 < frame_width - 1) ? x0 + 1 : x0;
    int y1 = (y0 < frame_height - 1) ? y0 + 1 : y0;
    unsigned char r00, g00, b00;
    unsigned char r10, g10, b10;
    unsigned char r01, g01, b01;
    unsigned char r11, g11, b11;
    yuyv_to_rgb(frame, frame_width, x0, y0, &r00, &g00, &b00);
    yuyv_to_rgb(frame, frame_width, x1, y0, &r10, &g10, &b10);
    yuyv_to_rgb(frame, frame_width, x0, y1, &r01, &g01, &b01);
    yuyv_to_rgb(frame, frame_width, x1, y1, &r11, &g11, &b11);
    *r = (r00 + r10 + r01 + r11) / 4;
    *g = (g00 + g10 + g01 + g11) / 4;
    *b = (b00 + b10 + b01 + b11) / 4;
}

void get_rgb_bilinear(const unsigned char *frame, int frame_width, int frame_height,
                      double fx, double fy, unsigned char *r, unsigned char *g, unsigned char *b) {
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    int x1 = (x0 < frame_width - 1) ? x0 + 1 : x0;
    int y1 = (y0 < frame_height - 1) ? y0 + 1 : y0;
    double wx = fx - x0, wy = fy - y0;
    unsigned char r00, g00, b00;
    unsigned char r10, g10, b10;
    unsigned char r01, g01, b01;
    unsigned char r11, g11, b11;
    yuyv_to_rgb(frame, frame_width, x0, y0, &r00, &g00, &b00);
    yuyv_to_rgb(frame, frame_width, x1, y0, &r10, &g10, &b10);
    yuyv_to_rgb(frame, frame_width, x0, y1, &r01, &g01, &b01);
    yuyv_to_rgb(frame, frame_width, x1, y1, &r11, &g11, &b11);
    double R = (1 - wx) * (1 - wy) * r00 + wx * (1 - wy) * r10 +
               (1 - wx) * wy * r01 + wx * wy * r11;
    double G = (1 - wx) * (1 - wy) * g00 + wx * (1 - wy) * g10 +
               (1 - wx) * wy * g01 + wx * wy * g11;
    double B = (1 - wx) * (1 - wy) * b00 + wx * (1 - wy) * b10 +
               (1 - wx) * wy * b01 + wx * wy * b11;
    if (R < 0)
        R = 0;
    if (R > 255)
        R = 255;
    if (G < 0)
        G = 0;
    if (G > 255)
        G = 255;
    if (B < 0)
        B = 0;
    if (B > 255)
        B = 255;
    *r = (unsigned char)R;
    *g = (unsigned char)G;
    *b = (unsigned char)B;
}

// ---------------------- Precomputed Scaling Arrays ----------------------
//
// These arrays map terminal column/row indices to frame pixel coordinates.
double *fx_arr = NULL;
double *fy_top_arr = NULL;
double *fy_bot_arr = NULL;

// ---------------------- ASCII Rendering with Batch Output ----------------------
//
// Renders the frame (YUYV) into the pre-allocated output buffer.
void frame_to_halfblock_ascii(const unsigned char *frame, int frame_width, int frame_height,
                                int term_cols, int term_rows, int quality, char *output_buf, size_t buf_size) {
    size_t pos = 0;
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            double fx = fx_arr[col];
            double fy_top = fy_top_arr[row];
            double fy_bot = fy_bot_arr[row];
            unsigned char top_r, top_g, top_b;
            unsigned char bot_r, bot_g, bot_b;
            if (quality == 1) {
                yuyv_to_rgb(frame, frame_width, (int)fx, (int)fy_top, &top_r, &top_g, &top_b);
                yuyv_to_rgb(frame, frame_width, (int)fx, (int)fy_bot, &bot_r, &bot_g, &bot_b);
            } else if (quality == 2) {
                get_rgb_average(frame, frame_width, frame_height, fx, fy_top, &top_r, &top_g, &top_b);
                get_rgb_average(frame, frame_width, frame_height, fx, fy_bot, &bot_r, &bot_g, &bot_b);
            } else if (quality == 3) {
                get_rgb_bilinear(frame, frame_width, frame_height, fx, fy_top, &top_r, &top_g, &top_b);
                get_rgb_bilinear(frame, frame_width, frame_height, fx, fy_bot, &bot_r, &bot_g, &bot_b);
            } else {
                yuyv_to_rgb(frame, frame_width, (int)fx, (int)fy_top, &top_r, &top_g, &top_b);
                yuyv_to_rgb(frame, frame_width, (int)fx, (int)fy_bot, &bot_r, &bot_g, &bot_b);
            }
            pos += snprintf(output_buf + pos, buf_size - pos,
                            "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀",
                            top_r, top_g, top_b, bot_r, bot_g, bot_b);
        }
        pos += snprintf(output_buf + pos, buf_size - pos, "\033[0m\n");
    }
    if (pos < buf_size)
        output_buf[pos] = '\0';
}

// ---------------------- Helper: Visible Length Computation ----------------------
//
// This function computes the visible length of a string by skipping ANSI escape codes.
int visible_length(const char *str) {
    int len = 0;
    while (*str) {
        if (*str == '\033') {  // Start of an escape sequence.
            while (*str && *str != 'm') {
                str++;
            }
            if (*str == 'm')
                str++;
        } else {
            len++;
            str++;
        }
    }
    return len;
}

// ---------------------- Menu Bar Drawing ----------------------
//
// Draws a menu bar at the bottom of the terminal with current mode, FPS, target FPS,
// object detection status, and displays the latest outputs.
void draw_menu_bar(double fps, int term_cols, int term_rows, int out0, int out1) {
    char menu[512];
    snprintf(menu, sizeof(menu),
             "\033[%d;1H\033[7m Mode: %d  FPS: %.1f  Target: %d  [Press 1: Fast, 2: Balanced, 3: Quality, 8: - FPS, 9: + FPS, D: ObjDetect %s]  Out0: %d, Out1: %d",
             term_rows,
             quality_mode,
             fps,
             target_fps,
             object_detection_enabled ? "On" : "Off",
             out0,
             out1);
    
    int vis_len = visible_length(menu);
    while (vis_len < term_cols && strlen(menu) < sizeof(menu) - 2) {
        strcat(menu, " ");
        vis_len++;
    }
    strcat(menu, "\033[0m");
    write(STDOUT_FILENO, menu, strlen(menu));
    fflush(stdout);
}

// ---------------------- Input Processing ----------------------
//
// Processes keyboard input and updates global states.
void process_input() {
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '1' || c == '2' || c == '3') {
                quality_mode = c - '0';
            } else if (c == '8') {
                if (target_fps > 1)
                    target_fps--;
            } else if (c == '9') {
                target_fps++;
            } else if (c == 'D' || c == 'd') {
                object_detection_enabled = !object_detection_enabled;
            }
        }
    }
}

// ---------------------- V4L2 Capture Thread ----------------------

// Context structure for capture thread.
struct capture_context {
    int fd;
    struct buffer *buffers;
    unsigned int n_buffers;
};

void *capture_thread_func(void *arg) {
    struct capture_context *ctx = (struct capture_context *)arg;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    
    while (!stop) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Dequeue Buffer");
            continue;
        }
        pthread_mutex_lock(&frame_mutex);
        memcpy(shared_frame, ctx->buffers[buf.index].start, buf.bytesused);
        shared_frame_size = buf.bytesused;
        frame_ready = 1;
        pthread_cond_signal(&frame_cond);
        pthread_mutex_unlock(&frame_mutex);
        
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Requeue Buffer");
        }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Stop Capture");
    }
    return NULL;
}

// ---------------------- External Object Recognition ----------------------
//
// Forward-declare the Position structure (matching object_recognition.c) and
// update the extern declaration so that process_frame() returns a Position.
typedef struct {
    int x;
    int y;
} Position;

extern Position process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height);

// ---------------------- Global Variables for TCP Connection ----------------------
int tcp_sockfd = -1;  // TCP socket file descriptor

// ---------------------- Main Function ----------------------
//
// Now accepts optional command-line arguments for server IP and port.
// Also sends object detection outputs to the TCP server as "out0:" and "out1:" messages.
int main(int argc, char *argv[]) {
    int fd;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct buffer *buffers;
    unsigned int n_buffers;
    int term_cols, term_rows;
    const char *server_ip = DEFAULT_SERVER_IP;
    unsigned short server_port = DEFAULT_SERVER_PORT;
    
    // Allow overriding server IP and port via command-line arguments.
    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = (unsigned short)atoi(argv[2]);
        if (server_port == 0) {
            server_port = DEFAULT_SERVER_PORT;
        }
    }
    
    setbuf(stdout, NULL);
    
    enable_raw_mode();
    atexit(disable_raw_mode);
    signal(SIGINT, handle_sigint);
    
    // Establish TCP connection to the server.
    tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        perror("TCP socket");
        // Continue without TCP if connection fails.
    } else {
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
            fprintf(stderr, "Invalid server IP: %s\n", server_ip);
            close(tcp_sockfd);
            tcp_sockfd = -1;
        } else if (connect(tcp_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("TCP connect");
            close(tcp_sockfd);
            tcp_sockfd = -1;
        }
    }
    
    fd = open("/dev/video0", O_RDWR);
    if (fd == -1) {
        perror("Opening video device");
        return EXIT_FAILURE;
    }
    
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = FRAME_WIDTH;
    fmt.fmt.pix.height      = FRAME_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Setting Pixel Format");
        close(fd);
        return EXIT_FAILURE;
    }
    
#ifdef V4L2_CID_LOW_LATENCY
    {
        struct v4l2_control ctrl;
        ctrl.id = V4L2_CID_LOW_LATENCY;
        ctrl.value = 1;
        if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
            perror("Setting low latency mode");
        }
    }
#endif
    
    memset(&req, 0, sizeof(req));
    req.count = 2;  // Only 2 buffers for lower latency.
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("Requesting Buffer");
        close(fd);
        return EXIT_FAILURE;
    }
    
    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        perror("Out of memory");
        close(fd);
        return EXIT_FAILURE;
    }
    n_buffers = req.count;
    
    for (unsigned int i = 0; i < n_buffers; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("Querying Buffer");
            free(buffers);
            close(fd);
            return EXIT_FAILURE;
        }
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Buffer map error");
            free(buffers);
            close(fd);
            return EXIT_FAILURE;
        }
    }
    
    for (unsigned int i = 0; i < n_buffers; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Queue Buffer");
            free(buffers);
            close(fd);
            return EXIT_FAILURE;
        }
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Start Capture");
        free(buffers);
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Initialize terminal parameters and precomputed scaling arrays.
    get_terminal_size(&term_cols, &term_rows);
    int render_rows = term_rows - 1;  // Reserve last row for menu.
    
    fx_arr = malloc(term_cols * sizeof(double));
    for (int col = 0; col < term_cols; col++) {
        double x_scale = (double)FRAME_WIDTH / term_cols;
        fx_arr[col] = col * x_scale;
    }
    fy_top_arr = malloc(render_rows * sizeof(double));
    fy_bot_arr = malloc(render_rows * sizeof(double));
    {
        double y_scale = (double)FRAME_HEIGHT / (render_rows * 2);
        for (int row = 0; row < render_rows; row++) {
            fy_top_arr[row] = row * 2 * y_scale;
            fy_bot_arr[row] = (row * 2 + 1) * y_scale;
        }
    }
    
    // Pre-allocate output buffer.
    size_t output_buf_size = render_rows * (term_cols * 64) + 128;
    char *output_buf = malloc(output_buf_size);
    if (!output_buf) {
        perror("Allocating output buffer");
        exit(EXIT_FAILURE);
    }
    
    // Allocate local frame buffer for rendering.
    unsigned char *local_frame = malloc(buffers[0].length);
    if (!local_frame) {
        perror("Allocating local frame buffer");
        exit(EXIT_FAILURE);
    }
    
    // Allocate shared frame buffer.
    shared_frame = malloc(buffers[0].length);
    if (!shared_frame) {
        perror("Allocating shared frame buffer");
        exit(EXIT_FAILURE);
    }
    
    // Set up capture context and start capture thread.
    struct capture_context cap_ctx = { fd, buffers, n_buffers };
    pthread_t cap_thread;
    if (pthread_create(&cap_thread, NULL, capture_thread_func, &cap_ctx) != 0) {
        perror("Creating capture thread");
        exit(EXIT_FAILURE);
    }
    
    // FPS computation variables.
    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    int frame_count = 0;
    double fps = 0.0;
    
    // Global outputs (initially set to center).
    int output0 = FRAME_WIDTH / 2;
    int output1 = FRAME_HEIGHT / 2;
    
    // Main rendering loop.
    while (!stop) {
        process_input();
        
        pthread_mutex_lock(&frame_mutex);
        while (!frame_ready && !stop) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 10 * 1000000; // 10 ms timeout.
            pthread_cond_timedwait(&frame_cond, &frame_mutex, &ts);
        }
        if (frame_ready) {
            memcpy(local_frame, shared_frame, shared_frame_size);
            frame_ready = 0;
        }
        pthread_mutex_unlock(&frame_mutex);
        
        // Process frame for object recognition and overlay if enabled.
        if (object_detection_enabled) {
            Position pos = process_frame(local_frame, shared_frame_size, FRAME_WIDTH, FRAME_HEIGHT);
            output0 = pos.x;
            output1 = pos.y;
            // Send outputs to server if TCP connection is active.
            if (tcp_sockfd != -1) {
                char tcp_buf[TCP_BUFFER_SIZE];
                int len = snprintf(tcp_buf, sizeof(tcp_buf), "out0: %d\n", output0);
                if (send(tcp_sockfd, tcp_buf, (size_t)len, 0) < 0) {
                    perror("send out0");
                }
                len = snprintf(tcp_buf, sizeof(tcp_buf), "out1: %d\n", output1);
                if (send(tcp_sockfd, tcp_buf, (size_t)len, 0) < 0) {
                    perror("send out1");
                }
            }
        }
        
        clear_terminal();
        frame_to_halfblock_ascii(local_frame, FRAME_WIDTH, FRAME_HEIGHT,
                                 term_cols, render_rows, quality_mode,
                                 output_buf, output_buf_size);
        write(STDOUT_FILENO, output_buf, strlen(output_buf));
        
        frame_count++;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) +
                         (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            fps = frame_count / elapsed;
            frame_count = 0;
            start_time = current_time;
        }
        draw_menu_bar(fps, term_cols, term_rows, output0, output1);
        
        useconds_t desired_delay = 1000000 / target_fps;
        sleep_microseconds(desired_delay);
    }
    
    pthread_join(cap_thread, NULL);
    
    for (unsigned int i = 0; i < n_buffers; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    free(output_buf);
    free(local_frame);
    free(shared_frame);
    free(fx_arr);
    free(fy_top_arr);
    free(fy_bot_arr);
    close(fd);
    if (tcp_sockfd != -1)
        close(tcp_sockfd);
    
    return EXIT_SUCCESS;
}
