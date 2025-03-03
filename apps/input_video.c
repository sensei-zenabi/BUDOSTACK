/*
 * input_video.c
 *
 * Advanced ANSI Camera App with Half-Block Rendering
 *
 * Description:
 *   This application captures video frames from /dev/video0 using V4L2,
 *   converts each frame into a colored, high-detail representation using
 *   Unicode half-block characters (▀), and displays the result in the terminal.
 *
 *   Each terminal cell is used to display two pixels: the top pixel is shown
 *   as the foreground color and the bottom pixel as the background color.
 *
 * Design principles:
 *   - Written in plain C using -std=c11 and only standard POSIX libraries.
 *   - Contains all functionality in a single file (no separate headers).
 *   - Uses ANSI true-color escape codes to output 24-bit color.
 *   - Dynamically adapts to the current terminal size.
 *   - Implements a fast, per-cell conversion using nearest-neighbor sampling.
 *
 *   This advanced approach is based on research into innovative terminal graphics
 *   techniques that combine true-color ANSI escapes with half-block rendering to
 *   achieve a high-fidelity, real-time camera feed in the terminal.
 *
 * Note:
 *   - The video device must support YUYV at 320x240. Adjust FRAME_WIDTH and FRAME_HEIGHT as needed.
 *
 * References:
 *   :contentReference[oaicite:0]{index=0} - V4L2 capture examples.
 *   :contentReference[oaicite:1]{index=1} - ANSI 24-bit color techniques.
 *   :contentReference[oaicite:2]{index=2} - Using Unicode half-blocks for terminal graphics.
 */

// Ensure that _POSIX_C_SOURCE is defined before including any headers.
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>      // For read, write, close, etc.
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>        // For nanosleep
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <termios.h>

// Global flag for graceful exit.
volatile sig_atomic_t stop = 0;

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
    // ANSI escape to clear screen and move cursor to top-left.
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

// Query the terminal size (columns and rows).
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

// Convert one pixel from YUYV format to RGB.
// For a given (x,y) coordinate, determine the pixel's Y value
// (using the appropriate byte in a pair) and the shared U, V.
void yuyv_to_rgb(const unsigned char *frame, int frame_width,
                 int x, int y, unsigned char *r, unsigned char *g, unsigned char *b) {
    // Each pair of pixels occupies 4 bytes: Y0 U0 Y1 V0.
    int pair_index = x / 2;
    int base = (y * frame_width + pair_index * 2) * 2;
    unsigned char Y;
    unsigned char U = frame[base + 1];
    unsigned char V = frame[base + 3];
    if ((x & 1) == 0) {
        Y = frame[base];
    } else {
        Y = frame[base + 2];
    }
    // Standard YUV to RGB conversion.
    int C = (int)Y - 16;
    int D = (int)U - 128;
    int E = (int)V - 128;
    int R_temp = (298 * C + 409 * E + 128) >> 8;
    int G_temp = (298 * C - 100 * D - 208 * E + 128) >> 8;
    int B_temp = (298 * C + 516 * D + 128) >> 8;
    if (R_temp < 0) R_temp = 0; else if (R_temp > 255) R_temp = 255;
    if (G_temp < 0) G_temp = 0; else if (G_temp > 255) G_temp = 255;
    if (B_temp < 0) B_temp = 0; else if (B_temp > 255) B_temp = 255;
    *r = (unsigned char)R_temp;
    *g = (unsigned char)G_temp;
    *b = (unsigned char)B_temp;
}

/*
 * Render the camera frame to the terminal using half-block characters.
 *
 * This function downsamples the camera frame to the effective display resolution,
 * where each terminal cell represents two vertical pixels:
 *   - The top pixel's color is set as the foreground color.
 *   - The bottom pixel's color is set as the background color.
 *
 * The mapping scales the source image (FRAME_WIDTH x FRAME_HEIGHT) to the terminal grid,
 * taking into account that each text row represents 2 pixels in height.
 */
void frame_to_halfblock_ascii(const unsigned char *frame, int frame_width, int frame_height,
                                int term_cols, int term_rows) {
    // Effective target resolution:
    // Each text column maps to one pixel in width.
    // Each text row maps to 2 pixels in height.
    int target_width = term_cols;
    int target_height = term_rows * 2;
    // Scaling factors (source -> target).
    double x_scale = (double)frame_width / target_width;
    double y_scale = (double)frame_height / target_height;

    // Build each line into a buffer to reduce write() calls.
    char *line_buffer = malloc(term_cols * 64);
    if (!line_buffer) {
        perror("malloc");
        return;
    }
    line_buffer[0] = '\0';

    for (int row = 0; row < term_rows; row++) {
        line_buffer[0] = '\0';
        for (int col = 0; col < term_cols; col++) {
            // For each text cell, sample two source pixels:
            // Top pixel at effective y = row*2, bottom pixel at effective y = row*2 + 1.
            int src_x = (int)(col * x_scale);
            int src_y_top = (int)(row * 2 * y_scale);
            int src_y_bot = (int)((row * 2 + 1) * y_scale);
            unsigned char top_r, top_g, top_b;
            unsigned char bot_r, bot_g, bot_b;
            yuyv_to_rgb(frame, frame_width, src_x, src_y_top, &top_r, &top_g, &top_b);
            // If bottom pixel is beyond source bounds, use black.
            if (src_y_bot >= frame_height) {
                bot_r = bot_g = bot_b = 0;
            } else {
                yuyv_to_rgb(frame, frame_width, src_x, src_y_bot, &bot_r, &bot_g, &bot_b);
            }
            // Append ANSI escape sequence for true color:
            // Set foreground (top pixel) and background (bottom pixel), then print half-block (▀).
            // Using sprintf to append into the line buffer.
            char cell[64];
            sprintf(cell, "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀",
                    top_r, top_g, top_b, bot_r, bot_g, bot_b);
            strcat(line_buffer, cell);
        }
        // Reset attributes and add newline.
        strcat(line_buffer, "\033[0m\n");
        // Write the entire line at once.
        fputs(line_buffer, stdout);
    }
    free(line_buffer);
}

int main(void) {
    int fd;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct buffer *buffers;
    unsigned int n_buffers;
    int term_cols, term_rows;

    // Register SIGINT handler for graceful exit.
    signal(SIGINT, handle_sigint);

    // Open video device.
    fd = open("/dev/video0", O_RDWR);
    if (fd == -1) {
        perror("Opening video device");
        return EXIT_FAILURE;
    }

    // Set video format (assumes YUYV at FRAME_WIDTH x FRAME_HEIGHT).
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

    // Request 4 buffers for memory-mapped I/O.
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

    // Map buffers.
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

    // Queue the buffers.
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

    // Start streaming.
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Start Capture");
        free(buffers);
        close(fd);
        return EXIT_FAILURE;
    }

    // Get terminal size.
    get_terminal_size(&term_cols, &term_rows);

    // Main loop: capture and display frames.
    while (!stop) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Dequeue Buffer");
            break;
        }

        clear_terminal();
        // Render frame using half-block Unicode (each cell represents two vertical pixels).
        frame_to_halfblock_ascii(buffers[buf.index].start, FRAME_WIDTH, FRAME_HEIGHT,
                                   term_cols, term_rows);

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Requeue Buffer");
            break;
        }

        // Short delay (~20 FPS).
        sleep_microseconds(50000);
    }

    // Stop streaming.
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Stop Capture");
    }

    // Unmap and free buffers.
    for (unsigned int i = 0; i < n_buffers; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);

    return EXIT_SUCCESS;
}
