#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_PORT 23456
#define MAX_BUFFER 4096

struct client {
    int fd;
    struct client *next;
};

static volatile sig_atomic_t child_exited = 0;

static void usage(const char *progname) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s server [port]\n", progname);
    fprintf(stderr, "  %s client <host> [port]\n", progname);
}

static void set_child_exited(int signo) {
    (void)signo;
    child_exited = 1;
}

static int add_client(struct client **head, int fd) {
    struct client *node = malloc(sizeof(*node));
    if (node == NULL) {
        return -1;
    }
    node->fd = fd;
    node->next = *head;
    *head = node;
    return 0;
}

static void remove_client(struct client **head, int fd) {
    struct client *prev = NULL;
    struct client *cur = *head;
    while (cur != NULL) {
        if (cur->fd == fd) {
            if (prev == NULL) {
                *head = cur->next;
            } else {
                prev->next = cur->next;
            }
            close(cur->fd);
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static void close_all_clients(struct client **head) {
    struct client *cur = *head;
    while (cur != NULL) {
        struct client *next = cur->next;
        close(cur->fd);
        free(cur);
        cur = next;
    }
    *head = NULL;
}

static int write_all(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = buf;
    size_t total = 0;
    while (total < count) {
        ssize_t written = write(fd, ptr + total, count - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)written;
    }
    return 0;
}

static void broadcast_output(struct client **head, const char *buffer, ssize_t len) {
    struct client *cur = *head;
    while (cur != NULL) {
        struct client *next = cur->next;
        if (write_all(cur->fd, buffer, (size_t)len) != 0) {
            int fd = cur->fd;
            remove_client(head, fd);
        }
        cur = next;
    }
}

static int run_server(uint16_t port) {
    int master_fd = -1;
    pid_t child_pid;

    child_exited = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = set_child_exited;
    sigaction(SIGCHLD, &sa, NULL);

    child_pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (child_pid < 0) {
        perror("forkpty");
        return 1;
    }

    if (child_pid == 0) {
        const char *shell = getenv("SHELL");
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/sh";
        }
        execlp(shell, shell, "-i", NULL);
        perror("execlp");
        _exit(127);
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        close(master_fd);
        return 1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(master_fd);
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(master_fd);
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 8) < 0) {
        perror("listen");
        close(master_fd);
        close(listen_fd);
        return 1;
    }

    fprintf(stdout, "remote: server listening on port %u\n", (unsigned)port);
    fflush(stdout);

    struct client *clients = NULL;
    int exit_code = 0;

    for (;;) {
        if (child_exited) {
            int status = 0;
            waitpid(child_pid, &status, WNOHANG);
            fprintf(stdout, "remote: shell exited\n");
            exit_code = 0;
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = master_fd;
        FD_SET(master_fd, &readfds);
        FD_SET(listen_fd, &readfds);
        if (listen_fd > max_fd) {
            max_fd = listen_fd;
        }

        struct client *cur = clients;
        while (cur != NULL) {
            FD_SET(cur->fd, &readfds);
            if (cur->fd > max_fd) {
                max_fd = cur->fd;
            }
            cur = cur->next;
        }

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            exit_code = 1;
            break;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd >= 0) {
                if (add_client(&clients, client_fd) != 0) {
                    fprintf(stderr, "remote: unable to add client\n");
                    close(client_fd);
                } else {
                    char hostbuf[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &client_addr.sin_addr, hostbuf, sizeof(hostbuf)) == NULL) {
                        snprintf(hostbuf, sizeof(hostbuf), "unknown");
                    }
                    fprintf(stdout, "remote: client connected from %s\n", hostbuf);
                    fflush(stdout);
                }
            }
        }

        if (FD_ISSET(master_fd, &readfds)) {
            char buffer[MAX_BUFFER];
            ssize_t nread = read(master_fd, buffer, sizeof(buffer));
            if (nread <= 0) {
                if (nread < 0 && errno == EINTR) {
                    continue;
                }
                exit_code = 0;
                break;
            }
            broadcast_output(&clients, buffer, nread);
        }

        cur = clients;
        while (cur != NULL) {
            int client_fd = cur->fd;
            struct client *next = cur->next;
            if (FD_ISSET(client_fd, &readfds)) {
                char buffer[MAX_BUFFER];
                ssize_t nread = read(client_fd, buffer, sizeof(buffer));
                if (nread <= 0) {
                    fprintf(stdout, "remote: client disconnected\n");
                    fflush(stdout);
                    remove_client(&clients, client_fd);
                } else {
                    if (write_all(master_fd, buffer, (size_t)nread) != 0) {
                        perror("write to pty");
                        exit_code = 1;
                        remove_client(&clients, client_fd);
                    }
                }
            }
            cur = next;
        }
    }

    close_all_clients(&clients);
    close(listen_fd);
    close(master_fd);
    return exit_code;
}

static void restore_terminal(int fd, struct termios *orig) {
    if (orig != NULL) {
        tcsetattr(fd, TCSANOW, orig);
    }
}

static int set_raw_mode(int fd, struct termios *orig) {
    if (tcgetattr(fd, orig) != 0) {
        perror("tcgetattr");
        return -1;
    }
    struct termios raw = *orig;
    cfmakeraw(&raw);
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

static int run_client(const char *host, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (he == NULL || he->h_addrtype != AF_INET) {
            fprintf(stderr, "remote: unable to resolve host %s\n", host);
            close(sock);
            return 1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    fprintf(stdout, "remote: connected to %s:%u\n", host, (unsigned)port);
    fflush(stdout);

    struct termios orig;
    if (set_raw_mode(STDIN_FILENO, &orig) != 0) {
        close(sock);
        return 1;
    }

    int exit_code = 0;
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            exit_code = 1;
            break;
        }

        if (FD_ISSET(sock, &readfds)) {
            char buffer[MAX_BUFFER];
            ssize_t nread = read(sock, buffer, sizeof(buffer));
            if (nread <= 0) {
                exit_code = 0;
                break;
            }
            if (write_all(STDOUT_FILENO, buffer, (size_t)nread) != 0) {
                perror("write");
                exit_code = 1;
                break;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char buffer[MAX_BUFFER];
            ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (nread <= 0) {
                exit_code = 0;
                break;
            }
            if (write_all(sock, buffer, (size_t)nread) != 0) {
                perror("write");
                exit_code = 1;
                break;
            }
        }
    }

    restore_terminal(STDIN_FILENO, &orig);
    close(sock);
    fprintf(stdout, "remote: disconnected\n");
    fflush(stdout);
    return exit_code;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "server") == 0) {
        uint16_t port = DEFAULT_PORT;
        if (argc >= 3) {
            char *endptr = NULL;
            long val = strtol(argv[2], &endptr, 10);
            if (endptr == NULL || *endptr != '\0' || val <= 0 || val > 65535) {
                fprintf(stderr, "remote: invalid port\n");
                return 1;
            }
            port = (uint16_t)val;
        }
        return run_server(port);
    }

    if (strcmp(argv[1], "client") == 0) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        uint16_t port = DEFAULT_PORT;
        if (argc >= 4) {
            char *endptr = NULL;
            long val = strtol(argv[3], &endptr, 10);
            if (endptr == NULL || *endptr != '\0' || val <= 0 || val > 65535) {
                fprintf(stderr, "remote: invalid port\n");
                return 1;
            }
            port = (uint16_t)val;
        }
        return run_client(argv[2], port);
    }

    usage(argv[0]);
    return 1;
}
