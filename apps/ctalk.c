/*
 * ctalk.c - A minimal MVP chat application for a local network.
 *
 * Fixed version with output buffering for non-blocking sockets.
 *
 * Design:
 *   - Each client_t now has an output buffer (outbuf) to hold unsent data.
 *   - When send() returns EAGAIN/EWOULDBLOCK or sends only part of a message,
 *     the unsent data is buffered.
 *   - The main select() loop now monitors for write-readiness on client sockets
 *     (or the single socket on the client side) so that buffered data can be flushed.
 *
 * Compile with: gcc -std=c11 -pthread -o ctalk ctalk.c
 *
 * Note: This is a simple solution and does not implement a fully robust buffering mechanism.
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

/* Structure to hold connected client info.
 * An output buffer (outbuf) is used to store messages that could not be sent immediately.
 */
typedef struct {
    int sockfd;
    char username[64];
    char *outbuf;         // Pointer to output buffer
    size_t outbuf_len;    // Total size of the buffer
    size_t outbuf_sent;   // Bytes already sent from the buffer
} client_t;

client_t *clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Flush as much of a client's output buffer as possible.
 * If an error occurs, the caller should remove the client.
 */
void flush_client_buffer(client_t *client) {
    while (client->outbuf_sent < client->outbuf_len) {
        ssize_t n = send(client->sockfd, client->outbuf + client->outbuf_sent,
                         client->outbuf_len - client->outbuf_sent, 0);
        if (n > 0) {
            client->outbuf_sent += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            perror("send");
            break;
        }
    }
    if (client->outbuf_sent == client->outbuf_len) {
        free(client->outbuf);
        client->outbuf = NULL;
        client->outbuf_len = 0;
        client->outbuf_sent = 0;
    }
}

/* Append message to a client's output buffer.
 * The unsent data is preserved and the function attempts to flush the buffer immediately.
 */
void append_to_client_buffer(client_t *client, const char *msg) {
    size_t msg_len = strlen(msg);
    size_t pending = client->outbuf_len - client->outbuf_sent;
    size_t new_size = pending + msg_len;
    char *newbuf = malloc(new_size);
    if (!newbuf) return;
    if (pending > 0) {
        memcpy(newbuf, client->outbuf + client->outbuf_sent, pending);
    }
    memcpy(newbuf + pending, msg, msg_len);
    free(client->outbuf);
    client->outbuf = newbuf;
    client->outbuf_len = new_size;
    client->outbuf_sent = 0;
    flush_client_buffer(client);
}

/* Broadcast a message to all connected clients.
 * Each client gets the message appended to its output buffer.
 */
void broadcast_message(const char *msg, int exclude_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->sockfd != exclude_fd) {
            append_to_client_buffer(clients[i], msg);
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
            free(clients[i]->outbuf);
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
    fprintf(stdout, "[INFO] Starting UDP broadcast thread: advertising ctalk server on UDP port %d every %d seconds.\n", UDP_PORT, BROADCAST_INTERVAL);
    while (1) {
        if (sendto(udp_sock, broadcast_msg, strlen(broadcast_msg), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
            perror("sendto");
        }
        sleep(BROADCAST_INTERVAL);
    }
    close(udp_sock);
    pthread_exit(NULL);
}

/* Server mode: Accept connections, relay messages, and handle local input.
 * Uses select() to multiplex the listening socket, STDIN, client sockets for reading,
 * and also monitors client sockets for writability when output buffers are not empty.
 */
void run_server(const char *username) {
    printf("[INFO] Starting ctalk server as '%s' on TCP port %d...\n", username, TCP_PORT);

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

    /* Start UDP broadcast thread so that clients can discover this server. */
    pthread_t udp_thread;
    if (pthread_create(&udp_thread, NULL, udp_broadcast_thread, NULL) != 0) {
        perror("pthread_create");
    }

    int maxfd;
    char buf[BUF_SIZE];

    while (1) {
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(listen_sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        maxfd = listen_sock;
        if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;
        
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i]->sockfd, &read_fds);
            if (clients[i]->sockfd > maxfd)
                maxfd = clients[i]->sockfd;
            /* Monitor for writability if there is pending data */
            if (clients[i]->outbuf && clients[i]->outbuf_len > clients[i]->outbuf_sent) {
                FD_SET(clients[i]->sockfd, &write_fds);
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        int activity = select(maxfd + 1, &read_fds, &write_fds, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        /* First, flush output buffers for clients that are writable. */
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            if (FD_ISSET(clients[i]->sockfd, &write_fds)) {
                flush_client_buffer(clients[i]);
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        /* Accept new connections. */
        if (FD_ISSET(listen_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addrlen);
            if (client_sock < 0) {
                perror("accept");
            } else {
                int flags = fcntl(client_sock, F_GETFL, 0);
                if (flags == -1) flags = 0;
                fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
                fprintf(stdout, "[INFO] Accepted connection from %s, socket %d set to non-blocking mode.\n",
                        inet_ntoa(client_addr.sin_addr), client_sock);

                memset(buf, 0, BUF_SIZE);
                int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        fprintf(stderr, "[WARN] No initial data from client on socket %d. Waiting...\n", client_sock);
                    } else {
                        close(client_sock);
                    }
                } else {
                    client_t *client = malloc(sizeof(client_t));
                    if (!client) {
                        perror("malloc");
                        close(client_sock);
                    } else {
                        client->sockfd = client_sock;
                        strncpy(client->username, buf, sizeof(client->username) - 1);
                        client->username[sizeof(client->username) - 1] = '\0';
                        client->outbuf = NULL;
                        client->outbuf_len = 0;
                        client->outbuf_sent = 0;
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

        /* Handle local input from server user. */
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

        /* Handle incoming messages from clients. */
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            int sd = clients[i]->sockfd;
            if (FD_ISSET(sd, &read_fds)) {
                memset(buf, 0, BUF_SIZE);
                int n = recv(sd, buf, sizeof(buf) - 1, 0);
                if (n == 0) {
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    char leave_msg[BUF_SIZE];
                    snprintf(leave_msg, sizeof(leave_msg), "[%s] %s left the chat.\n", timestamp, clients[i]->username);
                    printf("%s", leave_msg);
                    broadcast_message(leave_msg, sd);
                    close(sd);
                    remove_client(sd);
                    i--;
                } else if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    } else {
                        perror("recv");
                        char timestamp[64];
                        get_timestamp(timestamp, sizeof(timestamp));
                        char leave_msg[BUF_SIZE];
                        snprintf(leave_msg, sizeof(leave_msg), "[%s] %s left the chat (error).\n", timestamp, clients[i]->username);
                        printf("%s", leave_msg);
                        broadcast_message(leave_msg, sd);
                        close(sd);
                        remove_client(sd);
                        i--;
                    }
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
 * This version uses an output buffer for unsent messages and monitors the socket for writability.
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
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    fprintf(stdout, "[INFO] Client socket set to non-blocking mode.\n");

    /* Send the client's username as the first message. */
    send(sock, username, strlen(username), 0);
    printf("[INFO] Connected to ctalk server at %s\n", server_ip);

    char buf[BUF_SIZE];
    char *outbuf = NULL;
    size_t outbuf_len = 0;
    size_t outbuf_sent = 0;
    int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

    while (1) {
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);
        if (outbuf && outbuf_len > outbuf_sent) {
            FD_SET(sock, &write_fds);
        }
        int activity = select(maxfd + 1, &read_fds, &write_fds, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }
        /* Flush outgoing buffer if the socket is writable. */
        if (outbuf && FD_ISSET(sock, &write_fds)) {
            while (outbuf_sent < outbuf_len) {
                ssize_t n = send(sock, outbuf + outbuf_sent, outbuf_len - outbuf_sent, 0);
                if(n > 0) {
                    outbuf_sent += n;
                } else if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                } else {
                    perror("send");
                    break;
                }
            }
            if (outbuf_sent == outbuf_len) {
                free(outbuf);
                outbuf = NULL;
                outbuf_len = 0;
                outbuf_sent = 0;
            }
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                size_t msg_len = strlen(buf);
                ssize_t n = send(sock, buf, msg_len, 0);
                if(n < 0) {
                    if(errno == EAGAIN || errno == EWOULDBLOCK) {
                        size_t pending = outbuf ? (outbuf_len - outbuf_sent) : 0;
                        size_t new_size = pending + msg_len;
                        char *newbuf = malloc(new_size);
                        if(newbuf) {
                            if(outbuf && pending > 0) {
                                memcpy(newbuf, outbuf + outbuf_sent, pending);
                                free(outbuf);
                            }
                            memcpy(newbuf + pending, buf, msg_len);
                            outbuf = newbuf;
                            outbuf_len = new_size;
                            outbuf_sent = 0;
                        }
                    } else {
                        perror("send");
                    }
                }
            }
        }
        if (FD_ISSET(sock, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n == 0) {
                printf("[WARN] Disconnected from server.\n");
                break;
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                } else {
                    perror("recv");
                    break;
                }
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
    fprintf(stdout, "[INFO] Listening for ctalk server broadcast on UDP port %d (timeout 10 seconds)...\n", UDP_PORT);
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
        perror("setsockopt");
        close(udp_sock);
        return NULL;
    }
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
            fprintf(stdout, "[INFO] Received ctalk server broadcast from %s\n", server_ip);
        }
    }
    close(udp_sock);
    return server_ip;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s username\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *username = argv[1];

    fprintf(stdout, "[INFO] Starting ctalk instance with username '%s'\n", username);
    char *server_ip = discover_server();
    if (server_ip != NULL) {
        printf("[INFO] Discovered ctalk server at %s. Joining as client...\n", server_ip);
        run_client(username, server_ip);
        free(server_ip);
        exit(EXIT_SUCCESS);
    }
    printf("[INFO] No ctalk server discovered. Becoming the server...\n");
    run_server(username);
    return 0;
}
