/*
 * input_joystick.c
 *
 * Modified to act as a TCP client connecting to server.c.
 * On startup, the program connects to the server (default 127.0.0.1:12345 or as provided via command-line)
 * and then sends all joystick messages over the TCP connection as they are received.
 *
 * Revised Output Method:
 *   For each joystick event, two messages are sent:
 *     - Channel 0: A unique integer identifier representing the joystick control.
 *       * For an axis event, the identifier is the axis number (e.g. 0, 1, ...).
 *       * For a button event, the identifier is the button number plus an offset (here 100) to avoid conflict with axes.
 *     - Channel 1: The integer value of the event.
 *
 * Each message is formatted as a line containing only an integer,
 * preceded by the channel prefix ("out0:" for the identifier, "out1:" for the value),
 * and terminated with a newline.
 *
 * Example output for an axis event on axis 2 with value 32767:
 *   out0: 2\n
 *   out1: 32767\n
 *
 * Example output for a button event on button 3 with value 1:
 *   out0: 103\n   // (3 + 100 offset)
 *   out1: 1\n
 *
 * Compilation Example:
 *   gcc -std=c11 -Wall -Wextra -pedantic -o input_joystick input_joystick.c
 *
 * Run Example:
 *   ./input_joystick [server_ip]
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
#include <sys/socket.h>   // TCP socket functions
#include <arpa/inet.h>    // inet_pton(), htons()

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
#define MAX_JOYSTICKS      5       // Maximum number of joystick devices handled simultaneously.
#define MAX_BUFFER_ROWS    1000    // Circular buffer to hold last 1000 messages.
#define MAX_MESSAGE_LENGTH 256     // Maximum length per message.
#define INPUT_DIR          "/dev/input"
#define DEFAULT_SERVER_IP  "127.0.0.1"  // Default server IP if not provided.
#define SERVER_PORT        12345        // Server TCP port

// Offset to add for button events so that axis and button IDs are unique.
#define BUTTON_OFFSET 100

// ---------------------------------------------------------
// Joystick Event Structure
// ---------------------------------------------------------
// Defined here to keep the code self-contained.
typedef struct {
    uint32_t time;     // event timestamp in milliseconds
    int16_t  value;    // axis value or button press value
    uint8_t  type;     // event type
    uint8_t  number;   // axis/button number
} js_event;

// Event type masks (typical values)
#define JS_EVENT_BUTTON 0x01    // button pressed/released
#define JS_EVENT_AXIS   0x02    // joystick moved
#define JS_EVENT_INIT   0x80    // initial state of device

// ---------------------------------------------------------
// Circular Message Buffer
// ---------------------------------------------------------
char message_buffer[MAX_BUFFER_ROWS][MAX_MESSAGE_LENGTH];
size_t buffer_index = 0;  // points to the next row to fill

// Global variable for the TCP socket connected to the server.
int server_sockfd = -1;

// ---------------------------------------------------------
// Helper: Add a message to the circular buffer, print it, and send it over TCP.
// ---------------------------------------------------------
static void add_message(const char *msg) {
    // Copy the message into the circular buffer.
    strncpy(message_buffer[buffer_index], msg, MAX_MESSAGE_LENGTH - 1);
    message_buffer[buffer_index][MAX_MESSAGE_LENGTH - 1] = '\0'; // ensure null termination

    // Print the message locally.
    printf("%s", message_buffer[buffer_index]);  // message already includes newline
    fflush(stdout);

    // Send the message over the TCP connection if established.
    if (server_sockfd >= 0) {
        ssize_t sent = send(server_sockfd, message_buffer[buffer_index], strlen(message_buffer[buffer_index]), 0);
        if (sent < 0) {
            perror("send");
        }
    }

    // Update buffer index circularly.
    buffer_index = (buffer_index + 1) % MAX_BUFFER_ROWS;
}

// ---------------------------------------------------------
// Helper: Check if a filename represents a joystick device.
// ---------------------------------------------------------
static int is_joystick(const char *name) {
    // Check if filename starts with "js" followed by digits.
    if (name[0] != 'j' || name[1] != 's')
        return 0;
    for (size_t i = 2; name[i] != '\0'; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
    }
    return 1;
}

// ---------------------------------------------------------
// Function: Establish TCP connection to the server.
// ---------------------------------------------------------
static int connect_to_server(const char *server_ip, unsigned short port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// ---------------------------------------------------------
// Main: Joystick Input Capture and TCP Client Entry Point
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    // Determine server IP from command-line or use default.
    const char *server_ip = (argc >= 2) ? argv[1] : DEFAULT_SERVER_IP;
    
    // Connect to the server.
    server_sockfd = connect_to_server(server_ip, SERVER_PORT);
    if (server_sockfd < 0) {
        fprintf(stderr, "Failed to connect to server %s:%d\n", server_ip, SERVER_PORT);
        return 1;
    }
    printf("Connected to server %s:%d\n", server_ip, SERVER_PORT);

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
        close(server_sockfd);
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
        printf("Opened joystick device %s assigned to physical channel %d\n", path, js_count - 1);
    }
    closedir(dir);

    if (js_count == 0) {
        fprintf(stderr, "No joystick devices found in %s.\n", INPUT_DIR);
        close(server_sockfd);
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
                    // Compute a unique integer identifier:
                    // For axis events, use the axis number directly.
                    // For button events, add BUTTON_OFFSET.
                    int identifier;
                    if (event.type & JS_EVENT_AXIS) {
                        identifier = event.number;
                    } else if (event.type & JS_EVENT_BUTTON) {
                        identifier = event.number + BUTTON_OFFSET;
                    } else {
                        // Fallback for other event types.
                        identifier = event.number + 2 * BUTTON_OFFSET;
                    }
                    
                    // Prepare two messages:
                    // Channel 0: the identifier as an integer.
                    // Channel 1: the value of the event.
                    char msg_id[MAX_MESSAGE_LENGTH];
                    char msg_val[MAX_MESSAGE_LENGTH];
                    
                    snprintf(msg_id, sizeof(msg_id), "out0: %d\n", identifier);
                    snprintf(msg_val, sizeof(msg_val), "out1: %d\n", event.value);
                    
                    add_message(msg_id);
                    add_message(msg_val);
                }
            }
        }
    }

    // Clean up: close all joystick file descriptors and the TCP socket.
    for (int i = 0; i < js_count; i++) {
        if (js_fds[i] >= 0)
            close(js_fds[i]);
    }
    if (server_sockfd >= 0)
        close(server_sockfd);

    return 0;
}
