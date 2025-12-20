#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MULTITASK_MAX_SESSIONS 9
#define MULTITASK_BUFFER_LIMIT 65536

struct multitask_session {
    pid_t pid;
    int master_fd;
    int exited;
    int exit_status;
    char buffer[MULTITASK_BUFFER_LIMIT];
    size_t buffer_len;
};

static struct termios original_termios;
static int terminal_configured = 0;

static void stop_sessions(struct multitask_session *sessions, size_t session_count);
static int interpret_status(int status);

static int interpret_status(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

static void restore_terminal(void)
{
    if (terminal_configured) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        printf("\033[?25h");
        fflush(stdout);
    }
}

static int set_raw_mode(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("tcgetattr");
        return -1;
    }

    raw = original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    terminal_configured = 1;
    atexit(restore_terminal);
    printf("\033[?25l");
    fflush(stdout);
    return 0;
}

static void trim_buffer(struct multitask_session *session, const char *data, size_t len)
{
    size_t copy_len = len;

    if (copy_len > MULTITASK_BUFFER_LIMIT) {
        data += copy_len - MULTITASK_BUFFER_LIMIT;
        copy_len = MULTITASK_BUFFER_LIMIT;
    }

    if (session->buffer_len + copy_len > MULTITASK_BUFFER_LIMIT) {
        size_t excess = (session->buffer_len + copy_len) - MULTITASK_BUFFER_LIMIT;
        memmove(session->buffer, session->buffer + excess, session->buffer_len - excess);
        session->buffer_len -= excess;
    }

    memcpy(session->buffer + session->buffer_len, data, copy_len);
    session->buffer_len += copy_len;
}

static void print_header(struct multitask_session *sessions, size_t session_count, size_t active)
{
    size_t i;

    printf("\033[2J\033[H");
    printf("multitask: %zu session(s) running BUDOSTACK (n/p to switch, 1-%zu to select, q to quit)\n", session_count, session_count);
    for (i = 0; i < session_count; i++) {
        const char state_char = sessions[i].exited ? 'x' : 'o';
        if (i == active) {
            printf(" *[%zu:%c]* ", i + 1, state_char);
        } else {
            printf("  [%zu:%c]  ", i + 1, state_char);
        }
    }
    printf("\n\n");
}

static void redraw_active(struct multitask_session *sessions, size_t session_count, size_t active)
{
    struct multitask_session *session = &sessions[active];
    print_header(sessions, session_count, active);

    if (session->exited) {
        printf("Session %zu exited with status %d.\n\n", active + 1, session->exit_status);
    }

    fwrite(session->buffer, 1, session->buffer_len, stdout);
    fflush(stdout);
}

static int open_master(char *slave_name, size_t slave_name_size)
{
    int master_fd;
    char *name_ptr;

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd == -1) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) == -1 || unlockpt(master_fd) == -1) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    name_ptr = ptsname(master_fd);
    if (name_ptr == NULL) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    strncpy(slave_name, name_ptr, slave_name_size - 1u);
    slave_name[slave_name_size - 1] = '\0';
    return master_fd;
}

static pid_t spawn_session(int master_fd, const char *slave_name)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        int slave_fd;

        if (setsid() == -1) {
            perror("setsid");
            _exit(EXIT_FAILURE);
        }

        slave_fd = open(slave_name, O_RDWR);
        if (slave_fd == -1) {
            perror("open slave pty");
            _exit(EXIT_FAILURE);
        }

#ifdef TIOCSCTTY
        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }
#endif

        if (dup2(slave_fd, STDIN_FILENO) == -1 || dup2(slave_fd, STDOUT_FILENO) == -1 || dup2(slave_fd, STDERR_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        close(master_fd);
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        execlp("./budostack", "budostack", (char *)NULL);
        execlp("budostack", "budostack", (char *)NULL);
        perror("execlp budostack");
        _exit(127);
    }

    return pid;
}

static int create_sessions(struct multitask_session *sessions, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        char slave_name[PATH_MAX];
        int master_fd = open_master(slave_name, sizeof(slave_name));

        if (master_fd == -1) {
            return -1;
        }

        sessions[i].pid = spawn_session(master_fd, slave_name);
        if (sessions[i].pid < 0) {
            close(master_fd);
            stop_sessions(sessions, i);
            return -1;
        }

        sessions[i].master_fd = master_fd;
        sessions[i].buffer_len = 0u;
        sessions[i].exited = 0;
        sessions[i].exit_status = 0;
    }

    return 0;
}

static void forward_input(int fd, char ch)
{
    ssize_t written;

    do {
        written = write(fd, &ch, 1);
    } while (written == -1 && errno == EINTR);
}

static void reap_children(struct multitask_session *sessions, size_t session_count)
{
    size_t i;

    for (i = 0; i < session_count; i++) {
        int status;
        pid_t result;

        if (sessions[i].exited) {
            continue;
        }

        result = waitpid(sessions[i].pid, &status, WNOHANG);
        if (result == sessions[i].pid) {
            sessions[i].exited = 1;
            sessions[i].exit_status = interpret_status(status);
        }
    }
}

static void stop_sessions(struct multitask_session *sessions, size_t session_count)
{
    size_t i;

    for (i = 0; i < session_count; i++) {
        if (!sessions[i].exited) {
            kill(sessions[i].pid, SIGTERM);
        }
    }

    for (i = 0; i < session_count; i++) {
        if (sessions[i].master_fd != -1) {
            close(sessions[i].master_fd);
            sessions[i].master_fd = -1;
        }
    }

    for (i = 0; i < session_count; i++) {
        if (!sessions[i].exited) {
            int status;
            waitpid(sessions[i].pid, &status, 0);
            sessions[i].exited = 1;
            sessions[i].exit_status = interpret_status(status);
        }
    }
}

static void handle_switch(struct multitask_session *sessions, size_t session_count, size_t *active, size_t new_index)
{
    if (new_index < session_count && *active != new_index) {
        *active = new_index;
        redraw_active(sessions, session_count, *active);
    }
}

int main(int argc, char **argv)
{
    struct multitask_session sessions[MULTITASK_MAX_SESSIONS];
    size_t session_count = 2u;
    size_t active = 0u;
    size_t i;
    int running = 1;

    for (i = 0; i < MULTITASK_MAX_SESSIONS; i++) {
        sessions[i].pid = -1;
        sessions[i].master_fd = -1;
        sessions[i].exited = 1;
        sessions[i].exit_status = 0;
        sessions[i].buffer_len = 0u;
    }

    if (argc == 2) {
        long requested = strtol(argv[1], NULL, 10);
        if (requested > 0 && requested <= MULTITASK_MAX_SESSIONS) {
            session_count = (size_t)requested;
        } else {
            fprintf(stderr, "Usage: %s [1-%d]\n", argv[0], MULTITASK_MAX_SESSIONS);
            return EXIT_FAILURE;
        }
    }

    if (set_raw_mode() == -1) {
        return EXIT_FAILURE;
    }

    if (create_sessions(sessions, session_count) == -1) {
        fprintf(stderr, "Failed to create sessions.\n");
        restore_terminal();
        return EXIT_FAILURE;
    }

    redraw_active(sessions, session_count, active);

    while (running) {
        fd_set readfds;
        int max_fd = STDIN_FILENO;
        size_t i;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        for (i = 0; i < session_count; i++) {
            if (sessions[i].master_fd != -1) {
                FD_SET(sessions[i].master_fd, &readfds);
                if (sessions[i].master_fd > max_fd) {
                    max_fd = sessions[i].master_fd;
                }
            }
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            ssize_t r = read(STDIN_FILENO, &ch, 1);

            if (r == 1) {
                if (ch == 'q' || ch == 'Q' || ch == 3) {
                    running = 0;
                } else if (ch == 'n') {
                    handle_switch(sessions, session_count, &active, (active + 1u) % session_count);
                } else if (ch == 'p') {
                    handle_switch(sessions, session_count, &active, (active == 0u) ? session_count - 1u : active - 1u);
                } else if (ch >= '1' && ch <= '9') {
                    size_t idx = (size_t)(ch - '1');
                    handle_switch(sessions, session_count, &active, idx);
                } else {
                    if (!sessions[active].exited && sessions[active].master_fd != -1) {
                        forward_input(sessions[active].master_fd, ch);
                    }
                }
            } else if (r == 0) {
                running = 0;
            }
        }

        for (i = 0; i < session_count; i++) {
            if (sessions[i].master_fd != -1 && FD_ISSET(sessions[i].master_fd, &readfds)) {
                char buf[1024];
                ssize_t n = read(sessions[i].master_fd, buf, sizeof(buf));

                if (n > 0) {
                    trim_buffer(&sessions[i], buf, (size_t)n);
                    if (i == active) {
                        fwrite(buf, 1, (size_t)n, stdout);
                        fflush(stdout);
                    }
                } else if (n == 0) {
                    sessions[i].exited = 1;
                    close(sessions[i].master_fd);
                    sessions[i].master_fd = -1;
                    redraw_active(sessions, session_count, active);
                } else if (errno != EINTR && errno != EAGAIN) {
                    sessions[i].exited = 1;
                    close(sessions[i].master_fd);
                    sessions[i].master_fd = -1;
                    redraw_active(sessions, session_count, active);
                }
            }
        }

        reap_children(sessions, session_count);
    }

    stop_sessions(sessions, session_count);
    restore_terminal();
    printf("\nmultitask finished.\n");
    return 0;
}
