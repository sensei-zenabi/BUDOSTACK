/*
 * ctalk.c - TCP-based multi-channel chat inspired by IRC semantics.
 *
 * This implementation replaces the original UDP broadcast LAN utility with a
 * client/server architecture that works across the public internet.  The
 * server maintains persistent TCP connections, tracks channel membership, and
 * relays messages with timestamps.  Clients operate in raw terminal mode so
 * asynchronous messages do not disrupt in-progress input, mirroring the
 * original user experience while enabling global connectivity.
 *
 * Usage:
 *   Server: ctalk server <bind-address> <port>
 *           (bind-address may be "0.0.0.0" to listen on all interfaces)
 *   Client: ctalk client <username> <server-host> <port>
 *
 * Supported client commands:
 *   /help                 Show command summary.
 *   /join <channel>       Join (or create) a channel.
 *   /who                  List users in the current channel.
 *   /quit                 Disconnect from the server.
 *   Any other text is broadcast to the current channel.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE 2048
#define MAX_CLIENTS FD_SETSIZE
#define MAX_USERNAME_LEN 64
#define MAX_CHANNEL_LEN 64
#define DEFAULT_CHANNEL "lobby"
#define SERVER_BACKLOG 32

static const char *help_text =
    "Available commands:\n"
    "  /help                 Show this help.\n"
    "  /join <channel>       Join or create a channel.\n"
    "  /who                  List members in the current channel.\n"
    "  /quit                 Leave the chat.";

/* -------------------- Shared helpers -------------------- */

static void format_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void trim_trailing_whitespace(char *s)
{
    size_t len = strlen(s);
    while (len > 0) {
        unsigned char c = (unsigned char)s[len - 1];
        if (c == '\n' || c == '\r' || isspace(c)) {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static int send_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_line(int fd, const char *line)
{
    size_t len = strlen(line);
    return send_all(fd, line, len);
}

static int send_line_with_newline(int fd, const char *line)
{
    char buf[BUF_SIZE];
    size_t len = strlen(line);
    if (len > sizeof(buf) - 2) {
        len = sizeof(buf) - 2;
    }
    memcpy(buf, line, len);
    buf[len++] = '\n';
    buf[len] = '\0';
    return send_all(fd, buf, len);
}

/* -------------------- Server implementation -------------------- */

typedef struct {
    int fd;
    bool registered;
    char username[MAX_USERNAME_LEN];
    char channel[MAX_CHANNEL_LEN];
    char buffer[BUF_SIZE];
    size_t buffer_len;
} client_t;

static client_t clients[MAX_CLIENTS];

static void init_clients(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].registered = false;
        clients[i].buffer_len = 0;
        clients[i].username[0] = '\0';
        clients[i].channel[0] = '\0';
    }
}

static client_t *find_client_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            return &clients[i];
        }
    }
    return NULL;
}

static client_t *find_client_by_username(const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].registered &&
            strcmp(clients[i].username, username) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

static void broadcast_channel(const char *channel, const char *message, const client_t *exclude)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1 || !clients[i].registered) {
            continue;
        }
        if (strcmp(clients[i].channel, channel) != 0) {
            continue;
        }
        if (exclude != NULL && clients[i].fd == exclude->fd) {
            continue;
        }
        if (send_line(clients[i].fd, message) < 0) {
            close(clients[i].fd);
            clients[i].fd = -1;
            clients[i].registered = false;
        }
    }
}

static bool is_valid_name(const char *name, size_t max_len)
{
    size_t len = strlen(name);
    if (len == 0 || len >= max_len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isspace(c) || c == ',') {
            return false;
        }
    }
    return true;
}

static void disconnect_client(client_t *client, const char *reason)
{
    if (client->fd == -1) {
        return;
    }
    if (client->registered) {
        char timestamp[64];
        char msg[BUF_SIZE];
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(msg, sizeof(msg), "[%s] %s left channel %s (%s)\n",
                 timestamp, client->username, client->channel,
                 reason != NULL ? reason : "disconnected");
        broadcast_channel(client->channel, msg, client);
    }
    close(client->fd);
    client->fd = -1;
    client->registered = false;
    client->buffer_len = 0;
    client->username[0] = '\0';
    client->channel[0] = '\0';
}

static void send_help(client_t *client)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), "%s\n", help_text);
    if (send_line(client->fd, buf) < 0) {
        disconnect_client(client, "write failure");
    }
}

static void send_who(client_t *client)
{
    char buf[BUF_SIZE];
    size_t offset = 0;
    offset += (size_t)snprintf(buf + offset, sizeof(buf) - offset,
                               "Users in %s: ", client->channel);
    bool first = true;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1 || !clients[i].registered) {
            continue;
        }
        if (strcmp(clients[i].channel, client->channel) != 0) {
            continue;
        }
        if (!first) {
            offset += (size_t)snprintf(buf + offset, sizeof(buf) - offset, ", ");
        }
        first = false;
        offset += (size_t)snprintf(buf + offset, sizeof(buf) - offset, "%s",
                                   clients[i].username);
        if (offset >= sizeof(buf)) {
            break;
        }
    }
    offset += (size_t)snprintf(buf + offset, sizeof(buf) - offset, "\n");
    if (send_line(client->fd, buf) < 0) {
        disconnect_client(client, "write failure");
    }
}

static void handle_join(client_t *client, const char *channel)
{
    if (!is_valid_name(channel, MAX_CHANNEL_LEN)) {
        const char *err = "Channel names must be non-empty, without spaces or commas.\n";
        if (send_line(client->fd, err) < 0) {
            disconnect_client(client, "write failure");
        }
        return;
    }
    if (strcmp(client->channel, channel) == 0) {
        const char *msg = "You are already in that channel.\n";
        if (send_line(client->fd, msg) < 0) {
            disconnect_client(client, "write failure");
        }
        return;
    }
    char timestamp[64];
    char buf[BUF_SIZE];
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(buf, sizeof(buf), "[%s] %s left channel %s\n", timestamp,
             client->username, client->channel);
    broadcast_channel(client->channel, buf, client);
    snprintf(buf, sizeof(buf), "[%s] %s joined channel %s\n", timestamp,
             client->username, channel);
    strncpy(client->channel, channel, sizeof(client->channel) - 1);
    client->channel[sizeof(client->channel) - 1] = '\0';
    broadcast_channel(client->channel, buf, NULL);
}

static void process_client_line(client_t *client, char *line)
{
    trim_trailing_whitespace(line);
    if (client->registered) {
        if (line[0] == '\0') {
            return;
        }
        if (line[0] == '/') {
            if (strcmp(line, "/help") == 0) {
                send_help(client);
                return;
            }
            if (strcmp(line, "/who") == 0) {
                send_who(client);
                return;
            }
            if (strcmp(line, "/quit") == 0) {
                disconnect_client(client, "quit");
                return;
            }
            if (strncmp(line, "/join ", 6) == 0) {
                const char *channel = line + 6;
                handle_join(client, channel);
                return;
            }
            const char *unknown = "Unknown command. Type /help for assistance.\n";
            if (send_line(client->fd, unknown) < 0) {
                disconnect_client(client, "write failure");
            }
            return;
        }
        char timestamp[64];
        char message[BUF_SIZE];
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(message, sizeof(message), "[%s] %s: %s\n", timestamp,
                 client->username, line);
        broadcast_channel(client->channel, message, NULL);
    } else {
        if (!is_valid_name(line, MAX_USERNAME_LEN)) {
            const char *err = "Invalid username. Use up to 63 visible characters without spaces.\n";
            if (send_line(client->fd, err) < 0) {
                disconnect_client(client, "write failure");
            }
            return;
        }
        if (find_client_by_username(line) != NULL) {
            const char *err = "Username already in use. Choose another.\n";
            if (send_line(client->fd, err) < 0) {
                disconnect_client(client, "write failure");
            }
            return;
        }
        strncpy(client->username, line, sizeof(client->username) - 1);
        client->username[sizeof(client->username) - 1] = '\0';
        strncpy(client->channel, DEFAULT_CHANNEL, sizeof(client->channel) - 1);
        client->channel[sizeof(client->channel) - 1] = '\0';
        client->registered = true;
        const char *welcome = "Welcome to ctalk! Type /help for commands.\n";
        if (send_line(client->fd, welcome) < 0) {
            disconnect_client(client, "write failure");
            return;
        }
        char timestamp[64];
        char msg[BUF_SIZE];
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(msg, sizeof(msg), "[%s] %s joined channel %s\n", timestamp,
                 client->username, client->channel);
        broadcast_channel(client->channel, msg, NULL);
    }
}

static void handle_client_io(client_t *client)
{
    char recv_buf[BUF_SIZE];
    ssize_t n = recv(client->fd, recv_buf, sizeof(recv_buf), 0);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        disconnect_client(client, "read failure");
        return;
    }
    if (n == 0) {
        disconnect_client(client, "remote closed");
        return;
    }
    size_t offset = 0U;
    while (offset < (size_t)n) {
        if (client->buffer_len >= sizeof(client->buffer) - 1) {
            const char *err = "Input line too long. Clearing buffer.\n";
            if (send_line(client->fd, err) < 0) {
                disconnect_client(client, "write failure");
                return;
            }
            client->buffer_len = 0;
        }
        size_t to_copy = (size_t)n - offset;
        if (to_copy > sizeof(client->buffer) - 1 - client->buffer_len) {
            to_copy = sizeof(client->buffer) - 1 - client->buffer_len;
        }
        memcpy(client->buffer + client->buffer_len, recv_buf + offset, to_copy);
        client->buffer_len += to_copy;
        offset += to_copy;
        client->buffer[client->buffer_len] = '\0';
        char *newline = strchr(client->buffer, '\n');
        while (newline != NULL) {
            *newline = '\0';
            process_client_line(client, client->buffer);
            if (client->fd == -1) {
                return;
            }
            size_t remaining = client->buffer_len - (size_t)(newline - client->buffer + 1);
            memmove(client->buffer, newline + 1, remaining);
            client->buffer_len = remaining;
            client->buffer[client->buffer_len] = '\0';
            newline = strchr(client->buffer, '\n');
        }
    }
}

static void run_server(const char *bind_addr, const char *port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(bind_addr, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    int listen_fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }
        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt");
            close(listen_fd);
            listen_fd = -1;
            continue;
        }
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) < 0) {
            perror("bind");
            close(listen_fd);
            listen_fd = -1;
            continue;
        }
        if (listen(listen_fd, SERVER_BACKLOG) < 0) {
            perror("listen");
            close(listen_fd);
            listen_fd = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);

    if (listen_fd < 0) {
        fprintf(stderr, "Failed to set up server socket.\n");
        exit(EXIT_FAILURE);
    }

    printf("[INFO] ctalk server listening on %s:%s\n", bind_addr, port);
    init_clients();

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        int max_fd = listen_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > max_fd) {
                    max_fd = clients[i].fd;
                }
            }
        }
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (fd < 0) {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("accept");
                }
            } else {
                client_t *slot = find_client_slot();
                if (slot == NULL) {
                    const char *msg = "Server full. Try again later.\n";
                    send_line(fd, msg);
                    close(fd);
                } else {
                    slot->fd = fd;
                    slot->registered = false;
                    slot->buffer_len = 0;
                    slot->username[0] = '\0';
                    slot->channel[0] = '\0';
                    const char *prompt = "Enter your username:\n";
                    if (send_line(fd, prompt) < 0) {
                        close(fd);
                        slot->fd = -1;
                    }
                }
            }
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1 && FD_ISSET(clients[i].fd, &read_fds)) {
                handle_client_io(&clients[i]);
            }
        }
    }

    close(listen_fd);
}

/* -------------------- Client implementation -------------------- */

static struct termios orig_termios;

static void disable_raw_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

static void reprint_prompt(const char *buf)
{
    printf("\r\33[2K>> %s", buf);
    fflush(stdout);
}

static int connect_to_server(const char *host, const char *port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    return sock;
}

static void run_client(const char *username, const char *host, const char *port)
{
    int sock = connect_to_server(host, port);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", host, port);
        exit(EXIT_FAILURE);
    }

    enable_raw_mode();

    char intro_buf[BUF_SIZE];
    ssize_t intro_len = recv(sock, intro_buf, sizeof(intro_buf) - 1, 0);
    if (intro_len > 0) {
        intro_buf[intro_len] = '\0';
        printf("%s", intro_buf);
    }

    if (send_line_with_newline(sock, username) < 0) {
        perror("send");
        disable_raw_mode();
        close(sock);
        exit(EXIT_FAILURE);
    }

    char input_buf[BUF_SIZE] = {0};
    size_t input_len = 0;
    printf(">> ");
    fflush(stdout);

    char recv_buffer[BUF_SIZE];
    size_t pending_len = 0;
    char pending[BUF_SIZE];

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);
        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread < 0) {
                perror("read");
                break;
            }
            if (c == '\r' || c == '\n') {
                input_buf[input_len] = '\0';
                if (send_line_with_newline(sock, input_buf) < 0) {
                    perror("send");
                    break;
                }
                if (strcmp(input_buf, "/quit") == 0) {
                    printf("\r\33[2K[INFO] Disconnecting...\n");
                    goto cleanup;
                }
                input_len = 0;
                input_buf[0] = '\0';
                printf(">> ");
                fflush(stdout);
            } else if (c == 127 || c == '\b') {
                if (input_len > 0) {
                    input_len--;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
            } else if (isprint((unsigned char)c)) {
                if (input_len < sizeof(input_buf) - 1) {
                    input_buf[input_len++] = c;
                    input_buf[input_len] = '\0';
                }
                reprint_prompt(input_buf);
            }
        }
        if (FD_ISSET(sock, &read_fds)) {
            ssize_t n = recv(sock, recv_buffer, sizeof(recv_buffer), 0);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                perror("recv");
                break;
            }
            if (n == 0) {
                printf("\r\33[2K[INFO] Server closed the connection.\n");
                goto cleanup;
            }
            size_t offset = 0U;
            while (offset < (size_t)n) {
                size_t to_copy = (size_t)n - offset;
                if (to_copy > sizeof(pending) - 1 - pending_len) {
                    to_copy = sizeof(pending) - 1 - pending_len;
                }
                memcpy(pending + pending_len, recv_buffer + offset, to_copy);
                pending_len += to_copy;
                offset += to_copy;
                pending[pending_len] = '\0';
                char *newline = strchr(pending, '\n');
                while (newline != NULL) {
                    *newline = '\0';
                    printf("\r\33[2K%s\n", pending);
                    pending_len -= (size_t)(newline - pending + 1);
                    memmove(pending, newline + 1, pending_len);
                    pending[pending_len] = '\0';
                    newline = strchr(pending, '\n');
                }
                reprint_prompt(input_buf);
            }
        }
    }

    printf("\r\33[2K[INFO] Connection lost.\n");

cleanup:
    disable_raw_mode();
    close(sock);
    exit(EXIT_SUCCESS);
}

/* -------------------- Entry point -------------------- */

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s server <bind-address> <port>\n"
            "  %s client <username> <server-host> <port>\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "server") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        run_server(argv[2], argv[3]);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "client") == 0) {
        if (argc != 5) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        const char *username = argv[2];
        if (!is_valid_name(username, MAX_USERNAME_LEN)) {
            fprintf(stderr, "Invalid username. Use up to 63 visible characters without spaces.\n");
            return EXIT_FAILURE;
        }
        run_client(username, argv[3], argv[4]);
        return EXIT_SUCCESS;
    }

    print_usage(argv[0]);
    return EXIT_FAILURE;
}
