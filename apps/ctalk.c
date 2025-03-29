/*
 * ctalk.c - UDP-only chat application for a local network.
 *
 * Design Principles:
 *   - Uses a UDP socket for chat messages and a separate thread for periodic discovery broadcasts.
 *   - Uses raw terminal (non-canonical) mode to allow character-by-character input so that incoming
 *     messages do not disturb the current user input.
 *   - When an incoming message is received, the current input is cleared (using ANSI escape sequences),
 *     the message is printed, and then the prompt (with any partially typed input) is reprinted.
 *   - In client mode, when the user sends a message, it is immediately printed locally (with a timestamp)
 *     so that the user sees it, and the subsequent network echo (which is filtered) does not cause duplicates.
 *   - Only plain C (compiled with -std=c11) and POSIX-compliant functions are used.
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
#include <termios.h>
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

/* Global variable holding the local username */
const char *my_username = NULL;

/* Terminal raw mode management */
static struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    // Disable canonical mode and echo
    raw.c_lflag &= ~(ICANON | ECHO);
    // Read one byte at a time
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

/* Helper to clear current line and reprint prompt with current input */
void reprint_prompt(const char *buf) {
    // \r returns to start of line, \33[2K clears the line.
    printf("\r\33[2K>> %s", buf);
    fflush(stdout);
}

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

/* Broadcast a message to all registered clients except (optionally) the sender */
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

/* Get a formatted timestamp string */
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

/* UDP discovery broadcaster thread */
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

/*
 * Server mode: Listen for UDP chat messages and broadcast them.
 * The operator uses a raw-mode line editor so that incoming messages do not disturb the current input.
 */
void run_server(void) {
    printf("[INFO] Starting ctalk server (UDP-only) on chat port %d...\n", UDP_CHAT_PORT);
    enable_raw_mode();

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
    char input_buf[BUF_SIZE] = {0};
    int input_len = 0;

    /* Print initial prompt */
    printf(">> ");
    fflush(stdout);

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

        /* Handle operator input (non-canonical, character-by-character) */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread < 0) {
                perror("read");
                continue;
            }
            if (c == '\n' || c == '\r') {
                input_buf[input_len] = '\0';
                if (input_len > 0) {
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    char message[BUF_SIZE];
                    snprintf(message, sizeof(message), "[%s] %s - %s", timestamp, my_username, input_buf);
                    ensure_newline(message, sizeof(message));
                    broadcast_message(message, NULL);
                    // Print own message immediately.
                    printf("\r\33[2K%s", message);
                }
                input_len = 0;
                input_buf[0] = '\0';
                printf(">> ");
                fflush(stdout);
            } else if (c == 127 || c == '\b') { // backspace
                if (input_len > 0) {
                    input_len--;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
            } else {
                if (input_len < BUF_SIZE - 1) {
                    input_buf[input_len++] = c;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
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

            /* Filter out echo messages from our own username */
            if (strstr(buf, my_username) != NULL && strstr(buf, " - ") != NULL) {
                char *start = strchr(buf, ']');
                if (start) {
                    start++;
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
                /* New client registration */
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
                printf("\r\33[2K%s", join_msg);
                reprint_prompt(input_buf);
                broadcast_message(join_msg, &client_addr);
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
                printf("\r\33[2K%s", formatted);
                reprint_prompt(input_buf);
                broadcast_message(formatted, &client_addr);
            }
        }
    }
    close(sock);
    exit(EXIT_SUCCESS);
}

/*
 * Client mode: Discover the server, register the username, and exchange chat messages.
 * The client uses a raw-mode line editor so that incoming messages do not disturb the partial input.
 * In this update, when the user sends a message, it is immediately formatted and printed locally.
 */
void run_client(const char *username, const char *server_ip) {
    enable_raw_mode();
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(UDP_CHAT_PORT);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }
    /* Send registration (username) */
    if (sendto(sock, username, strlen(username), 0,
               (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("sendto");
    }
    printf("[INFO] Registered as '%s' with ctalk server at %s\n", username, server_ip);

    char buf[BUF_SIZE];
    char input_buf[BUF_SIZE] = {0};
    int input_len = 0;
    printf(">> ");
    fflush(stdout);

    int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

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
        /* Handle local input */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread < 0) {
                perror("read");
                continue;
            }
            if (c == '\n' || c == '\r') {
                input_buf[input_len] = '\0';
                if (input_len > 0) {
                    if (sendto(sock, input_buf, strlen(input_buf), 0,
                               (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                        perror("sendto");
                    }
                    // Format and immediately print the client's own message.
                    char timestamp[64];
                    get_timestamp(timestamp, sizeof(timestamp));
                    char message[BUF_SIZE];
                    snprintf(message, sizeof(message), "[%s] %s - %s", timestamp, username, input_buf);
                    ensure_newline(message, sizeof(message));
                    printf("\r\33[2K%s", message);
                }
                input_len = 0;
                input_buf[0] = '\0';
                printf(">> ");
                fflush(stdout);
            } else if (c == 127 || c == '\b') { // backspace
                if (input_len > 0) {
                    input_len--;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
            } else {
                if (input_len < BUF_SIZE - 1) {
                    input_buf[input_len++] = c;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
            }
        }
        /* Handle incoming messages */
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
            /* Parse formatted message to extract sender.
             * Expected format: "[timestamp] <username> - <message>"
             */
            char *start = strchr(buf, ']');
            if (start) {
                start++;
                while (*start == ' ') start++;
                char sender[64];
                char *dash = strstr(start, " - ");
                if (dash && (size_t)(dash - start) < sizeof(sender)) {
                    memcpy(sender, start, dash - start);
                    sender[dash - start] = '\0';
                    // Filter out the echo of our own message (since we print it immediately on send)
                    if (strcmp(sender, username) == 0) {
                        continue;
                    }
                }
            }
            ensure_newline(buf, sizeof(buf));
            printf("\r\33[2K%s", buf);
            reprint_prompt(input_buf);
        }
    }
    close(sock);
    exit(EXIT_SUCCESS);
}

/*
 * Discover the server by listening for a UDP broadcast on UDP_DISCOVERY_PORT.
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
