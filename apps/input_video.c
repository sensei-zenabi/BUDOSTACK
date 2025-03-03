/*
 * input_video.c
 *
 * ANSI Camera App
 *
 * Description:
 *   This application captures video frames from the default video device (/dev/video0)
 *   using the V4L2 API, converts each frame into ASCII art, and displays it in the terminal.
 *   The application is designed for POSIX-compliant Linux systems.
 *
 * Design principles:
 *   - Use plain C with -std=c11.
 *   - Rely only on standard POSIX libraries (plus the Linux-specific V4L2 headers).
 *   - All code is in a single file; no header files are created.
 *   - Comments are provided to explain design decisions.
 *   - The code is intended for remote terminal use (e.g., via SSH/putty).
 *
 * Note:
 *   The video device is assumed to support the YUYV pixel format at 320x240 resolution.
 *   You may need to adjust the format or resolution depending on your camera.
 *
 * References:
 *   :contentReference[oaicite:0]{index=0} - General V4L2 capture examples.
 *   :contentReference[oaicite:1]{index=1} - ASCII art brightness mapping technique.
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

// Global flag to allow graceful exit.
volatile sig_atomic_t stop = 0;

void handle_sigint(int sig) {
    (void)sig;
    stop = 1;
}

// Wrapper for sleeping in microseconds using nanosleep.
static void sleep_microseconds(useconds_t usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// Structure for buffer memory mapping.
struct buffer {
    void   *start;
    size_t  length;
};

#define FRAME_WIDTH  320
#define FRAME_HEIGHT 240

// ASCII characters from light to dark (adjust gradient as desired)
const char *ascii_chars = " .:-=+*#%@";

// Clear terminal using ANSI escape codes.
void clear_terminal(void) {
    // ANSI escape sequence to clear screen and move cursor to top left.
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

// Get terminal size (columns and rows)
void get_terminal_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        // Fallback to default if error.
        *cols = 80;
        *rows = 24;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

// Map a brightness value (0-255) to an ASCII character.
char brightness_to_ascii(unsigned char brightness) {
    size_t len = strlen(ascii_chars);
    size_t index = brightness * (len - 1) / 255;
    return ascii_chars[index];
}

// Downscale and convert a YUYV frame to ASCII art.
// We use only the Y (luminance) channel which is every other byte.
void frame_to_ascii(const unsigned char *frame, int frame_width, int frame_height,
                    int term_cols, int term_rows) {
    // Calculate scaling factors.
    double x_scale = (double)frame_width / term_cols;
    double y_scale = (double)frame_height / term_rows;
    char *line = malloc(term_cols + 1);
    if (!line) {
        perror("malloc");
        return;
    }

    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            // Calculate corresponding pixel in the frame.
            int src_x = (int)(col * x_scale);
            int src_y = (int)(row * y_scale);
            // In YUYV, each pair of bytes contains: Y0 U, then Y1 V.
            // So the Y value for pixel at (x, y) is at offset = (src_y * frame_width + src_x)*2.
            int offset = (src_y * frame_width + src_x) * 2;
            unsigned char Y = frame[offset]; // Luminance channel.
            line[col] = brightness_to_ascii(Y);
        }
        line[term_cols] = '\0';
        printf("%s\n", line);
    }
    free(line);
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

    // Open the video device.
    fd = open("/dev/video0", O_RDWR);
    if (fd == -1) {
        perror("Opening video device");
        return EXIT_FAILURE;
    }

    // Query and set the video format.
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = FRAME_WIDTH;
    fmt.fmt.pix.height      = FRAME_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; // YUYV format.
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Setting Pixel Format");
        close(fd);
        return EXIT_FAILURE;
    }

    // Request buffers.
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

    // Map the buffers.
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

        // Dequeue a buffer.
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Dequeue Buffer");
            break;
        }

        // Clear terminal and convert frame to ASCII.
        clear_terminal();
        frame_to_ascii(buffers[buf.index].start, FRAME_WIDTH, FRAME_HEIGHT, term_cols, term_rows);

        // Requeue the buffer.
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Requeue Buffer");
            break;
        }

        // A short delay to control frame rate (~20 FPS).
        sleep_microseconds(50000);  // 50ms delay
    }

    // Stop streaming.
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Stop Capture");
    }

    // Unmap buffers.
    for (unsigned int i = 0; i < n_buffers; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);

    return EXIT_SUCCESS;
}
