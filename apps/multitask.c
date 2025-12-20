#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MULTITASK_MAX_SESSIONS 9
#define MULTITASK_STATUS_LABEL "multitask"
#define MULTITASK_STATUS_PADDING " "

struct multitask_session {
    int in_use;
    int index;
    pid_t pid;
    int master_fd;
};

static struct termios multitask_original_termios;
static volatile sig_atomic_t multitask_resize_requested = 0;
static volatile sig_atomic_t multitask_child_exited = 0;

static void multitask_handle_sigwinch(int sig) {
    (void)sig;
    multitask_resize_requested = 1;
}

static void multitask_handle_sigchld(int sig) {
    (void)sig;
    multitask_child_exited = 1;
}

static int multitask_set_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &multitask_original_termios) < 0) {
        perror("tcgetattr");
        return -1;
    }

    struct termios raw = multitask_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        return -1;
    }

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl");
        return -1;
    }

    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

static void multitask_restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &multitask_original_termios);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    }
    const char *restore = "\x1b[?25h\x1b[?1049l";
    (void)write(STDOUT_FILENO, restore, strlen(restore));
}

static ssize_t multitask_safe_write(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = (const unsigned char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
    return (ssize_t)count;
}

static int multitask_build_path(char *out_path, size_t out_size, const char *base, const char *leaf) {
    if (!out_path || !base || !leaf) {
        return -1;
    }
    size_t base_len = strnlen(base, out_size);
    size_t leaf_len = strnlen(leaf, out_size);
    if (base_len == 0 || leaf_len == 0 || base_len + leaf_len + 2 > out_size) {
        return -1;
    }
    memcpy(out_path, base, base_len);
    if (base[base_len - 1] != '/') {
        out_path[base_len++] = '/';
    }
    memcpy(out_path + base_len, leaf, leaf_len);
    out_path[base_len + leaf_len] = '\0';
    return 0;
}

static int multitask_compute_root(const char *argv0, char *out_path, size_t out_size) {
    if (!argv0 || !out_path || out_size == 0) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) {
        return -1;
    }

    char *slash = strrchr(resolved, '/');
    if (!slash) {
        return -1;
    }
    *slash = '\0';

    slash = strrchr(resolved, '/');
    if (!slash) {
        return -1;
    }

    *slash = '\0';
    size_t len = strnlen(resolved, sizeof(resolved));
    if (len + 1 > out_size) {
        return -1;
    }
    memcpy(out_path, resolved, len + 1);
    return 0;
}

static pid_t multitask_spawn_budostack(const char *exe_path, int *out_master_fd, unsigned short rows, unsigned short cols) {
    if (!exe_path || !out_master_fd) {
        return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    struct winsize ws = {0};
    ws.ws_row = rows;
    ws.ws_col = cols;
    ioctl(master_fd, TIOCSWINSZ, &ws);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        if (setsid() == -1) {
            perror("setsid");
            _exit(EXIT_FAILURE);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            perror("open slave pty");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }

        if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 || dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        close(master_fd);

        const char *term_value = getenv("TERM");
        if (!term_value || term_value[0] == '\0') {
            setenv("TERM", "xterm-256color", 1);
        }

        execl(exe_path, exe_path, (char *)NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    *out_master_fd = master_fd;
    return pid;
}

static void multitask_render_status(const struct multitask_session *sessions, int active_index, unsigned short rows, unsigned short cols) {
    if (rows == 0 || cols == 0) {
        return;
    }

    char line[1024];
    size_t offset = 0u;

    offset += (size_t)snprintf(line + offset, sizeof(line) - offset, "%s%s", MULTITASK_STATUS_LABEL, MULTITASK_STATUS_PADDING);

    for (int i = 0; i < MULTITASK_MAX_SESSIONS && offset < sizeof(line) - 1; i++) {
        if (!sessions[i].in_use) {
            continue;
        }
        int is_active = (sessions[i].index == active_index);
        offset += (size_t)snprintf(line + offset, sizeof(line) - offset, "[%d%s]%s", sessions[i].index, is_active ? "*" : "", MULTITASK_STATUS_PADDING);
    }

    if (offset >= sizeof(line)) {
        offset = sizeof(line) - 1;
    }
    line[offset] = '\0';

    char sequence[64];
    snprintf(sequence, sizeof(sequence), "\x1b[s\x1b[%hu;1H\x1b[2K", rows);
    multitask_safe_write(STDOUT_FILENO, sequence, strlen(sequence));
    size_t line_len = strnlen(line, sizeof(line));
    if (line_len > (size_t)cols) {
        line_len = (size_t)cols;
    }
    multitask_safe_write(STDOUT_FILENO, line, line_len);
    multitask_safe_write(STDOUT_FILENO, "\x1b[u", 3);
}

static int multitask_allocate_index(const struct multitask_session *sessions) {
    for (int i = 1; i <= MULTITASK_MAX_SESSIONS; i++) {
        int used = 0;
        for (int j = 0; j < MULTITASK_MAX_SESSIONS; j++) {
            if (sessions[j].in_use && sessions[j].index == i) {
                used = 1;
                break;
            }
        }
        if (!used) {
            return i;
        }
    }
    return -1;
}

static void multitask_update_winsize(const struct multitask_session *sessions, unsigned short rows, unsigned short cols) {
    struct winsize ws = {0};
    ws.ws_row = rows;
    ws.ws_col = cols;
    for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
        if (!sessions[i].in_use) {
            continue;
        }
        ioctl(sessions[i].master_fd, TIOCSWINSZ, &ws);
    }
}

static void multitask_cleanup_sessions(struct multitask_session *sessions) {
    for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
        if (!sessions[i].in_use) {
            continue;
        }
        kill(sessions[i].pid, SIGTERM);
        waitpid(sessions[i].pid, NULL, 0);
        close(sessions[i].master_fd);
        sessions[i].in_use = 0;
    }
}

static int multitask_spawn_session(struct multitask_session *sessions, const char *budostack_path, unsigned short rows, unsigned short cols) {
    int slot = -1;
    for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
        if (!sessions[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "No free session slots available.\n");
        return -1;
    }

    int index = multitask_allocate_index(sessions);
    if (index < 0) {
        fprintf(stderr, "Could not allocate session index.\n");
        return -1;
    }

    if (rows == 0 || cols == 0) {
        fprintf(stderr, "Terminal size unavailable.\n");
        return -1;
    }

    int master_fd = -1;
    pid_t pid = multitask_spawn_budostack(budostack_path, &master_fd, rows, cols);
    if (pid < 0) {
        return -1;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        kill(pid, SIGKILL);
        close(master_fd);
        return -1;
    }

    sessions[slot].in_use = 1;
    sessions[slot].index = index;
    sessions[slot].pid = pid;
    sessions[slot].master_fd = master_fd;
    return index;
}

static int multitask_find_slot_by_index(const struct multitask_session *sessions, int index) {
    for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
        if (sessions[i].in_use && sessions[i].index == index) {
            return i;
        }
    }
    return -1;
}

static void multitask_send_to_active(const struct multitask_session *sessions, int active_index, const unsigned char *data, size_t len) {
    int slot = multitask_find_slot_by_index(sessions, active_index);
    if (slot >= 0 && data && len > 0) {
        multitask_safe_write(sessions[slot].master_fd, data, len);
    }
}

static int multitask_close_session(struct multitask_session *sessions, int active_index) {
    int slot = multitask_find_slot_by_index(sessions, active_index);
    if (slot < 0) {
        return -1;
    }

    kill(sessions[slot].pid, SIGTERM);
    waitpid(sessions[slot].pid, NULL, 0);
    close(sessions[slot].master_fd);
    sessions[slot].in_use = 0;
    return 0;
}

static size_t multitask_read_escape_sequence(unsigned char *buffer, size_t buf_size) {
    if (!buffer || buf_size == 0) {
        return 0u;
    }

    buffer[0] = 0x1b;
    size_t len = 1u;

    struct pollfd pfd = {0};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    while (len < buf_size) {
        int ready = poll(&pfd, 1u, 15);
        if (ready <= 0 || !(pfd.revents & POLLIN)) {
            break;
        }

        unsigned char byte = 0;
        ssize_t nread = read(STDIN_FILENO, &byte, 1u);
        if (nread == 1) {
            buffer[len++] = byte;
            continue;
        }
        if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }

    return len;
}

int main(int argc, char **argv) {
    (void)argc;
    struct multitask_session sessions[MULTITASK_MAX_SESSIONS] = {0};
    struct sigaction sa = {0};

    sa.sa_handler = multitask_handle_sigwinch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = multitask_handle_sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    if (multitask_set_raw_mode() != 0) {
        return EXIT_FAILURE;
    }

    const char *prologue = "\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l";
    if (multitask_safe_write(STDOUT_FILENO, prologue, strlen(prologue)) < 0) {
        multitask_restore_terminal();
        return EXIT_FAILURE;
    }

    char root_dir[PATH_MAX];
    char budostack_path[PATH_MAX];
    if (multitask_compute_root(argv[0], root_dir, sizeof(root_dir)) != 0 ||
        multitask_build_path(budostack_path, sizeof(budostack_path), root_dir, "budostack") != 0) {
        multitask_restore_terminal();
        fprintf(stderr, "Could not resolve budostack path.\n");
        return EXIT_FAILURE;
    }

    if (access(budostack_path, X_OK) != 0) {
        multitask_restore_terminal();
        fprintf(stderr, "Executable not found at %s.\n", budostack_path);
        return EXIT_FAILURE;
    }

    struct winsize ws = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_row == 0 || ws.ws_col == 0) {
        ws.ws_row = 24;
        ws.ws_col = 80;
    }

    unsigned short session_rows = (ws.ws_row > 1) ? (unsigned short)(ws.ws_row - 1u) : ws.ws_row;
    unsigned short session_cols = ws.ws_col;

    int active_index = multitask_spawn_session(sessions, budostack_path, session_rows, session_cols);
    if (active_index < 0) {
        multitask_restore_terminal();
        return EXIT_FAILURE;
    }

    int running = 1;
    while (running) {
        if (multitask_resize_requested) {
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
                session_rows = (ws.ws_row > 1) ? (unsigned short)(ws.ws_row - 1u) : ws.ws_row;
                session_cols = ws.ws_col;
                ws.ws_row = session_rows + 1u;
                ws.ws_col = session_cols;
                multitask_update_winsize(sessions, session_rows, session_cols);
            }
            multitask_resize_requested = 0;
        }

        if (multitask_child_exited) {
            for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
                if (!sessions[i].in_use) {
                    continue;
                }
                int status = 0;
                pid_t result = waitpid(sessions[i].pid, &status, WNOHANG);
                if (result > 0) {
                    close(sessions[i].master_fd);
                    sessions[i].in_use = 0;
                    if (sessions[i].index == active_index) {
                        active_index = -1;
                    }
                }
            }
            multitask_child_exited = 0;
        }

        if (active_index < 0) {
            for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
                if (sessions[i].in_use) {
                    active_index = sessions[i].index;
                    break;
                }
            }
            if (active_index < 0) {
                break;
            }
        }

        multitask_render_status(sessions, active_index, ws.ws_row, ws.ws_col);

        struct pollfd pollfds[MULTITASK_MAX_SESSIONS + 1] = {0};
        nfds_t nfds = 0;
        pollfds[nfds].fd = STDIN_FILENO;
        pollfds[nfds].events = POLLIN;
        nfds++;

        for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
            if (!sessions[i].in_use) {
                continue;
            }
            pollfds[nfds].fd = sessions[i].master_fd;
            pollfds[nfds].events = POLLIN;
            nfds++;
        }

        int poll_result = poll(pollfds, nfds, 50);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        nfds_t offset = 1;
        for (int i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
            if (!sessions[i].in_use) {
                continue;
            }
            if (pollfds[offset].revents & POLLIN) {
                unsigned char buffer[4096];
                ssize_t bytes = read(sessions[i].master_fd, buffer, sizeof(buffer));
                if (bytes > 0) {
                    multitask_safe_write(STDOUT_FILENO, buffer, (size_t)bytes);
                } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close(sessions[i].master_fd);
                    waitpid(sessions[i].pid, NULL, WNOHANG);
                    sessions[i].in_use = 0;
                    if (sessions[i].index == active_index) {
                        active_index = -1;
                    }
                }
            }
            offset++;
        }

        if (pollfds[0].revents & POLLIN) {
            unsigned char input_buffer[32];
            ssize_t read_bytes = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
            if (read_bytes > 0) {
                ssize_t i = 0;
                while (i < read_bytes) {
                    unsigned char ch = input_buffer[i++];
                    if (ch == 0x1b) {
                        unsigned char sequence[64];
                        size_t seq_len = multitask_read_escape_sequence(sequence, sizeof(sequence));

                        if (seq_len == 2 && (sequence[1] == 'n' || sequence[1] == 'N')) {
                            int new_index = multitask_spawn_session(sessions, budostack_path, session_rows, session_cols);
                            if (new_index > 0) {
                                active_index = new_index;
                            }
                        } else if (seq_len == 2 && sequence[1] >= '1' && sequence[1] <= '9') {
                            int desired = sequence[1] - '0';
                            if (multitask_find_slot_by_index(sessions, desired) >= 0) {
                                active_index = desired;
                            }
                        } else if (seq_len == 2 && (sequence[1] == 'd' || sequence[1] == 'D')) {
                            if (multitask_close_session(sessions, active_index) == 0) {
                                active_index = -1;
                            }
                        } else if (seq_len == 2 && (sequence[1] == 'q' || sequence[1] == 'Q')) {
                            running = 0;
                            break;
                        } else {
                            multitask_send_to_active(sessions, active_index, sequence, seq_len);
                        }
                    } else {
                        multitask_send_to_active(sessions, active_index, &ch, 1u);
                    }
                }
            }
        }
    }

    multitask_cleanup_sessions(sessions);
    multitask_restore_terminal();
    return EXIT_SUCCESS;
}
