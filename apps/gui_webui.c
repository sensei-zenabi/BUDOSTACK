/*
 * Single-file minimal HTTP server to control tmux windows on a Raspberry Pi.
 *
 * Design Principles:
 * - Single file: All functionality is implemented in one file.
 * - Plain C: Written in C11 using only standard libraries and POSIX APIs.
 * - Minimal HTTP server: Listens on port 8080 and serves an HTML page with a "NEXT" button.
 * - Command execution: Pressing the "NEXT" button calls system() with the custom tmux command:
 *       tmux -S /tmp/tmux_server.sock next-window -t server
 *
 * Build Instructions:
 *   gcc -std=c11 -o webtmux server.c
 *
 * Usage:
 *   ./webtmux
 *
 * Then, in your home network, open a browser and navigate to:
 *   http://<raspberry_pi_ip>:8080
 *
 * Note:
 * - Ensure that tmux is running with the socket /tmp/tmux_server.sock and a session named "server".
 * - For production use, add proper error handling and security measures.
 * - The server runs indefinitely and uses blocking socket calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUF_SIZE 4096

// HTML page with a "NEXT" button. The button links to the /next endpoint.
const char *html_page =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n\r\n"
"<!DOCTYPE html>"
"<html>"
"<head><title>tmux Controller</title></head>"
"<body>"
"<h1>tmux Controller</h1>"
"<form action=\"/next\" method=\"get\">"
"<button type=\"submit\">NEXT</button>"
"</form>"
"</body>"
"</html>";

// HTML response after executing the tmux command.
const char *html_response =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n\r\n"
"<!DOCTYPE html>"
"<html>"
"<head><title>tmux Controller</title></head>"
"<body>"
"<h1>tmux command executed!</h1>"
"<a href=\"/\">Back</a>"
"</body>"
"</html>";

int main(void) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUF_SIZE] = {0};

    // Create a socket file descriptor.
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Allow the port to be reused immediately after the server terminates.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to all interfaces on the defined port.
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces.
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections.
    if (listen(server_fd, 3) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server running on port %d\n", PORT);

    // Main loop: accept and process incoming connections.
    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept failed");
            continue;
        }

        // Read the HTTP request.
        int valread = read(new_socket, buffer, BUF_SIZE - 1);
        if (valread < 0) {
            perror("read failed");
            close(new_socket);
            continue;
        }
        buffer[valread] = '\0';

        // Check if the request is for the /next endpoint.
        if (strstr(buffer, "GET /next") != NULL) {
            // Execute the tmux command to switch to the next window using the custom socket and session.
            int ret = system("tmux -S /tmp/tmux_server.sock next-window -t server");
            if (ret == -1) {
                perror("system call failed");
            }
            // Respond with a confirmation HTML page.
            send(new_socket, html_response, strlen(html_response), 0);
        } else {
            // Serve the main page with the NEXT button.
            send(new_socket, html_page, strlen(html_page), 0);
        }

        // Close the connection.
        close(new_socket);
    }

    close(server_fd);
    return 0;
}
