/*
 * input_video.c
 *
 * Advanced ANSI Camera App with Performance Modes and Metrics
 *
 * Description:
 *   This application captures video frames from /dev/video0 using V4L2,
 *   converts each frame into a colored, high-detail representation using
 *   Unicode half-block characters (▀) and outputs it to the terminal.
 *
 *   Each terminal cell displays two vertical pixels:
 *     - The top pixel is shown as the foreground color.
 *     - The bottom pixel is shown as the background color.
 *
 *   A bottom menu bar displays performance metrics (FPS, current mode) and
 *   allows the user to switch between three modes (1, 2, and 3) in real time.
 *
 * Modes:
 *   1: Fast (Nearest Neighbor)
 *   2: Balanced (Simple 2x2 average)
 *   3: Quality (Bilinear Interpolation)
 *
 * Design principles:
 *   - Written in plain C (compiled with -std=c11) using only standard POSIX libraries.
 *   - Single-file implementation (no additional header files).
 *   - Uses ANSI 24-bit color escape codes.
 *   - Adapts to the current terminal size.
 *   - Processes keyboard input in raw mode (non-canonical, no echo) to allow mode toggling.
 *
 * Note:
 *   The video device is assumed to support YUYV at 320x240.
 *
 * References:
 *   :contentReference[oaicite:0]{index=0} - V4L2 capture examples.
 *   :contentReference[oaicite:1]{index=1} - ANSI 24-bit color techniques.
 *   :contentReference[oaicite:2]{index=2} - Using Unicode half-blocks.
 */

// Define _POSIX_C_SOURCE early.
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

// Global flag for graceful exit.
volatile sig_atomic_t stop = 0;

// Global quality mode (1: fast, 2: balanced, 3: quality)
volatile int quality_mode = 1;

void handle_sigint(int sig) {
    (void)sig;
    stop = 1;
}

// Sleep for a specified number of microseconds using nanosleep.
static void sleep_microseconds(useconds_t usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// Structure for memory-mapped buffer.
struct buffer {
    void   *start;
    size_t  length;
};

#define FRAME_WIDTH  320
#define FRAME_HEIGHT 240

// Clear the terminal using ANSI escape sequences.
void clear_terminal(void) {
    // ANSI: clear screen and move cursor to top-left.
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

// Get terminal size (columns and rows).
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

// Convert one pixel from YUYV to RGB using nearest neighbor.
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

// Mode 2: Simple average of 4 nearest pixels.
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

// Mode 3: Bilinear interpolation.
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
    double R = (1 - wx) * (1 - wy) * r00 + wx * (1 - wy) * r10 + (1 - wx) * wy * r01 + wx * wy * r11;
    double G = (1 - wx) * (1 - wy) * g00 + wx * (1 - wy) * g10 + (1 - wx) * wy * g01 + wx * wy * g11;
    double B = (1 - wx) * (1 - wy) * b00 + wx * (1 - wy) * b10 + (1 - wx) * wy * b01 + wx * wy * b11;
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

/*
 * Render the camera frame using half-block (▀) characters.
 *
 * Each text cell maps to one column (width) and two rows (height) of source pixels.
 * Sampling method depends on quality mode.
 */
void frame_to_halfblock_ascii(const unsigned char *frame, int frame_width, int frame_height,
                                int term_cols, int term_rows, int quality) {
    int target_width = term_cols;
    int target_height = term_rows * 2;
    double x_scale = (double)frame_width / target_width;
    double y_scale = (double)frame_height / target_height;
    
    char *line_buffer = malloc(term_cols * 64);
    if (!line_buffer) {
        perror("malloc");
        return;
    }
    line_buffer[0] = '\0';

    for (int row = 0; row < term_rows; row++) {
        line_buffer[0] = '\0';
        for (int col = 0; col < term_cols; col++) {
            double fx = col * x_scale;
            double fy_top = row * 2 * y_scale;
            double fy_bot = (row * 2 + 1) * y_scale;
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
            char cell[64];
            sprintf(cell, "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀",
                    top_r, top_g, top_b, bot_r, bot_g, bot_b);
            strcat(line_buffer, cell);
        }
        strcat(line_buffer, "\033[0m\n");
        fputs(line_buffer, stdout);
    }
    free(line_buffer);
}

// Set terminal to raw mode.
struct termios orig_termios;
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Restore terminal settings.
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Process key input (non-blocking) to change quality mode.
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
            }
        }
    }
}

// Draw a menu bar at the bottom displaying FPS and current mode.
void draw_menu_bar(double fps, int term_cols, int term_rows) {
    printf("\033[%d;1H", term_rows);
    printf("\033[7m");
    
    int bufsize = term_cols + 1;
    char *menu = malloc(bufsize);
    if (!menu) {
        perror("malloc");
        return;
    }
    
    snprintf(menu, bufsize, " Mode: %d  FPS: %.1f  [Press 1: Fast, 2: Balanced, 3: Quality] ", quality_mode, fps);
    int len = strlen(menu);
    while (len < term_cols) {
        if (len + 2 > bufsize) {
            bufsize *= 2;
            menu = realloc(menu, bufsize);
            if (!menu) {
                perror("realloc");
                return;
            }
        }
        strcat(menu, " ");
        len++;
    }
    printf("%s", menu);
    free(menu);
    printf("\033[0m");
    fflush(stdout); // Flush output to ensure menu bar is displayed.
}

int main(void) {
    int fd;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct buffer *buffers;
    unsigned int n_buffers;
    int term_cols, term_rows;

    // Disable stdout buffering.
    setbuf(stdout, NULL);
    
    enable_raw_mode();
    atexit(disable_raw_mode);
    signal(SIGINT, handle_sigint);

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

    memset(&req, 0, sizeof(req));
    req.count = 4;
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
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                fd, buf.m.offset);
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

    get_terminal_size(&term_cols, &term_rows);

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    int frame_count = 0;
    double fps = 0.0;

    while (!stop) {
        process_input();

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Dequeue Buffer");
            break;
        }

        clear_terminal();
        frame_to_halfblock_ascii(buffers[buf.index].start, FRAME_WIDTH, FRAME_HEIGHT,
                                   term_cols, term_rows - 1, quality_mode);

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Requeue Buffer");
            break;
        }

        frame_count++;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) +
                         (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            fps = frame_count / elapsed;
            frame_count = 0;
            start_time = current_time;
        }
        draw_menu_bar(fps, term_cols, term_rows);
        sleep_microseconds(33333);
    }

    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Stop Capture");
    }

    for (unsigned int i = 0; i < n_buffers; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);

    return EXIT_SUCCESS;
}
