/*
 * ctalk.c - UDP-only chat application for a local network.
 *
 * Design Principles:
 *   - The server uses a UDP socket bound to UDP_CHAT_PORT (60000) to receive messages.
 *   - When a message is received from an unknown client, it is treated as a registration
 *     (the username) and the client is added to a global list.
 *   - All subsequent messages from a client are broadcast to all registered clients,
 *     prefixed with a timestamp (using local machine time) and the sender’s username.
 *   - A separate UDP discovery thread continuously broadcasts "CTALK_SERVER" on UDP_DISCOVERY_PORT (60001)
 *     so that clients can automatically discover the server.
 *   - The client uses a UDP socket to send its registration and chat messages to the server,
 *     and uses select() to multiplex between STDIN (for user input) and incoming UDP messages.
 *   - Both server and client filter out messages originating from themselves.
 *   - When sending a message, the local copy is not immediately printed; it is only shown when
 *     received via UDP (and filtered out on the local side). This avoids duplicate display.
 *   - The prompt ">> " is reprinted after every message output (whether sent or received)
 *     to preserve user input visibility even when multiple sequential messages occur.
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

#define UDP_CHAT_PORT 60000         // UDP port used for chat messages
#define UDP_DISCOVERY_PORT 60001    // UDP port used for server discovery broadcasts
#define DISCOVERY_INTERVAL 5        // Seconds between UDP discovery broadcasts
#define BUF_SIZE 1024
#define MAX_CLIENTS  FD_SETSIZE

typedef struct {
    struct sockaddr_in addr;
    char username[64];
} client_t;

client_t *clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Global variable holding the local username (whether acting as server operator or client) */
const char *my_username = NULL;

/* Check if a client is already registered by comparing IP and port */
int find_client(struct sockaddr_in *addr) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i]->addr.sin_port == addr->sin_port) {
            return i;
        }
    }
    return -1;
}

/* Add a new client */
void add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
        clients[client_count++] = client;
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Broadcast a message to all registered clients except (optionally) the sender.
 * Note: The broadcast message is sent via separate UDP sockets.
 */
void broadcast_message(const char *msg, struct sockaddr_in *exclude) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (exclude != NULL) {
            if (clients[i]->addr.sin_addr.s_addr == exclude->sin_addr.s_addr &&
                clients[i]->addr.sin_port == exclude->sin_port) {
                continue;
            }
        }
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("socket");
            continue;
        }
        if (sendto(sock, msg, strlen(msg), 0,
                   (struct sockaddr *)&clients[i]->addr, sizeof(clients[i]->addr)) < 0) {
            perror("sendto");
        }
        close(sock);
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Get a formatted timestamp string using local machine time */
void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* Helper to ensure message ends with a newline */
void ensure_newline(char *msg, size_t size) {
    size_t len = strlen(msg);
    if (len > 0 && msg[len - 1] != '\n') {
        if (len + 1 < size) {
            msg[len] = '\n';
            msg[len + 1] = '\0';
        }
    }
}

/* UDP discovery broadcaster thread.
 * This thread continuously broadcasts "CTALK_SERVER" on UDP_DISCOVERY_PORT
 * so that clients can discover the chat server.
 */
void *udp_discovery_thread(void *arg) {
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
    broadcast_addr.sin_port = htons(UDP_DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    const char *broadcast_msg = "CTALK_SERVER";
    fprintf(stdout, "[INFO] Starting UDP discovery broadcast on port %d every %d seconds.\n", UDP_DISCOVERY_PORT, DISCOVERY_INTERVAL);
    while (1) {
        if (sendto(udp_sock, broadcast_msg, strlen(broadcast_msg), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
            perror("sendto");
        }
        sleep(DISCOVERY_INTERVAL);
    }
    close(udp_sock);
    pthread_exit(NULL);
}

/* Server mode: Listen for UDP chat messages and broadcast them.
 * The first message from a new client is assumed to be its username registration.
 * Subsequent messages are prefixed with a timestamp and the sender's username.
 * In addition, the server monitors STDIN so that the operator can send messages.
 * To avoid echoing its own messages, both operator and incoming messages are filtered based on my_username.
 * Also, the prompt ">> " is reprinted after each message to keep the input at the bottom.
 */
void run_server(void) {
    printf("[INFO] Starting ctalk server (UDP-only) on chat port %d...\n", UDP_CHAT_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(UDP_CHAT_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* Start UDP discovery broadcast thread */
    pthread_t disc_thread;
    if (pthread_create(&disc_thread, NULL, udp_discovery_thread, NULL) != 0) {
        perror("pthread_create");
    }

    char buf[BUF_SIZE];
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        int activity = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        /* Handle local server operator input */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                char message[BUF_SIZE];
                snprintf(message, sizeof(message), "[%s] %s - %s", timestamp, my_username, buf);
                ensure_newline(message, sizeof(message));
                broadcast_message(message, NULL);
                /* Reprint prompt after sending a message */
                printf(">> ");
                fflush(stdout);
            }
        }

        /* Handle incoming UDP messages from clients */
        if (FD_ISSET(sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            memset(buf, 0, BUF_SIZE);
            ssize_t n = recvfrom(sock, buf, BUF_SIZE - 1, 0,
                                 (struct sockaddr *)&client_addr, &addrlen);
            if (n < 0) {
                perror("recvfrom");
                continue;
            }
            buf[n] = '\0';

            /* Filter out messages that are echoes of our own (comparing against my_username). */
            if (strstr(buf, my_username) != NULL && strstr(buf, " - ") != NULL) {
                char *start = strchr(buf, ']');
                if (start) {
                    start++; // skip ']'
                    while (*start == ' ') start++;
                    char sender[64];
                    char *dash = strstr(start, " - ");
                    if (dash && (size_t)(dash - start) < sizeof(sender)) {
                        memcpy(sender, start, dash - start);
                        sender[dash - start] = '\0';
                        if (strcmp(sender, my_username) == 0) {
                            continue;
                        }
                    }
                }
            }

            int idx = find_client(&client_addr);
            if (idx < 0) {
                /* New client registration; treat the first message as the username.
                 * Ignore if the username contains our own username.
                 */
                if (strstr(buf, my_username) != NULL) {
                    continue;
                }
                client_t *client = malloc(sizeof(client_t));
                if (!client) {
                    perror("malloc");
                    continue;
                }
                client->addr = client_addr;
                strncpy(client->username, buf, sizeof(client->username) - 1);
                client->username[sizeof(client->username) - 1] = '\0';
                add_client(client);

                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                char join_msg[BUF_SIZE];
                snprintf(join_msg, sizeof(join_msg), "[%s] %s joined the chat.\n", timestamp, client->username);
                printf("%s", join_msg);
                broadcast_message(join_msg, &client_addr);
                /* Reprint prompt */
                printf(">> ");
                fflush(stdout);
            } else {
                /* Regular chat message from a known client */
                char timestamp[64];
                get_timestamp(timestamp, sizeof(timestamp));
                char formatted[BUF_SIZE];
                pthread_mutex_lock(&clients_mutex);
                const char *sender = clients[idx]->username;
                pthread_mutex_unlock(&clients_mutex);
                snprintf(formatted, sizeof(formatted), "[%s] %s - %s", timestamp, sender, buf);
                ensure_newline(formatted, sizeof(formatted));
                printf("%s", formatted);
                broadcast_message(formatted, &client_addr);
                /* Reprint prompt */
                printf(">> ");
                fflush(stdout);
            }
        }
    }
    close(sock);
    exit(EXIT_SUCCESS);
}

/* Client mode: Discover the server, register the username, and exchange chat messages.
 * The client sends its username as the first message, then uses select() to handle STDIN and
 * incoming UDP messages.
 * When a message is received, the client checks the sender's username from the formatted message.
 * If the sender matches the client's username, the message is skipped.
 * The prompt ">> " is reprinted after each received message to keep the input at the bottom.
 */
void run_client(const char *username, const char *server_ip) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    /* Set server address for chat messages */
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(UDP_CHAT_PORT);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }
    /* Send registration: the username */
    if (sendto(sock, username, strlen(username), 0,
               (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("sendto");
    }
    printf("[INFO] Registered as '%s' with ctalk server at %s\n", username, server_ip);
    printf(">> ");
    fflush(stdout);

    int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
    char buf[BUF_SIZE];

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);
        int activity = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }
        /* Check for local input */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(buf, 0, BUF_SIZE);
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                if (sendto(sock, buf, strlen(buf), 0,
                           (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                    perror("sendto");
                }
                /* Reprint prompt after sending a message */
                printf(">> ");
                fflush(stdout);
            }
        }
        /* Check for incoming messages */
        if (FD_ISSET(sock, &read_fds)) {
            struct sockaddr_in from_addr;
            socklen_t addrlen = sizeof(from_addr);
            memset(buf, 0, BUF_SIZE);
            ssize_t n = recvfrom(sock, buf, BUF_SIZE - 1, 0,
                                 (struct sockaddr *)&from_addr, &addrlen);
            if (n < 0) {
                perror("recvfrom");
                continue;
            }
            buf[n] = '\0';
            /* Parse the formatted message to extract the sender's username.
             * Expected format: "[timestamp] <username> - <message>"
             */
            char *start = strchr(buf, ']');
            if (start) {
                start++; // skip ']'
                while (*start == ' ') start++; // skip spaces
                char sender[64];
                char *dash = strstr(start, " - ");
                if (dash && (size_t)(dash - start) < sizeof(sender)) {
                    memcpy(sender, start, dash - start);
                    sender[dash - start] = '\0';
                    if (strcmp(sender, username) == 0) {
                        /* Skip our own echoed message */
                        continue;
                    }
                }
            }
            ensure_newline(buf, sizeof(buf));
            printf("%s", buf);
            /* Reprint prompt to keep input at the bottom */
            printf(">> ");
            fflush(stdout);
        }
    }
    close(sock);
    exit(EXIT_SUCCESS);
}

/* Discover the server by listening for a UDP broadcast on UDP_DISCOVERY_PORT.
 * If a "CTALK_SERVER" broadcast is received within 10 seconds, returns the sender's IP.
 */
char *discover_server(void) {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket");
        return NULL;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_DISCOVERY_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(udp_sock);
        return NULL;
    }
    fprintf(stdout, "[INFO] Listening for ctalk server broadcast on UDP port %d (timeout 10 seconds)...\n", UDP_DISCOVERY_PORT);
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
    my_username = username;
    fprintf(stdout, "[INFO] Starting ctalk instance with username '%s'\n", username);

    char *server_ip = discover_server();
    if (server_ip != NULL) {
        printf("[INFO] Discovered ctalk server at %s. Joining as client...\n", server_ip);
        run_client(username, server_ip);
        free(server_ip);
        exit(EXIT_SUCCESS);
    }
    printf("[INFO] No ctalk server discovered. Becoming the server...\n");
    run_server();
    return 0;
}
