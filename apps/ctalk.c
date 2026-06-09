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
 *   /who                  Refresh the member list in the current channel.
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
#include <sys/ioctl.h>
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
#define ROSTER_WIDTH 24
#define MIN_ROSTER_COLUMNS 72
#define MIN_PRIVILEGED_PORT 1024
#define MAX_TCP_PORT 65535

static const char *help_text =
    "Available commands:\n"
    "  /help                 Show this help.\n"
    "  /join <channel>       Join or create a channel.\n"
    "  /who                  Refresh the member list in the current channel.\n"
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

static void enable_socket_keepalive(int fd)
{
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_KEEPALIVE");
    }
}

static bool parse_numeric_port(const char *port, long *value)
{
    char *endptr;
    long parsed;

    if (port == NULL || port[0] == '\0' || isspace((unsigned char)port[0])) {
        return false;
    }

    errno = 0;
    parsed = strtol(port, &endptr, 10);
    if (errno != 0 || endptr == port || *endptr != '\0') {
        return false;
    }

    *value = parsed;
    return true;
}

static bool validate_numeric_port(const char *port, bool server_mode)
{
    long numeric_port;

    if (!parse_numeric_port(port, &numeric_port)) {
        return true;
    }

    if (numeric_port <= 0 || numeric_port > MAX_TCP_PORT) {
        fprintf(stderr, "Invalid port %ld. Use a TCP port from 1 to %d.\n",
                numeric_port, MAX_TCP_PORT);
        return false;
    }

    if (server_mode && numeric_port < MIN_PRIVILEGED_PORT && geteuid() != 0) {
        fprintf(stderr,
                "Port %ld is privileged on Unix-like systems and usually requires root.\n"
                "Use an unprivileged ctalk port such as 5000 or 8080, then connect clients to that same port.\n",
                numeric_port);
        return false;
    }

    return true;
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

static void disconnect_client(client_t *client, const char *reason);
static void broadcast_member_list(const char *channel);

static void disconnect_client(client_t *client, const char *reason)
{
    if (client->fd == -1) {
        return;
    }
    if (client->registered) {
        char timestamp[64];
        char msg[BUF_SIZE];
        char channel[MAX_CHANNEL_LEN];
        strncpy(channel, client->channel, sizeof(channel) - 1);
        channel[sizeof(channel) - 1] = '\0';
        format_timestamp(timestamp, sizeof(timestamp));
        snprintf(msg, sizeof(msg), "[%s] %s left channel %s (%s)\n",
                 timestamp, client->username, channel,
                 reason != NULL ? reason : "disconnected");
        broadcast_channel(channel, msg, client);
        client->registered = false;
        broadcast_member_list(channel);
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

static int build_member_list(const char *channel, char *buf, size_t size)
{
    int written = snprintf(buf, size, "Members in %s: ", channel);
    if (written < 0 || (size_t)written >= size) {
        return -1;
    }

    size_t offset = (size_t)written;
    bool first = true;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1 || !clients[i].registered) {
            continue;
        }
        if (strcmp(clients[i].channel, channel) != 0) {
            continue;
        }
        written = snprintf(buf + offset, size - offset, "%s%s",
                           first ? "" : ", ", clients[i].username);
        if (written < 0) {
            return -1;
        }
        if ((size_t)written >= size - offset) {
            offset = size - 1;
            break;
        }
        offset += (size_t)written;
        first = false;
    }

    written = snprintf(buf + offset, size - offset, "\n");
    if (written < 0 || (size_t)written >= size - offset) {
        buf[size - 2] = '\n';
        buf[size - 1] = '\0';
    }
    return 0;
}

static void send_member_list_to_client(client_t *client)
{
    char buf[BUF_SIZE];
    if (build_member_list(client->channel, buf, sizeof(buf)) < 0) {
        const char *err = "Unable to build member list.\n";
        if (send_line(client->fd, err) < 0) {
            disconnect_client(client, "write failure");
        }
        return;
    }
    if (send_line(client->fd, buf) < 0) {
        disconnect_client(client, "write failure");
    }
}

static void broadcast_member_list(const char *channel)
{
    char buf[BUF_SIZE];
    if (build_member_list(channel, buf, sizeof(buf)) < 0) {
        return;
    }
    broadcast_channel(channel, buf, NULL);
}

static void send_who(client_t *client)
{
    send_member_list_to_client(client);
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
    char old_channel[MAX_CHANNEL_LEN];
    strncpy(old_channel, client->channel, sizeof(old_channel) - 1);
    old_channel[sizeof(old_channel) - 1] = '\0';
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(buf, sizeof(buf), "[%s] %s left channel %s\n", timestamp,
             client->username, old_channel);
    broadcast_channel(old_channel, buf, client);
    strncpy(client->channel, channel, sizeof(client->channel) - 1);
    client->channel[sizeof(client->channel) - 1] = '\0';
    broadcast_member_list(old_channel);
    snprintf(buf, sizeof(buf), "[%s] %s joined channel %s\n", timestamp,
             client->username, channel);
    broadcast_channel(client->channel, buf, NULL);
    broadcast_member_list(client->channel);
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
        broadcast_member_list(client->channel);
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
    if (!validate_numeric_port(port, true)) {
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
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
        enable_socket_keepalive(listen_fd);
        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt");
            close(listen_fd);
            listen_fd = -1;
            continue;
        }
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) < 0) {
            if (errno == EACCES) {
                fprintf(stderr,
                        "bind %s:%s: permission denied. Use an unprivileged port such as 5000 or 8080 unless running as root.\n",
                        bind_addr, port);
            } else {
                perror("bind");
            }
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
            struct sockaddr_storage cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (fd < 0) {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("accept");
                }
            } else {
                enable_socket_keepalive(fd);
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
static char member_roster[BUF_SIZE] = "Members: waiting";

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

static int terminal_width(void)
{
    struct winsize ws;
    const char *columns;
    char *endptr;
    long parsed;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    columns = getenv("COLUMNS");
    if (columns != NULL) {
        errno = 0;
        parsed = strtol(columns, &endptr, 10);
        if (errno == 0 && endptr != columns && parsed > 0 && parsed <= 1000) {
            return (int)parsed;
        }
    }

    return 80;
}

static bool supports_roster_panel(void)
{
    return terminal_width() >= MIN_ROSTER_COLUMNS;
}

static void reprint_prompt(const char *buf)
{
    printf("\r\33[2K>> %s", buf);
    fflush(stdout);
}

static bool is_member_roster_line(const char *line)
{
    return strncmp(line, "Members in ", 11) == 0;
}

static void print_roster_line(const char *text)
{
    int width = terminal_width();
    int left_pad = width - ROSTER_WIDTH;
    char item[ROSTER_WIDTH + 1];

    if (left_pad < 0) {
        left_pad = 0;
    }
    snprintf(item, sizeof(item), "%-*.*s", ROSTER_WIDTH, ROSTER_WIDTH, text);
    printf("\r\33[2K%*s%s\n", left_pad, "", item);
}

static void update_member_roster(const char *line)
{
    char roster_copy[BUF_SIZE];
    char *members;
    char *saveptr = NULL;
    char *member;
    char header[ROSTER_WIDTH + 1];

    strncpy(member_roster, line, sizeof(member_roster) - 1);
    member_roster[sizeof(member_roster) - 1] = '\0';

    if (!supports_roster_panel()) {
        printf("\r\33[2K%s\n", member_roster);
        return;
    }

    strncpy(roster_copy, member_roster, sizeof(roster_copy) - 1);
    roster_copy[sizeof(roster_copy) - 1] = '\0';
    members = strchr(roster_copy, ':');
    if (members == NULL) {
        print_roster_line(member_roster);
        return;
    }

    *members = '\0';
    members++;
    while (*members != '\0' && isspace((unsigned char)*members)) {
        members++;
    }

    snprintf(header, sizeof(header), "[%.*s]", ROSTER_WIDTH - 2, roster_copy);
    print_roster_line(header);
    member = strtok_r(members, ",", &saveptr);
    while (member != NULL) {
        while (*member != '\0' && isspace((unsigned char)*member)) {
            member++;
        }
        trim_trailing_whitespace(member);
        if (member[0] != '\0') {
            print_roster_line(member);
        }
        member = strtok_r(NULL, ",", &saveptr);
    }
}

static int connect_to_server(const char *host, const char *port)
{
    if (!validate_numeric_port(port, false)) {
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
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
            enable_socket_keepalive(sock);
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
                    if (is_member_roster_line(pending)) {
                        update_member_roster(pending);
                    } else {
                        printf("\r\33[2K%s\n", pending);
                    }
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
            "  %s client <username> <server-host> <port>\n"
            "\n"
            "Use an unprivileged server port such as 5000 or 8080 unless running as root.\n",
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
