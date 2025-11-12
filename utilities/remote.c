#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define SERVER_BACKLOG 8
#define COMMAND_MAX 4096
#define IO_BUFFER 4096
#define MAX_LOG_SIZE 131072

static void usage(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  remote server <bind-address> <port>\n"
            "  remote client <server-address> <port>\n");
}

static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t written = send(fd, data, len, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        data += (size_t)written;
        len -= (size_t)written;
    }
    return 0;
}

/* --------------------------- Server helpers --------------------------- */

static void trim_command(char *command)
{
    size_t len = strlen(command);
    while (len > 0 &&
           (command[len - 1] == '\r' || command[len - 1] == '\n' ||
            isspace((unsigned char)command[len - 1]))) {
        command[len - 1] = '\0';
        len--;
    }

    size_t start = 0;
    while (command[start] != '\0' && isspace((unsigned char)command[start])) {
        start++;
    }

    if (start > 0) {
        size_t dst = 0;
        while (command[start] != '\0') {
            command[dst++] = command[start++];
        }
        command[dst] = '\0';
    }
}

static int stream_command_output(FILE *pipe_fp, int client_fd)
{
    char buffer[IO_BUFFER];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe_fp)) > 0) {
        if (send_all(client_fd, buffer, bytes_read) != 0) {
            return -1;
        }
    }

    if (ferror(pipe_fp) != 0) {
        return -1;
    }

    return 0;
}

static int report_command_status(int client_fd, int status)
{
    char message[128];
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        int written = snprintf(message, sizeof(message),
                               "\n[command exited with status %d]\n", exit_code);
        if (written < 0 || (size_t)written >= sizeof(message)) {
            strcpy(message, "\n[command exited]\n");
        }
    } else if (WIFSIGNALED(status)) {
        int signal_code = WTERMSIG(status);
        int written = snprintf(message, sizeof(message),
                               "\n[command terminated by signal %d]\n", signal_code);
        if (written < 0 || (size_t)written >= sizeof(message)) {
            strcpy(message, "\n[command terminated]\n");
        }
    } else {
        strcpy(message, "\n[command finished]\n");
    }

    return send_all(client_fd, message, strlen(message));
}

static int process_command(const char *command, int client_fd)
{
    if (command[0] == '\0') {
        return 0;
    }

    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        const char *farewell = "[session terminated]\n";
        (void)send_all(client_fd, farewell, strlen(farewell));
        return 1;
    }

    char prompt_line[COMMAND_MAX + 16];
    int prompt_len = snprintf(prompt_line, sizeof(prompt_line), "$ %s\n", command);
    if (prompt_len < 0) {
        prompt_line[0] = '\0';
        prompt_len = 0;
    } else if ((size_t)prompt_len >= sizeof(prompt_line)) {
        prompt_len = (int)(sizeof(prompt_line) - 1);
        prompt_line[prompt_len] = '\0';
    }

    if (prompt_len > 0 && send_all(client_fd, prompt_line, (size_t)prompt_len) != 0) {
        return -1;
    }

    FILE *pipe_fp = popen(command, "r");
    if (pipe_fp == NULL) {
        char error_line[256];
        int written = snprintf(error_line, sizeof(error_line),
                               "remote server: failed to run '%s': %s\n", command,
                               strerror(errno));
        if (written < 0 || (size_t)written >= sizeof(error_line)) {
            strcpy(error_line, "remote server: failed to run command\n");
        }
        (void)send_all(client_fd, error_line, strlen(error_line));
        return 0;
    }

    if (stream_command_output(pipe_fp, client_fd) != 0) {
        pclose(pipe_fp);
        return -1;
    }

    int status = pclose(pipe_fp);
    if (status == -1) {
        char status_line[256];
        int written = snprintf(status_line, sizeof(status_line),
                               "\nremote server: failed to retrieve command status: %s\n",
                               strerror(errno));
        if (written < 0 || (size_t)written >= sizeof(status_line)) {
            strcpy(status_line,
                   "\nremote server: failed to retrieve command status\n");
        }
        (void)send_all(client_fd, status_line, strlen(status_line));
        return -1;
    }

    if (report_command_status(client_fd, status) != 0) {
        return -1;
    }

    return 0;
}

static int handle_client(int client_fd)
{
    const char *welcome = "Connected to BUDOSTACK remote server.\nType 'exit' to close the session.\n";
    if (send_all(client_fd, welcome, strlen(welcome)) != 0) {
        return -1;
    }

    char buffer[COMMAND_MAX * 2];
    size_t buffer_len = 0;

    while (1) {
        if (buffer_len >= sizeof(buffer)) {
            const char *message = "remote server: command too long, clearing buffer\n";
            buffer_len = 0;
            if (send_all(client_fd, message, strlen(message)) != 0) {
                return -1;
            }
        }

        ssize_t received = recv(client_fd, buffer + buffer_len,
                                sizeof(buffer) - buffer_len, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            return 0;
        }

        buffer_len += (size_t)received;
        size_t processed = 0;

        for (size_t i = 0; i < buffer_len; i++) {
            if (buffer[i] == '\n') {
                size_t command_length = i - processed;
                if (command_length >= COMMAND_MAX) {
                    char message[128];
                    int written = snprintf(message, sizeof(message),
                                            "remote server: command exceeded %d characters and was ignored\n",
                                            COMMAND_MAX - 1);
                    if (written < 0 || (size_t)written >= sizeof(message)) {
                        strcpy(message, "remote server: command too long and was ignored\n");
                    }
                    if (send_all(client_fd, message, strlen(message)) != 0) {
                        return -1;
                    }
                } else {
                    char command[COMMAND_MAX];
                    memcpy(command, buffer + processed, command_length);
                    command[command_length] = '\0';
                    trim_command(command);
                    int result = process_command(command, client_fd);
                    if (result != 0) {
                        return result > 0 ? 0 : -1;
                    }
                }
                processed = i + 1;
            }
        }

        if (processed > 0) {
            memmove(buffer, buffer + processed, buffer_len - processed);
            buffer_len -= processed;
        }
    }
}

static int run_server(const char *bind_address, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(bind_address, port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "remote server: getaddrinfo: %s\n", gai_strerror(rc));
        return EXIT_FAILURE;
    }

    int listen_fd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }

        int optval = 1;
        (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(listen_fd, SERVER_BACKLOG) == 0) {
                break;
            }
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(result);

    if (listen_fd < 0) {
        fprintf(stderr, "remote server: failed to set up listening socket\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "remote server listening on %s:%s\n", bind_address, port);
    fflush(stdout);

    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("remote server: accept");
            break;
        }

        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        if (getnameinfo((struct sockaddr *)&client_addr, addr_len, host, sizeof(host),
                        service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            fprintf(stdout, "remote server: connection from %s:%s\n", host, service);
            fflush(stdout);
        }

        int status = handle_client(client_fd);
        close(client_fd);
        if (status < 0) {
            fprintf(stderr, "remote server: client handling failed\n");
        }
    }

    close(listen_fd);
    return EXIT_FAILURE;
}

/* --------------------------- Client helpers --------------------------- */

struct output_buffer {
    char *data;
    size_t length;
    size_t capacity;
};

static void output_buffer_init(struct output_buffer *buffer)
{
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void output_buffer_free(struct output_buffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static int output_buffer_reserve(struct output_buffer *buffer, size_t new_capacity)
{
    if (new_capacity <= buffer->capacity) {
        return 0;
    }

    char *new_data = realloc(buffer->data, new_capacity);
    if (new_data == NULL) {
        return -1;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return 0;
}

static int output_buffer_append(struct output_buffer *buffer, const char *data, size_t len)
{
    if (len == 0) {
        return 0;
    }

    size_t required = buffer->length + len;
    if (required > buffer->capacity) {
        size_t new_capacity = buffer->capacity > 0 ? buffer->capacity : 4096;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }
        if (output_buffer_reserve(buffer, new_capacity) != 0) {
            return -1;
        }
    }

    memcpy(buffer->data + buffer->length, data, len);
    buffer->length += len;

    if (buffer->length > MAX_LOG_SIZE) {
        size_t excess = buffer->length - MAX_LOG_SIZE;
        memmove(buffer->data, buffer->data + excess, buffer->length - excess);
        buffer->length -= excess;
    }

    return 0;
}

static const char *output_buffer_tail(const struct output_buffer *buffer, size_t lines)
{
    if (buffer->length == 0 || buffer->data == NULL) {
        return NULL;
    }

    if (lines == 0) {
        return buffer->data + buffer->length;
    }

    const char *start = buffer->data;
    const char *cursor = buffer->data + buffer->length - 1;
    size_t newline_count = 0;

    while (cursor >= start) {
        if (*cursor == '\n') {
            newline_count++;
            if (newline_count >= lines) {
                return cursor + 1;
            }
        }
        if (cursor == start) {
            break;
        }
        cursor--;
    }

    return start;
}

static void clear_command_line(void)
{
    printf("\033[K");
}

static void render_view(const struct output_buffer *buffer, const char *command,
                        size_t command_length)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0 || ws.ws_col == 0) {
        ws.ws_row = 24;
        ws.ws_col = 80;
    }

    size_t rows = ws.ws_row;
    size_t cols = ws.ws_col;

    if (rows == 0) {
        rows = 1;
    }

    size_t content_rows = rows > 0 ? rows - 1 : 0;

    printf("\033[H");

    const char *data_start = buffer->data != NULL ? buffer->data : "";
    size_t data_length = buffer->length;
    const char *start = output_buffer_tail(buffer, content_rows);
    if (start == NULL) {
        start = data_start;
    }
    const char *end = data_start + data_length;
    const char *cursor = start;

    for (size_t line = 0; line < content_rows; line++) {
        if (cursor < end) {
            const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
            size_t to_write = newline != NULL ? (size_t)(newline - cursor) : (size_t)(end - cursor);
            if (to_write > 0) {
                fwrite(cursor, 1, to_write, stdout);
            }
            if (newline != NULL) {
                cursor = newline + 1;
            } else {
                cursor = end;
            }
        }
        clear_command_line();
        putchar('\n');
    }

    clear_command_line();
    fputs("Command: ", stdout);

    size_t prompt_len = strlen("Command: ");
    size_t max_visible = cols > prompt_len ? cols - prompt_len : 0;
    const char *visible_command = command;
    size_t visible_length = command_length;

    if (visible_length > max_visible && max_visible > 0) {
        visible_command = command + (visible_length - max_visible);
        visible_length = max_visible;
    }

    if (visible_length > 0) {
        fwrite(visible_command, 1, visible_length, stdout);
    }

    clear_command_line();
    fflush(stdout);
}

static struct termios original_termios;
static bool termios_saved = false;
static bool screen_active = false;

static void restore_terminal(void)
{
    if (termios_saved) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        termios_saved = false;
    }
    if (screen_active) {
        printf("\033[?25h\033[?1049l");
        fflush(stdout);
        screen_active = false;
    }
}

static int enable_terminal(void)
{
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("remote client: tcgetattr");
        return -1;
    }

    struct termios raw = original_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("remote client: tcsetattr");
        return -1;
    }

    termios_saved = true;
    atexit(restore_terminal);

    printf("\033[?1049h\033[2J\033[H\033[?25l");
    fflush(stdout);
    screen_active = true;
    return 0;
}

static int connect_to_server(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "remote client: getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd < 0) {
        fprintf(stderr, "remote client: failed to connect to %s:%s\n", host, port);
    }

    return sockfd;
}

static int run_client(const char *host, const char *port)
{
    int sockfd = connect_to_server(host, port);
    if (sockfd < 0) {
        return EXIT_FAILURE;
    }

    if (enable_terminal() != 0) {
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct output_buffer buffer;
    output_buffer_init(&buffer);

    char command[COMMAND_MAX];
    command[0] = '\0';
    size_t command_length = 0;
    bool running = true;
    bool requested_exit = false;
    int exit_status = EXIT_SUCCESS;

    render_view(&buffer, command, command_length);

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);
        int max_fd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("remote client: select");
            exit_status = EXIT_FAILURE;
            break;
        }

        if (FD_ISSET(sockfd, &readfds)) {
            char recv_buffer[IO_BUFFER];
            ssize_t received = recv(sockfd, recv_buffer, sizeof(recv_buffer), 0);
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                perror("remote client: recv");
                exit_status = EXIT_FAILURE;
                break;
            }
            if (received == 0) {
                const char *message = "Connection closed by remote host.\n";
                (void)output_buffer_append(&buffer, message, strlen(message));
                render_view(&buffer, command, command_length);
                running = false;
                break;
            }

            if (output_buffer_append(&buffer, recv_buffer, (size_t)received) != 0) {
                const char *message = "remote client: failed to allocate memory\n";
                (void)output_buffer_append(&buffer, message, strlen(message));
            }
            render_view(&buffer, command, command_length);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            unsigned char ch;
            ssize_t r = read(STDIN_FILENO, &ch, 1);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("remote client: read");
                exit_status = EXIT_FAILURE;
                break;
            }
            if (r == 0) {
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                if (command_length > 0) {
                    if (send_all(sockfd, command, command_length) != 0 ||
                        send_all(sockfd, "\n", 1) != 0) {
                        perror("remote client: send");
                        running = false;
                        exit_status = EXIT_FAILURE;
                        break;
                    }
                    command_length = 0;
                    command[0] = '\0';
                    render_view(&buffer, command, command_length);
                }
            } else if (ch == 127 || ch == 8) {
                if (command_length > 0) {
                    command_length--;
                    command[command_length] = '\0';
                    render_view(&buffer, command, command_length);
                }
            } else if (ch == 3) {
                requested_exit = true;
                running = false;
                break;
            } else if (ch == 4) {
                if (command_length == 0) {
                    requested_exit = true;
                    running = false;
                    break;
                }
            } else if (isprint(ch) || ch == '\t') {
                if (command_length + 1 < sizeof(command)) {
                    command[command_length++] = (char)ch;
                    command[command_length] = '\0';
                    render_view(&buffer, command, command_length);
                }
            }
        }
    }

    if (requested_exit) {
        const char *exit_command = "exit\n";
        (void)send_all(sockfd, exit_command, strlen(exit_command));
    }

    restore_terminal();
    output_buffer_free(&buffer);
    close(sockfd);
    return exit_status;
}

/* ------------------------------ Entrypoint ------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    if (strcmp(argv[1], "server") == 0) {
        if (argc != 4) {
            usage();
            return EXIT_FAILURE;
        }
        return run_server(argv[2], argv[3]);
    }

    if (strcmp(argv[1], "client") == 0) {
        if (argc != 4) {
            usage();
            return EXIT_FAILURE;
        }
        return run_client(argv[2], argv[3]);
    }

    usage();
    return EXIT_FAILURE;
}
