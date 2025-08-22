/*
 * input_joystick.c
 *
 * Modified to act as a TCP client connecting to server.c.
 * On startup, the program connects to the server (default 127.0.0.1:12345 or as provided via command-line)
 * and then sends all joystick events over the TCP connection as they are received.
 *
 * Output details:
 *   - For the first connected joystick device (device index 0), its events are sent on channels 0 and 1:
 *         Channel 0: Unique integer identifier (for axis: event.number; for button: event.number + BUTTON_OFFSET).
 *         Channel 1: The event’s value.
 *   - For the second connected joystick device (device index 1), its events are sent on channels 2 and 3 similarly.
 *   - Channel 4 remains unused.
 *
 * Each message is sent as a line containing only an integer with the prefix "outX:" (where X is the channel number)
 * and terminated with a newline. Terminal output is prepended with a timestamp (HH:MM:SS).
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
#include <time.h>
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
#define MAX_JOYSTICKS      5       // Maximum number of joystick devices to handle.
#define MAX_BUFFER_ROWS    1000    // Circular buffer size.
#define MAX_MESSAGE_LENGTH 256     // Maximum length per message.
#define INPUT_DIR          "/dev/input"
#define DEFAULT_SERVER_IP  "127.0.0.1"  // Default server IP.
#define SERVER_PORT        12345        // Server TCP port

// Offset for button events to keep identifiers unique.
#define BUTTON_OFFSET 100

// ---------------------------------------------------------
// Joystick Event Structure
// ---------------------------------------------------------
typedef struct {
    uint32_t time;     // Event timestamp in milliseconds.
    int16_t  value;    // Axis value or button state.
    uint8_t  type;     // Event type.
    uint8_t  number;   // Axis/button number.
} js_event;

// Event type masks.
#define JS_EVENT_BUTTON 0x01    // Button pressed/released.
#define JS_EVENT_AXIS   0x02    // Joystick moved.
#define JS_EVENT_INIT   0x80    // Initial state.

// ---------------------------------------------------------
// Circular Message Buffer (for terminal output and TCP sending)
// ---------------------------------------------------------
char message_buffer[MAX_BUFFER_ROWS][MAX_MESSAGE_LENGTH];
size_t buffer_index = 0;  // Next available slot.

// Global TCP socket.
int server_sockfd = -1;

// ---------------------------------------------------------
// Helper: Get current timestamp string in HH:MM:SS format.
// ---------------------------------------------------------
static void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", tm_info);
}

// ---------------------------------------------------------
// Helper: Add a message to the buffer, print with timestamp, and send via TCP.
// ---------------------------------------------------------
static void add_message(const char *msg) {
    strncpy(message_buffer[buffer_index], msg, MAX_MESSAGE_LENGTH - 1);
    message_buffer[buffer_index][MAX_MESSAGE_LENGTH - 1] = '\0';

    char time_str[16];
    get_timestamp(time_str, sizeof(time_str));
    printf("[%s] %s", time_str, message_buffer[buffer_index]);
    fflush(stdout);

    if (server_sockfd >= 0) {
        ssize_t sent = send(server_sockfd, message_buffer[buffer_index],
                            strlen(message_buffer[buffer_index]), 0);
        if (sent < 0)
            perror("send");
    }
    buffer_index = (buffer_index + 1) % MAX_BUFFER_ROWS;
}

// ---------------------------------------------------------
// Helper: Check if a filename represents a joystick device.
// ---------------------------------------------------------
static int is_joystick(const char *name) {
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
    const char *server_ip = (argc >= 2) ? argv[1] : DEFAULT_SERVER_IP;
    server_sockfd = connect_to_server(server_ip, SERVER_PORT);
    if (server_sockfd < 0) {
        fprintf(stderr, "Failed to connect to server %s:%d\n", server_ip, SERVER_PORT);
        return 1;
    }
    printf("Connected to server %s:%d\n", server_ip, SERVER_PORT);

    DIR *dir;
    struct dirent *entry;
    char path[512];
    int js_fds[MAX_JOYSTICKS];
    int js_count = 0;
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        js_fds[i] = -1;
    }
    dir = opendir(INPUT_DIR);
    if (!dir) {
        perror("opendir");
        close(server_sockfd);
        return 1;
    }
    while ((entry = readdir(dir)) != NULL && js_count < MAX_JOYSTICKS) {
        if (!is_joystick(entry->d_name))
            continue;
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, entry->d_name);
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
    // We only process up to two devices.
    if (js_count > 2)
        js_count = 2;
    
    fd_set readfds;
    int maxfd = -1;
    for (int i = 0; i < js_count; i++) {
        if (js_fds[i] > maxfd)
            maxfd = js_fds[i];
    }
    
    printf("Listening for joystick events...\n");
    while (1) {
        FD_ZERO(&readfds);
        for (int i = 0; i < js_count; i++) {
            FD_SET(js_fds[i], &readfds);
        }
        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }
        // Process each device independently.
        for (int i = 0; i < js_count; i++) {
            if (FD_ISSET(js_fds[i], &readfds)) {
                js_event event;
                ssize_t n = read(js_fds[i], &event, sizeof(event));
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    perror("read");
                    continue;
                }
                if (n == sizeof(event)) {
                    int identifier;
                    if (event.type & JS_EVENT_AXIS)
                        identifier = event.number;
                    else if (event.type & JS_EVENT_BUTTON)
                        identifier = event.number + BUTTON_OFFSET;
                    else
                        identifier = event.number; // fallback

                    char msg_id[MAX_MESSAGE_LENGTH];
                    char msg_val[MAX_MESSAGE_LENGTH];
                    if (i == 0) {
                        // Device 0 -> channels 0 and 1.
                        snprintf(msg_id, sizeof(msg_id), "out0: %d\n", identifier);
                        snprintf(msg_val, sizeof(msg_val), "out1: %d\n", event.value);
                    } else if (i == 1) {
                        // Device 1 -> channels 2 and 3.
                        snprintf(msg_id, sizeof(msg_id), "out2: %d\n", identifier);
                        snprintf(msg_val, sizeof(msg_val), "out3: %d\n", event.value);
                    }
                    add_message(msg_id);
                    add_message(msg_val);
                }
            }
        }
    }
    
    for (int i = 0; i < js_count; i++) {
        if (js_fds[i] >= 0)
            close(js_fds[i]);
    }
    if (server_sockfd >= 0)
        close(server_sockfd);
    return 0;
}
