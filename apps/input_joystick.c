/*
 * input_joystick.c
 *
 * This program detects any USB-connected joystick on a Linux machine by scanning the /dev/input directory.
 * It opens up to 5 joystick devices (one per output channel) and captures all messages (joystick events)
 * sent by them. Each event is formatted as "outN:" (where N is 0..4) followed by event details and printed on a new line.
 * The program also maintains a circular message buffer of 1000 rows.
 *
 * Design Principles:
 *   - Plain C, compiled with -std=c11.
 *   - Single-file implementation (no header files).
 *   - Uses only standard C and POSIX libraries.
 *   - Uses select() to multiplex I/O between joystick file descriptors.
 *   - Follows a simple line-based protocol for output similar to the provided client_template.
 *
 * Compilation Example:
 *   gcc -std=c11 -Wall -Wextra -pedantic -o input_joystick input_joystick.c
 *
 * Run Example:
 *   ./input_joystick
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>       // read(), close(), etc.
#include <fcntl.h>        // open(), O_NONBLOCK
#include <dirent.h>       // opendir(), readdir(), closedir()
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>   // select()
#include <errno.h>

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
#define MAX_JOYSTICKS      5       // Maximum number of joystick devices handled simultaneously.
#define MAX_BUFFER_ROWS    1000    // Circular buffer to hold last 1000 messages.
#define MAX_MESSAGE_LENGTH 256     // Maximum length per message.
#define INPUT_DIR          "/dev/input"

// ---------------------------------------------------------
// Joystick Event Structure
// ---------------------------------------------------------
// Define the joystick event structure. Normally, you could include <linux/joystick.h>,
// but here we define it ourselves to keep the code self-contained and standard.
typedef struct {
    uint32_t time;     // event timestamp in milliseconds
    int16_t  value;    // axis value or button press value
    uint8_t  type;     // event type
    uint8_t  number;   // axis/button number
} js_event;

// Event type masks (these are typical values)
#define JS_EVENT_BUTTON 0x01    // button pressed/released
#define JS_EVENT_AXIS   0x02    // joystick moved
#define JS_EVENT_INIT   0x80    // initial state of device

// ---------------------------------------------------------
// Circular Message Buffer Structure
// ---------------------------------------------------------
char message_buffer[MAX_BUFFER_ROWS][MAX_MESSAGE_LENGTH];
size_t buffer_index = 0;  // points to the next row to fill

// ---------------------------------------------------------
// Helper function: Add a message to the circular buffer and print it
// ---------------------------------------------------------
static void add_message(const char *msg) {
    // Copy the message into the circular buffer
    strncpy(message_buffer[buffer_index], msg, MAX_MESSAGE_LENGTH - 1);
    message_buffer[buffer_index][MAX_MESSAGE_LENGTH - 1] = '\0'; // ensure null termination

    // Print the message immediately
    printf("%s\n", message_buffer[buffer_index]);
    fflush(stdout);

    // Update buffer index circularly
    buffer_index = (buffer_index + 1) % MAX_BUFFER_ROWS;
}

// ---------------------------------------------------------
// Helper function: Check if a filename represents a joystick device
// ---------------------------------------------------------
static int is_joystick(const char *name) {
    // We check if the filename starts with "js" followed by at least one digit.
    if (name[0] != 'j' || name[1] != 's')
        return 0;
    for (size_t i = 2; name[i] != '\0'; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
    }
    return 1;
}

// ---------------------------------------------------------
// Main: Joystick Input Capture and Multiplexing Entry Point
// ---------------------------------------------------------
int main(void) {
    DIR *dir;
    struct dirent *entry;
    char path[256];
    int js_fds[MAX_JOYSTICKS];
    int js_count = 0;

    // Initialize joystick file descriptors array.
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        js_fds[i] = -1;
    }

    // Open /dev/input directory to search for joystick devices.
    dir = opendir(INPUT_DIR);
    if (!dir) {
        perror("opendir");
        return 1;
    }

    // Scan directory entries for joystick devices.
    while ((entry = readdir(dir)) != NULL && js_count < MAX_JOYSTICKS) {
        if (!is_joystick(entry->d_name))
            continue;
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, entry->d_name);
        // Open joystick device in non-blocking mode.
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
            continue;
        }
        js_fds[js_count++] = fd;
        printf("Opened joystick device %s as channel out%d\n", path, js_count - 1);
    }
    closedir(dir);

    if (js_count == 0) {
        fprintf(stderr, "No joystick devices found in %s.\n", INPUT_DIR);
        return 1;
    }

    // ---------------------------------------------------------
    // Main event loop: use select() to multiplex joystick file descriptors.
    // ---------------------------------------------------------
    fd_set readfds;
    int maxfd = -1;
    for (int i = 0; i < js_count; i++) {
        if (js_fds[i] > maxfd) {
            maxfd = js_fds[i];
        }
    }

    printf("Listening for joystick events...\n");
    while (1) {
        FD_ZERO(&readfds);
        // Add each joystick fd to the set.
        for (int i = 0; i < js_count; i++) {
            FD_SET(js_fds[i], &readfds);
        }

        // Wait indefinitely until an event is available on any joystick.
        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // Check each joystick fd for available events.
        for (int i = 0; i < js_count; i++) {
            if (FD_ISSET(js_fds[i], &readfds)) {
                js_event event;
                ssize_t n = read(js_fds[i], &event, sizeof(event));
                if (n < 0) {
                    // If no data available, continue.
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    perror("read");
                    continue;
                }
                if (n == sizeof(event)) {
                    // Format message using output channel corresponding to joystick index.
                    char msg[MAX_MESSAGE_LENGTH];
                    // We ignore the JS_EVENT_INIT flag when printing events.
                    snprintf(msg, sizeof(msg), "out%d: time=%u type=%u number=%u value=%d",
                             i, event.time, event.type, event.number, event.value);
                    add_message(msg);
                }
            }
        }
    }

    // Clean up: close all joystick file descriptors.
    for (int i = 0; i < js_count; i++) {
        if (js_fds[i] >= 0)
            close(js_fds[i]);
    }

    return 0;
}
