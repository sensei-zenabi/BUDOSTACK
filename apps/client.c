/*
 * client_template.c
 *
 * This file is a TEMPLATE for creating new TCP client applications
 * that communicate with the "Switchboard" server (or any similar server).
 *
 * Usage:
 *   ./client_template [server_ip] [port]
 *
 * Default server_ip is "127.0.0.1" and default port is 12345.
 *
 * Functionality:
 *   - Reads from standard input (stdin).
 *   - Writes messages to the server on the standard 5 output channels (out0..out4).
 *   - Prints any messages received (on the corresponding 5 input channels) to stdout.
 *
 * Message format:
 *   outN: message
 *   where N is a digit 0-4, representing the output channel.
 *
 * Design Principles:
 *   - Plain C, compiled with -std=c11.
 *   - Single-file implementation (no header files).
 *   - Uses only standard C and POSIX libraries.
 *   - Uses select() to multiplex I/O between the network socket and console.
 *   - Respects a simple line-based protocol for client-server messages.
 *
 * Compilation Example:
 *   gcc -std=c11 -Wall -Wextra -pedantic -o client_template client_template.c
 *
 * Run Example:
 *   ./client_template [server_ip] [port]
 *
 * Future Usage:
 *   - If you create a new client program, simply copy/rename this file (e.g. "my_client.c"),
 *     adjust the internal logic as needed (e.g. extra parsing of messages, custom commands),
 *     and compile it in the same manner.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // close(), read(), etc.
#include <arpa/inet.h>    // socket, connect(), etc.
#include <sys/select.h>   // select()
#include <ctype.h>        // isdigit()

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
#define DEFAULT_PORT 12345
#define DEFAULT_IP   "127.0.0.1"
#define BUFFER_SIZE  512

// ---------------------------------------------------------
// Helper function: remove newline chars from a string
// ---------------------------------------------------------
static void trim_newline(char *s) {
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
    p = strchr(s, '\r');
    if (p) *p = '\0';
}

// ---------------------------------------------------------
// Main: Template Client App Entry Point
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    const char *server_ip = DEFAULT_IP;
    unsigned short port   = DEFAULT_PORT;

    // Optionally allow overriding server IP and port via command-line arguments.
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

    // Set up the server address structure.
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        close(sockfd);
        return 1;
    }

    // Connect to the Switchboard server.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to server %s:%hu\n", server_ip, port);
    printf("Enter messages in the format 'outN: message' (N = 0..4).\n");
    printf("Press Ctrl+D to exit.\n");

    // Set up the event loop to monitor both the server socket and standard input.
    fd_set readfds;
    int maxfd = (sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO);
    char buffer[BUFFER_SIZE];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // Check for data from the server.
        if (FD_ISSET(sockfd, &readfds)) {
            ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                printf("Server disconnected or an error occurred.\n");
                break;
            }
            buffer[n] = '\0';
            // Print server messages directly to stdout.
            printf("%s", buffer);
        }

        // Check for user input from stdin.
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                // End-of-file detected (Ctrl+D)
                printf("Exiting.\n");
                break;
            }
            trim_newline(buffer);

            if (strlen(buffer) == 0) {
                continue; // ignore empty lines
            }

            // Validate that the message begins with "out" followed by a digit.
            // Example: "out0: Hello"
            if (strncmp(buffer, "out", 3) == 0 && isdigit((unsigned char)buffer[3])) {
                size_t len = strlen(buffer);
                // Append a newline for protocol compliance.
                buffer[len]     = '\n';
                buffer[len + 1] = '\0';
                if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
                    perror("send");
                    break;
                }
            } else {
                printf("Invalid format. Use 'outN: message' where N is 0-4.\n");
            }
        }
    }

    close(sockfd);
    return 0;
}
