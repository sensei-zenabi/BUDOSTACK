/*
 * ctalk.c - A minimal MVP chat application for a local network.
 *
 * Design:
 *   - When started with a username (ctalk username), the app first tries to bind
 *     a TCP listening socket on a well-known port (60000). If binding succeeds,
 *     it assumes the role of the server.
 *
 *   - The server spawns a UDP broadcast thread that periodically sends a fixed
 *     advertisement ("CTALK_SERVER") on UDP port 60001.
 *
 *   - Clients (which fail to bind the TCP port) call discover_server() to listen
 *     on UDP port 60001 for the advertisement and then connect to the server's IP.
 *
 *   - After connecting via TCP, the client immediately sends its username.
 *     Thereafter, both server and client use select() to monitor standard input
 *     (for outgoing messages) and the socket (for incoming messages). All messages
 *     are displayed with a timestamp and sender’s username in an IRC-like view.
 *
 *   - The server accepts new client connections, reads the initial username from
 *     each, and then relays any message received from one client to all the others.
 *
 *   - This implementation uses basic POSIX socket APIs, select() for I/O multiplexing,
 *     and pthread for running the UDP broadcaster concurrently.
 *
 * Compile with: gcc -std=c11 -pthread -o ctalk ctalk.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TCP_PORT 60000         // Fixed TCP port for chat server
#define UDP_PORT 60001         // Fixed UDP port for server discovery
#define BROADCAST_INTERVAL 5   // Seconds between UDP broadcasts
#define BUF_SIZE 1024
#define MAX_CLIENTS FD_SETSIZE

// Structure to hold connected client info.
typedef struct {
    int sockfd;
    char username[64];
} client_t;

client_t *clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Broadcast a message to all connected clients.
 * If exclude_fd is not -1, do not send the message to that socket.
 */
void broadcast_message(const char *msg, int exclude_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->sockfd != exclude_fd) {
            send(clients[i]->sockfd, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Add a new client to the global list. */
void add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
        clients[client_count++] = client;
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Remove a client identified by its socket fd. */
void remove_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->sockfd == sockfd) {
            free(clients[i]);
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Get a formatted timestamp string. */
void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* UDP broadcaster thread for server discovery.
 * Periodically sends "CTALK_SERVER" as a broadcast message.
 */
void *udp_broadcast_thread(void *arg) {
	(void)arg;
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket");
        pthread_exit(NULL);
    }
    int broadcastEnable = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt SO_BROADCAST");
        close(udp_sock);
        pthread_exit(NULL);
    }
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    const char *broadcast_msg = "CTALK_SERVER";
    while (1) {
        sendto(udp_sock, broadcast_msg, strlen(broadcast_msg), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        sleep(BROADCAST_INTERVAL);
    }
    close(udp_sock);
    pthread_exit(NULL);
}

/* Server mode: Accept connections, relay messages, and include local input.
 * Uses select() to multiplex the listening socket, STDIN, and all client sockets.
 */
void run_server(const char *username) {
    printf("Starting ctalk server as '%s' on TCP port %d...\n", username, TCP_PORT);

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }
    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // Start UDP broadcast thread
    pthread_t udp_thread;
    if (pthread_create(&udp_thread, NULL, udp_broadcast_thread, NULL) != 0) {
        perror("pthread_create");
        // Continue even if broadcasting fails
    }

    fd_set read_fds;
    int maxfd = listen_sock > STDIN_FILENO ? listen_sock : STDIN_FILENO;
    char buf[BUF_SIZE];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i]->sockfd, &read_fds);
            if (clients[i]->sockfd > maxfd)
                maxfd = clients[i]->sockfd;
        }
        pthread_mutex_unlock(&clients_mutex);

        int activity = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // New client connection
        if (FD_ISSET(listen_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addrlen);
            if (client_sock < 0) {
                perror("accept");
            } else {
                // Expect the client to send its username as the first message
                memset(buf, 0, BUF_SIZE);
                int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    close(client_sock);
                } else {
                    client_t *client = malloc(sizeof(client_t));
                    if (!client) {
                        perror("malloc");
                        close(client_sock);
                    } else {
                        client->sockfd = client_sock;
                        strncpy(client->username, buf, sizeof(client->username) - 1);
                        client->username[sizeof(client->username) - 1] = '\0';
                        add_client(client);

                        char timestamp[64];
                        get_timestamp(timestamp, sizeof(timestamp));
                        char join_msg[BUF_SIZE];
                        snprintf(join_msg, sizeof(join_msg), "[%s] %s joined the chat.\n", timestamp, client->username);
                        printf("%s", join_msg);
                        broadcast_message(join_msg, client_sock);
                    }
                }
            }
        }

        // Local input from server user
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                char message[BUF_SIZE];
                snprintf(message, sizeof(message), "[%s] %s - %s", timestamp, username, buf);
                printf("%s", message);
                broadcast_message(message, -1);
            }
        }

        // Check each client socket for incoming messages
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            int sd = clients[i]->sockfd;
            if (FD_ISSET(sd, &read_fds)) {
                memset(buf, 0, BUF_SIZE);
                int n = recv(sd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    char leave_msg[BUF_SIZE];
                    snprintf(leave_msg, sizeof(leave_msg), "[%s] %s left the chat.\n", timestamp, clients[i]->username);
                    printf("%s", leave_msg);
                    broadcast_message(leave_msg, sd);
                    close(sd);
                    remove_client(sd);
                    i--; // Adjust index since we removed a client
                } else {
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    char formatted[BUF_SIZE];
                    snprintf(formatted, sizeof(formatted), "[%s] %s - %s", timestamp, clients[i]->username, buf);
                    printf("%s", formatted);
                    broadcast_message(formatted, sd);
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    close(listen_sock);
    exit(EXIT_SUCCESS);
}

/* Client mode: Discover the server via UDP broadcast, connect via TCP,
 * send the client’s username, and then use select() to handle incoming and outgoing messages.
 */
void run_client(const char *username, const char *server_ip) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }
    // Send the client's username as the first message.
    send(sock, username, strlen(username), 0);
    printf("Connected to ctalk server at %s\n", server_ip);

    fd_set read_fds;
    int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
    char buf[BUF_SIZE];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        int activity = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }
        // Handle local input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                send(sock, buf, strlen(buf), 0);
            }
        }
        // Handle incoming messages from server
        if (FD_ISSET(sock, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                printf("Disconnected from server.\n");
                break;
            } else {
                printf("%s", buf);
            }
        }
    }
    close(sock);
    exit(EXIT_SUCCESS);
}

/* Client UDP discovery: Listens for a UDP broadcast message ("CTALK_SERVER")
 * on UDP_PORT and returns the sender's IP address as a string.
 * Caller is responsible for freeing the returned string.
 */
char *discover_server() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket");
        return NULL;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(udp_sock);
        return NULL;
    }
    // Set a 5-second timeout on recvfrom()
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    char buffer[128];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    int n = recvfrom(udp_sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&sender, &sender_len);
    if (n < 0) {
        perror("recvfrom");
        close(udp_sock);
        return NULL;
    }
    buffer[n] = '\0';
    char *server_ip = NULL;
    if (strcmp(buffer, "CTALK_SERVER") == 0) {
        server_ip = malloc(INET_ADDRSTRLEN);
        if (server_ip) {
            inet_ntop(AF_INET, &(sender.sin_addr), server_ip, INET_ADDRSTRLEN);
        }
    }
    close(udp_sock);
    return server_ip;
}

/* Main function: expects one argument - the username.
 * It tries to bind a TCP listening socket on TCP_PORT.
 * If binding succeeds, it runs in server mode.
 * Otherwise, it acts as a client by discovering the server via UDP.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s username\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *username = argv[1];

    // Try to bind a TCP socket on TCP_PORT to check if we're the first
    int test_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (test_sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(test_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in test_addr;
    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = AF_INET;
    test_addr.sin_port = htons(TCP_PORT);
    test_addr.sin_addr.s_addr = INADDR_ANY;

    int server_mode = 0;
    if (bind(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr)) == 0) {
        // Binding succeeded: we are the first; run as server.
        server_mode = 1;
        close(test_sock); // Will re-create socket in run_server()
    } else {
        // Binding failed: assume server already exists.
        server_mode = 0;
        close(test_sock);
    }

    if (server_mode) {
        run_server(username);
    } else {
        // Discover the server using UDP broadcast
        char *server_ip = discover_server();
        if (server_ip == NULL) {
            fprintf(stderr, "No ctalk server found in the network.\n");
            exit(EXIT_FAILURE);
        }
        run_client(username, server_ip);
        free(server_ip);
    }
    return 0;
}
