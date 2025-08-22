/*
 * usb_mapper_client.c
 *
 * This application is a TCP client that connects to a server (default 127.0.0.1:12345)
 * and sends data read from USB serial devices. It scans /dev for devices whose names start
 * with "ttyUSB" or "ttyACM", then lists them with index numbers. The user is prompted to select
 * one device for each output channel (0..4) by entering the corresponding number (or -1 to skip).
 *
 * After the mapping is complete, the app goes into monitor mode. Each time new payload data is
 * detected on a mapped USB device, the app prints a newline to STDOUT with the current timestamp,
 * the device identification (full path), and the payload (displayed as a hexadecimal string).
 *
 * The app also sends the data to the server, using the format:
 *    outN: <hex_payload>
 * where N is the output channel.
 *
 * Design Principles:
 *   - Written in plain C (compiled with -std=c11) in a single file.
 *   - Uses only standard C and POSIX libraries.
 *   - Uses select() to multiplex I/O among the network socket, STDIN, and USB devices.
 *
 * Compilation Example:
 *   gcc -std=c11 -Wall -Wextra -pedantic -o usb_mapper_client usb_mapper_client.c
 *
 * Run Example:
 *   ./usb_mapper_client [server_ip] [port]
 *
 * Default server_ip is "127.0.0.1" and default port is 12345.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // close(), read(), etc.
#include <arpa/inet.h>    // socket, connect(), etc.
#include <sys/select.h>   // select()
#include <ctype.h>        // isdigit()
#include <fcntl.h>        // open()
#include <errno.h>
#include <limits.h>       // for PATH_MAX
#include <dirent.h>       // opendir(), readdir()
#include <time.h>         // time(), localtime(), strftime()

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
#define DEFAULT_PORT 12345
#define DEFAULT_IP   "127.0.0.1"
#define BUFFER_SIZE  512
#define NUM_OUTPUTS  5
#define MAX_USB_DEVICES 256

// ---------------------------------------------------------
// Helper function: remove newline characters from a string
// ---------------------------------------------------------
static void trim_newline(char *s) {
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
    p = strchr(s, '\r');
    if (p) *p = '\0';
}

// ---------------------------------------------------------
// Scan /dev for USB serial devices (ttyUSB* and ttyACM*).
// Returns the number of devices found and fills found_devs array.
// ---------------------------------------------------------
static int list_usb_devices(char *found_devs[MAX_USB_DEVICES]) {
    DIR *dev_dir = opendir("/dev");
    if (!dev_dir) {
        perror("opendir");
        return 0;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dev_dir)) != NULL && count < MAX_USB_DEVICES) {
        if (strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
            strncmp(entry->d_name, "ttyACM", 6) == 0) {
            found_devs[count] = strdup(entry->d_name);
            if (found_devs[count] == NULL) {
                perror("strdup");
                continue;
            }
            count++;
        }
    }
    closedir(dev_dir);
    return count;
}

// ---------------------------------------------------------
// Prompt the user to map available USB devices (from found list)
// to output channels 0..NUM_OUTPUTS-1.
// For each channel, the user enters a device index from the list or -1 to skip.
// Opens the chosen device in non-blocking mode.
// ---------------------------------------------------------
static int get_usb_mapping(int usb_fds[NUM_OUTPUTS], char *usb_paths[NUM_OUTPUTS]) {
    char *all_devs[MAX_USB_DEVICES];
    int total_devs = list_usb_devices(all_devs);

    if (total_devs == 0) {
        printf("No USB serial devices found in /dev (ttyUSB* or ttyACM*).\n");
    } else {
        printf("Found %d USB serial device(s):\n", total_devs);
        for (int i = 0; i < total_devs; i++) {
            printf("  [%d] /dev/%s\n", i, all_devs[i]);
        }
    }

    char input[32];
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
        printf("Select device ID for output channel %d (or -1 to skip): ", ch);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            fprintf(stderr, "Error reading input.\n");
            return -1;
        }
        trim_newline(input);
        int sel = atoi(input);
        if (sel < 0 || sel >= total_devs) {
            usb_fds[ch] = -1;
            usb_paths[ch] = NULL;
            printf("Output channel %d skipped.\n", ch);
        } else {
            char dev_path[PATH_MAX];
            snprintf(dev_path, sizeof(dev_path), "/dev/%s", all_devs[sel]);
            int fd = open(dev_path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                fprintf(stderr, "Warning: cannot open %s: %s\n", dev_path, strerror(errno));
                usb_fds[ch] = -1;
                usb_paths[ch] = NULL;
            } else {
                usb_fds[ch] = fd;
                usb_paths[ch] = strdup(dev_path);
                if (!usb_paths[ch]) {
                    perror("strdup");
                    close(fd);
                    usb_fds[ch] = -1;
                }
                printf("Mapped out%d -> %s\n", ch, dev_path);
            }
        }
    }

    // Free the temporary list.
    for (int i = 0; i < total_devs; i++) {
        free(all_devs[i]);
    }
    return 0;
}

// ---------------------------------------------------------
// Main: Client Application Entry Point
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    const char *server_ip = DEFAULT_IP;
    unsigned short port   = DEFAULT_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        port = (unsigned short)atoi(argv[2]);
        if (port == 0) {
            port = DEFAULT_PORT;
        }
    }

    // Create a TCP socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Set up the server address.
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        close(sockfd);
        return 1;
    }

    // Connect to the server.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to server %s:%hu\n", server_ip, port);
    printf("Mapping USB devices to outputs (out0..out4).\n");
    printf("Type 'quit' on STDIN to exit.\n");

    // Arrays to hold file descriptors and device paths for USB devices.
    int usb_fds[NUM_OUTPUTS] = { -1, -1, -1, -1, -1 };
    char *usb_paths[NUM_OUTPUTS] = { NULL, NULL, NULL, NULL, NULL };

    if (get_usb_mapping(usb_fds, usb_paths) != 0) {
        close(sockfd);
        return 1;
    }

    // Determine max file descriptor for select().
    fd_set readfds;
    int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (usb_fds[i] > maxfd) {
            maxfd = usb_fds[i];
        }
    }

    char buffer[BUFFER_SIZE];
    int running = 1;
    while (running) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            if (usb_fds[i] != -1) {
                FD_SET(usb_fds[i], &readfds);
            }
        }

        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // Check for data from server.
        if (FD_ISSET(sockfd, &readfds)) {
            ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                printf("Server disconnected or an error occurred.\n");
                break;
            }
            buffer[n] = '\0';
            printf("Server: %s", buffer);
        }

        // Check for user input.
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                printf("Exiting.\n");
                break;
            }
            trim_newline(buffer);
            if (strcmp(buffer, "quit") == 0) {
                running = 0;
            }
        }

        // Check each USB device for incoming data.
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            if (usb_fds[i] != -1 && FD_ISSET(usb_fds[i], &readfds)) {
                unsigned char usb_buf[BUFFER_SIZE];
                ssize_t n = read(usb_fds[i], usb_buf, sizeof(usb_buf));
                if (n > 0) {
                    // Get current timestamp.
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                    
                    // Convert the raw payload to a hex string.
                    char payload[BUFFER_SIZE];
                    int offset = 0;
                    for (ssize_t j = 0; j < n && offset < (int)sizeof(payload) - 3; j++) {
                        offset += snprintf(payload + offset, sizeof(payload) - offset, " %02X", usb_buf[j]);
                    }
                    
                    // Print the new payload as a new line with timestamp and device info.
                    printf("%s [%s]:%s\n", time_str, usb_paths[i], payload);
                    fflush(stdout);
                    
                    // Also send the payload to the server, prefixed with outN:
                    char send_msg[BUFFER_SIZE * 2];
                    snprintf(send_msg, sizeof(send_msg), "out%d:%s\n", i, payload);
                    if (send(sockfd, send_msg, strlen(send_msg), 0) < 0) {
                        perror("send");
                        running = 0;
                        break;
                    }
                } else if (n < 0 && errno != EAGAIN) {
                    fprintf(stderr, "Error reading from %s: %s\n", usb_paths[i], strerror(errno));
                }
            }
        }
    }

    // Clean up: close the socket and any open USB devices.
    close(sockfd);
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (usb_fds[i] != -1) {
            close(usb_fds[i]);
            free(usb_paths[i]);
        }
    }
    return 0;
}
