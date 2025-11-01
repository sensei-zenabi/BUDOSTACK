#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_PARTICIPANTS 4
#define USERNAME_LEN 32
#define FRAME_COLS 32
#define FRAME_ROWS 16
#define FRAME_SIZE (FRAME_COLS * FRAME_ROWS)
#define DEFAULT_PORT 60080
#define FRAME_INTERVAL_USEC 200000
#define RENDER_INTERVAL_USEC 100000

enum message_type {
    MSG_JOIN = 1,
    MSG_ACCEPT = 2,
    MSG_FRAME = 3,
    MSG_MUTE = 4,
    MSG_ROSTER = 5,
    MSG_LEAVE = 6
};

typedef struct {
    int active;
    int muted;
    char username[USERNAME_LEN];
    char frame[FRAME_SIZE];
} slot_view_t;

typedef struct {
    int fd;
    int active;
    uint8_t slot;
    int muted;
    char username[USERNAME_LEN];
} remote_client_t;

typedef struct {
    uint8_t type;
    uint8_t slot;
    uint16_t reserved;
    uint32_t size;
} message_header_t;

typedef struct app_state {
    int is_server;
    int running;
    int local_slot;
    int local_muted;
    unsigned short port;
    int listen_fd;
    int socket_fd;
    char username[USERNAME_LEN];
    const char *input_path;

    slot_view_t slots[MAX_PARTICIPANTS];
    pthread_mutex_t slots_mutex;

    remote_client_t clients[MAX_PARTICIPANTS];
    pthread_mutex_t clients_mutex;

    pthread_t render_thread;
    pthread_t frame_thread;
    pthread_t input_thread;
    pthread_t network_thread;

    struct termios orig_termios;
    int raw_enabled;
} app_state_t;

static app_state_t *global_state = NULL;

static void sleep_for_usecs(long usec)
{
    if (usec <= 0) {
        return;
    }
    struct timespec req;
    req.tv_sec = usec / 1000000L;
    req.tv_nsec = (usec % 1000000L) * 1000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        continue;
    }
}

static void disable_raw_mode(void)
{
    if (global_state != NULL && global_state->raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_state->orig_termios);
        global_state->raw_enabled = 0;
    }
}

static void enable_raw_mode(app_state_t *state)
{
    if (tcgetattr(STDIN_FILENO, &state->orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disable_raw_mode);
    struct termios raw = state->orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    state->raw_enabled = 1;
}

static void handle_signal(int sig)
{
    (void)sig;
    if (global_state != NULL) {
        global_state->running = 0;
    }
}

static void fill_blank_frame(char *frame)
{
    memset(frame, ' ', FRAME_SIZE);
}

static void initialize_slots(app_state_t *state)
{
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        state->slots[i].active = 0;
        state->slots[i].muted = 0;
        snprintf(state->slots[i].username, USERNAME_LEN, "-");
        fill_blank_frame(state->slots[i].frame);
    }
}

static void initialize_clients(app_state_t *state)
{
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        state->clients[i].fd = -1;
        state->clients[i].active = 0;
        state->clients[i].slot = (uint8_t)i;
        state->clients[i].muted = 0;
        memset(state->clients[i].username, 0, sizeof(state->clients[i].username));
    }
}

static void update_slot_frame(app_state_t *state, uint8_t slot, const char *frame)
{
    if (slot >= MAX_PARTICIPANTS) {
        return;
    }
    pthread_mutex_lock(&state->slots_mutex);
    memcpy(state->slots[slot].frame, frame, FRAME_SIZE);
    pthread_mutex_unlock(&state->slots_mutex);
}

static void update_slot_meta(app_state_t *state, uint8_t slot, const char *username, int active, int muted)
{
    if (slot >= MAX_PARTICIPANTS) {
        return;
    }
    pthread_mutex_lock(&state->slots_mutex);
    slot_view_t *view = &state->slots[slot];
    view->active = active;
    view->muted = muted;
    if (username != NULL) {
        snprintf(view->username, USERNAME_LEN, "%s", username);
    }
    if (!active) {
        fill_blank_frame(view->frame);
        view->muted = 0;
    }
    pthread_mutex_unlock(&state->slots_mutex);
}

static int write_full(int fd, const void *buf, size_t len)
{
    const unsigned char *ptr = buf;
    size_t total = 0;
    while (total < len) {
        ssize_t written = write(fd, ptr + total, len - total);
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

static int read_full(int fd, void *buf, size_t len)
{
    unsigned char *ptr = buf;
    size_t total = 0;
    while (total < len) {
        ssize_t rd = read(fd, ptr + total, len - total);
        if (rd == 0) {
            return 0;
        }
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)rd;
    }
    return 1;
}

static int send_message(int fd, uint8_t type, uint8_t slot, const void *payload, uint32_t payload_size)
{
    message_header_t header;
    header.type = type;
    header.slot = slot;
    header.reserved = 0;
    header.size = htonl(payload_size);
    if (write_full(fd, &header, sizeof(header)) < 0) {
        return -1;
    }
    if (payload_size > 0 && payload != NULL) {
        if (write_full(fd, payload, payload_size) < 0) {
            return -1;
        }
    }
    return 0;
}

static int receive_header(int fd, message_header_t *header)
{
    int rc = read_full(fd, header, sizeof(*header));
    if (rc <= 0) {
        return rc;
    }
    header->size = ntohl(header->size);
    return 1;
}

static size_t build_roster_string(app_state_t *state, char *out, size_t out_size)
{
    size_t len = 0;
    pthread_mutex_lock(&state->slots_mutex);
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        const slot_view_t *view = &state->slots[i];
        int written = snprintf(out + len, out_size - len, "%d %d %d %s\n", i, view->active, view->muted, view->username);
        if (written < 0) {
            len = 0;
            break;
        }
        if ((size_t)written >= out_size - len) {
            len = out_size - 1;
            out[len] = '\0';
            break;
        }
        len += (size_t)written;
    }
    pthread_mutex_unlock(&state->slots_mutex);
    return len;
}

static int server_remove_client_fd(app_state_t *state, int fd);
static void send_roster_to_all(app_state_t *state)
{
    for (;;) {
        char buffer[512];
        size_t len = build_roster_string(state, buffer, sizeof(buffer));
        if (len == 0) {
            return;
        }
        int fds[MAX_PARTICIPANTS];
        size_t count = 0;

        pthread_mutex_lock(&state->clients_mutex);
        for (int i = 0; i < MAX_PARTICIPANTS; i++) {
            if (state->clients[i].active) {
                fds[count++] = state->clients[i].fd;
            }
        }
        pthread_mutex_unlock(&state->clients_mutex);

        int removed = 0;
        for (size_t i = 0; i < count; i++) {
            if (send_message(fds[i], MSG_ROSTER, 0, buffer, (uint32_t)len) < 0) {
                if (server_remove_client_fd(state, fds[i]) >= 0) {
                    removed = 1;
                }
            }
        }
        if (!removed) {
            break;
        }
    }
}

static int send_existing_frames_to_client(app_state_t *state, int fd)
{
    pthread_mutex_lock(&state->slots_mutex);
    slot_view_t snapshot[MAX_PARTICIPANTS];
    memcpy(snapshot, state->slots, sizeof(snapshot));
    pthread_mutex_unlock(&state->slots_mutex);

    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        if (!snapshot[i].active) {
            continue;
        }
        if (send_message(fd, MSG_FRAME, (uint8_t)i, snapshot[i].frame, FRAME_SIZE) < 0) {
            return -1;
        }
    }
    return 0;
}

static void server_broadcast_frame(app_state_t *state, uint8_t slot, const char *frame, int exclude_fd)
{
    int fds[MAX_PARTICIPANTS];
    size_t count = 0;

    pthread_mutex_lock(&state->clients_mutex);
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        if (!state->clients[i].active) {
            continue;
        }
        if (state->clients[i].fd == exclude_fd) {
            continue;
        }
        fds[count++] = state->clients[i].fd;
    }
    pthread_mutex_unlock(&state->clients_mutex);

    int removed = 0;
    for (size_t i = 0; i < count; i++) {
        if (send_message(fds[i], MSG_FRAME, slot, frame, FRAME_SIZE) < 0) {
            if (server_remove_client_fd(state, fds[i]) >= 0) {
                removed = 1;
            }
        }
    }
    if (removed) {
        send_roster_to_all(state);
    }
}

static void close_all_clients(app_state_t *state)
{
    pthread_mutex_lock(&state->clients_mutex);
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        if (state->clients[i].active) {
            close(state->clients[i].fd);
            state->clients[i].fd = -1;
            state->clients[i].active = 0;
            state->clients[i].muted = 0;
            memset(state->clients[i].username, 0, sizeof(state->clients[i].username));
        }
    }
    pthread_mutex_unlock(&state->clients_mutex);
}

static int find_free_slot(app_state_t *state)
{
    int slot = -1;
    pthread_mutex_lock(&state->slots_mutex);
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        if (!state->slots[i].active) {
            slot = i;
            break;
        }
    }
    pthread_mutex_unlock(&state->slots_mutex);
    return slot;
}

static int server_remove_client_fd(app_state_t *state, int fd)
{
    int slot = -1;

    pthread_mutex_lock(&state->clients_mutex);
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        if (state->clients[i].active && state->clients[i].fd == fd) {
            slot = state->clients[i].slot;
            close(state->clients[i].fd);
            state->clients[i].fd = -1;
            state->clients[i].active = 0;
            state->clients[i].muted = 0;
            memset(state->clients[i].username, 0, sizeof(state->clients[i].username));
            break;
        }
    }
    pthread_mutex_unlock(&state->clients_mutex);

    if (slot >= 0) {
        update_slot_meta(state, (uint8_t)slot, "-", 0, 0);
    }
    return slot;
}

static int server_handle_join(app_state_t *state, int fd)
{
    message_header_t header;
    int rc = receive_header(fd, &header);
    if (rc <= 0) {
        return -1;
    }
    if (header.type != MSG_JOIN || header.size == 0 || header.size > USERNAME_LEN) {
        return -1;
    }
    char username[USERNAME_LEN];
    memset(username, 0, sizeof(username));
    if (read_full(fd, username, header.size) <= 0) {
        return -1;
    }
    username[USERNAME_LEN - 1] = '\0';

    int slot = find_free_slot(state);
    if (slot < 0) {
        unsigned char reject = 255;
        (void)send_message(fd, MSG_ACCEPT, 0, &reject, 1);
        close(fd);
        return -1;
    }

    unsigned char assigned = (unsigned char)slot;
    if (send_message(fd, MSG_ACCEPT, 0, &assigned, 1) < 0) {
        close(fd);
        return -1;
    }

    pthread_mutex_lock(&state->clients_mutex);
    state->clients[slot].fd = fd;
    state->clients[slot].active = 1;
    state->clients[slot].slot = (uint8_t)slot;
    state->clients[slot].muted = 0;
    snprintf(state->clients[slot].username, USERNAME_LEN, "%s", username);
    pthread_mutex_unlock(&state->clients_mutex);

    update_slot_meta(state, (uint8_t)slot, username, 1, 0);
    if (send_existing_frames_to_client(state, fd) < 0) {
        server_remove_client_fd(state, fd);
        send_roster_to_all(state);
        return -1;
    }
    send_roster_to_all(state);
    return 0;
}

static int server_handle_client_message(app_state_t *state, remote_client_t *client)
{
    message_header_t header;
    int rc = receive_header(client->fd, &header);
    if (rc <= 0) {
        return -1;
    }

    if (header.size > 4096) {
        return -1;
    }

    if (header.type == MSG_FRAME) {
        if (header.size != FRAME_SIZE) {
            return -1;
        }
        char frame[FRAME_SIZE];
        if (read_full(client->fd, frame, FRAME_SIZE) <= 0) {
            return -1;
        }
        update_slot_frame(state, client->slot, frame);
        server_broadcast_frame(state, client->slot, frame, client->fd);
        return 0;
    }

    if (header.type == MSG_MUTE) {
        if (header.size != 1) {
            return -1;
        }
        unsigned char value = 0;
        if (read_full(client->fd, &value, 1) <= 0) {
            return -1;
        }
        client->muted = value ? 1 : 0;
        update_slot_meta(state, client->slot, client->username, 1, client->muted);
        send_roster_to_all(state);
        return 0;
    }

    if (header.type == MSG_LEAVE) {
        return -1;
    }

    if (header.size > 0) {
        char discard[512];
        size_t remaining = header.size;
        while (remaining > 0) {
            size_t chunk = remaining > sizeof(discard) ? sizeof(discard) : remaining;
            if (read_full(client->fd, discard, chunk) <= 0) {
                break;
            }
            remaining -= chunk;
        }
    }
    return 0;
}

static void *server_network_thread(void *arg)
{
    app_state_t *state = arg;
    while (state->running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        if (state->listen_fd >= 0) {
            FD_SET(state->listen_fd, &readfds);
            max_fd = state->listen_fd;
        }

        pthread_mutex_lock(&state->clients_mutex);
        for (int i = 0; i < MAX_PARTICIPANTS; i++) {
            if (!state->clients[i].active) {
                continue;
            }
            FD_SET(state->clients[i].fd, &readfds);
            if (state->clients[i].fd > max_fd) {
                max_fd = state->clients[i].fd;
            }
        }
        pthread_mutex_unlock(&state->clients_mutex);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (!state->running) {
            break;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (state->listen_fd >= 0 && FD_ISSET(state->listen_fd, &readfds)) {
            struct sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);
            int fd = accept(state->listen_fd, (struct sockaddr *)&addr, &addrlen);
            if (fd >= 0) {
                if (server_handle_join(state, fd) < 0) {
                    close(fd);
                }
            }
        }

        pthread_mutex_lock(&state->clients_mutex);
        for (int i = 0; i < MAX_PARTICIPANTS; i++) {
            if (!state->clients[i].active) {
                continue;
            }
            if (FD_ISSET(state->clients[i].fd, &readfds)) {
                if (server_handle_client_message(state, &state->clients[i]) < 0) {
                    int fd = state->clients[i].fd;
                    pthread_mutex_unlock(&state->clients_mutex);
                    if (server_remove_client_fd(state, fd) >= 0) {
                        send_roster_to_all(state);
                    }
                    pthread_mutex_lock(&state->clients_mutex);
                }
            }
        }
        pthread_mutex_unlock(&state->clients_mutex);
    }
    return NULL;
}

static void client_handle_roster(app_state_t *state, const char *buffer, size_t len)
{
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, buffer, len);
    copy[len] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr); line != NULL; line = strtok_r(NULL, "\n", &saveptr)) {
        int slot = -1;
        int active = 0;
        int muted = 0;
        char name[USERNAME_LEN];
        name[0] = '\0';
        if (sscanf(line, "%d %d %d %31s", &slot, &active, &muted, name) == 4) {
            if (slot >= 0 && slot < MAX_PARTICIPANTS) {
                update_slot_meta(state, (uint8_t)slot, name, active, muted);
            }
        }
    }
    free(copy);
}

static void *client_network_thread(void *arg)
{
    app_state_t *state = arg;
    while (state->running) {
        message_header_t header;
        int rc = receive_header(state->socket_fd, &header);
        if (rc <= 0) {
            state->running = 0;
            break;
        }
        if (header.size > 4096) {
            state->running = 0;
            break;
        }
        if (header.type == MSG_FRAME && header.size == FRAME_SIZE) {
            char frame[FRAME_SIZE];
            if (read_full(state->socket_fd, frame, FRAME_SIZE) <= 0) {
                state->running = 0;
                break;
            }
            update_slot_frame(state, header.slot, frame);
            continue;
        }
        if (header.type == MSG_ROSTER) {
            char *payload = malloc(header.size);
            if (payload == NULL) {
                state->running = 0;
                break;
            }
            if (read_full(state->socket_fd, payload, header.size) <= 0) {
                free(payload);
                state->running = 0;
                break;
            }
            client_handle_roster(state, payload, header.size);
            free(payload);
            continue;
        }
        if (header.type == MSG_MUTE && header.size == 1) {
            unsigned char value = 0;
            if (read_full(state->socket_fd, &value, 1) <= 0) {
                state->running = 0;
                break;
            }
            slot_view_t snapshot;
            pthread_mutex_lock(&state->slots_mutex);
            if (header.slot < MAX_PARTICIPANTS) {
                snapshot = state->slots[header.slot];
            } else {
                memset(&snapshot, 0, sizeof(snapshot));
            }
            pthread_mutex_unlock(&state->slots_mutex);
            update_slot_meta(state, header.slot, snapshot.username, snapshot.active, value ? 1 : 0);
            continue;
        }
        if (header.size > 0) {
            char discard[512];
            size_t remaining = header.size;
            while (remaining > 0) {
                size_t chunk = remaining > sizeof(discard) ? sizeof(discard) : remaining;
                if (read_full(state->socket_fd, discard, chunk) <= 0) {
                    state->running = 0;
                    break;
                }
                remaining -= chunk;
            }
        }
    }
    return NULL;
}

static void render_grid(const slot_view_t *slots)
{
    printf("\033[H");

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            int idx = row * 2 + col;
            const slot_view_t *view = &slots[idx];
            const char *status;
            if (!view->active) {
                status = "(offline)";
            } else if (view->muted) {
                status = "(muted)";
            } else {
                status = "(live)";
            }
            printf("Slot %d - %s %s\n", idx, view->username, status);
        }
    }

    for (int r = 0; r < FRAME_ROWS; r++) {
        const char *left = slots[0].frame + r * FRAME_COLS;
        const char *right = slots[1].frame + r * FRAME_COLS;
        fwrite(left, 1, FRAME_COLS, stdout);
        printf("  ");
        fwrite(right, 1, FRAME_COLS, stdout);
        printf("\n");
    }
    printf("\n");
    for (int r = 0; r < FRAME_ROWS; r++) {
        const char *left = slots[2].frame + r * FRAME_COLS;
        const char *right = slots[3].frame + r * FRAME_COLS;
        fwrite(left, 1, FRAME_COLS, stdout);
        printf("  ");
        fwrite(right, 1, FRAME_COLS, stdout);
        printf("\n");
    }

    printf("\nPress M to toggle mute. Press Q to quit.\n");
    fflush(stdout);
}

static void *render_thread_func(void *arg)
{
    app_state_t *state = arg;
    printf("\033[2J\033[H");
    while (state->running) {
        slot_view_t snapshot[MAX_PARTICIPANTS];
        pthread_mutex_lock(&state->slots_mutex);
        memcpy(snapshot, state->slots, sizeof(snapshot));
        pthread_mutex_unlock(&state->slots_mutex);
        render_grid(snapshot);
        sleep_for_usecs(RENDER_INTERVAL_USEC);
    }
    return NULL;
}

static void generate_test_pattern(char *frame, size_t tick)
{
    const char *palette = " .:-=+*#%@";
    size_t palette_len = strlen(palette);
    for (int r = 0; r < FRAME_ROWS; r++) {
        for (int c = 0; c < FRAME_COLS; c++) {
            size_t idx = (size_t)((r + c + (int)tick) % (int)palette_len);
            frame[r * FRAME_COLS + c] = palette[idx];
        }
    }
}

static int read_frame_from_file(FILE *fp, char *frame)
{
    char line[256];
    for (int r = 0; r < FRAME_ROWS; r++) {
        if (fgets(line, sizeof(line), fp) == NULL) {
            return -1;
        }
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        for (int c = 0; c < FRAME_COLS; c++) {
            if ((size_t)c < len) {
                frame[r * FRAME_COLS + c] = line[c];
            } else {
                frame[r * FRAME_COLS + c] = ' ';
            }
        }
    }
    return 0;
}

static void send_frame_from_local(app_state_t *state, const char *frame)
{
    if (state->is_server) {
        server_broadcast_frame(state, (uint8_t)state->local_slot, frame, -1);
    } else if (state->socket_fd >= 0) {
        if (send_message(state->socket_fd, MSG_FRAME, (uint8_t)state->local_slot, frame, FRAME_SIZE) < 0) {
            state->running = 0;
        }
    }
}

static void *frame_thread_func(void *arg)
{
    app_state_t *state = arg;
    FILE *input = NULL;
    if (state->input_path != NULL) {
        input = fopen(state->input_path, "r");
        if (input == NULL) {
            fprintf(stderr, "Failed to open input file %s\n", state->input_path);
        }
    }
    char frame[FRAME_SIZE];
    size_t tick = 0;

    while (state->running) {
        if (input != NULL) {
            if (read_frame_from_file(input, frame) < 0) {
                clearerr(input);
                if (fseek(input, 0, SEEK_SET) == 0) {
                    continue;
                }
                break;
            }
        } else {
            generate_test_pattern(frame, tick++);
        }
        update_slot_frame(state, (uint8_t)state->local_slot, frame);
        send_frame_from_local(state, frame);
        sleep_for_usecs(FRAME_INTERVAL_USEC);
    }
    if (input != NULL) {
        fclose(input);
    }
    return NULL;
}

static void broadcast_local_mute(app_state_t *state)
{
    if (state->is_server) {
        send_roster_to_all(state);
    } else if (state->socket_fd >= 0) {
        unsigned char value = (unsigned char)state->local_muted;
        if (send_message(state->socket_fd, MSG_MUTE, (uint8_t)state->local_slot, &value, 1) < 0) {
            state->running = 0;
        }
    }
}

static void *input_thread_func(void *arg)
{
    app_state_t *state = arg;
    while (state->running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (!state->running) {
            break;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }
        char ch;
        ssize_t rd = read(STDIN_FILENO, &ch, 1);
        if (rd <= 0) {
            continue;
        }
        if (ch == 'm' || ch == 'M') {
            state->local_muted = !state->local_muted;
            update_slot_meta(state, (uint8_t)state->local_slot, state->username, 1, state->local_muted);
            broadcast_local_mute(state);
        } else if (ch == 'q' || ch == 'Q') {
            if (!state->is_server && state->socket_fd >= 0) {
                (void)send_message(state->socket_fd, MSG_LEAVE, (uint8_t)state->local_slot, NULL, 0);
            }
            state->running = 0;
            break;
        }
    }
    return NULL;
}

static void stop_running(app_state_t *state)
{
    state->running = 0;
    if (state->is_server) {
        if (state->listen_fd >= 0) {
            close(state->listen_fd);
            state->listen_fd = -1;
        }
        close_all_clients(state);
    } else if (state->socket_fd >= 0) {
        shutdown(state->socket_fd, SHUT_RDWR);
        close(state->socket_fd);
        state->socket_fd = -1;
    }
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <username> [server_host] [--input path] [--port port]\n", prog);
    fprintf(stderr, "If server_host is omitted, the program runs as the session host.\n");
    fprintf(stderr, "Frames default to a test pattern; use --input to read ASCII frames from a file.\n");
}

static int parse_arguments(int argc, char **argv, char **username, char **host, char **input_path, unsigned short *port)
{
    *username = NULL;
    *host = NULL;
    *input_path = NULL;
    *port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            *input_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (end == NULL || *end != '\0' || value <= 0 || value > 65535) {
                return -1;
            }
            *port = (unsigned short)value;
            continue;
        }
        if (*username == NULL) {
            *username = argv[i];
        } else if (*host == NULL) {
            *host = argv[i];
        } else {
            return -1;
        }
    }
    if (*username == NULL) {
        return -1;
    }
    return 0;
}

static int start_server(app_state_t *state)
{
    state->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (state->listen_fd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    if (setsockopt(state->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(state->listen_fd);
        state->listen_fd = -1;
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(state->port);
    if (bind(state->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(state->listen_fd);
        state->listen_fd = -1;
        return -1;
    }
    if (listen(state->listen_fd, MAX_PARTICIPANTS) < 0) {
        perror("listen");
        close(state->listen_fd);
        state->listen_fd = -1;
        return -1;
    }
    state->local_slot = 0;
    update_slot_meta(state, (uint8_t)state->local_slot, state->username, 1, state->local_muted);
    return 0;
}

static int start_client(app_state_t *state, const char *host)
{
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", state->port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        fprintf(stderr, "Unable to connect to %s:%u\n", host, state->port);
        return -1;
    }
    state->socket_fd = fd;

    size_t name_len = strnlen(state->username, USERNAME_LEN - 1) + 1;
    if (send_message(state->socket_fd, MSG_JOIN, 0, state->username, (uint32_t)name_len) < 0) {
        perror("send");
        return -1;
    }
    message_header_t header;
    int rc = receive_header(state->socket_fd, &header);
    if (rc <= 0) {
        fprintf(stderr, "Connection closed during handshake\n");
        return -1;
    }
    if (header.type != MSG_ACCEPT || header.size != 1) {
        fprintf(stderr, "Unexpected handshake response\n");
        return -1;
    }
    unsigned char slot = 0;
    if (read_full(state->socket_fd, &slot, 1) <= 0) {
        fprintf(stderr, "Failed to read slot assignment\n");
        return -1;
    }
    if (slot == 255) {
        fprintf(stderr, "Session is full\n");
        return -1;
    }
    state->local_slot = slot;
    update_slot_meta(state, (uint8_t)state->local_slot, state->username, 1, state->local_muted);
    return 0;
}

int main(int argc, char **argv)
{
    app_state_t state;
    memset(&state, 0, sizeof(state));
    state.running = 1;
    state.local_slot = 0;
    state.local_muted = 0;
    state.listen_fd = -1;
    state.socket_fd = -1;
    state.raw_enabled = 0;

    if (pthread_mutex_init(&state.slots_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to init mutex\n");
        return EXIT_FAILURE;
    }
    if (pthread_mutex_init(&state.clients_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to init mutex\n");
        pthread_mutex_destroy(&state.slots_mutex);
        return EXIT_FAILURE;
    }

    initialize_slots(&state);
    initialize_clients(&state);

    char *username = NULL;
    char *host = NULL;
    char *input_path = NULL;
    unsigned short port = DEFAULT_PORT;
    if (parse_arguments(argc, argv, &username, &host, &input_path, &port) < 0) {
        usage(argv[0]);
        pthread_mutex_destroy(&state.clients_mutex);
        pthread_mutex_destroy(&state.slots_mutex);
        return EXIT_FAILURE;
    }
    state.port = port;
    state.input_path = input_path;
    snprintf(state.username, USERNAME_LEN, "%s", username);

    enable_raw_mode(&state);
    global_state = &state;
    signal(SIGINT, handle_signal);

    if (host == NULL) {
        state.is_server = 1;
        if (start_server(&state) < 0) {
            disable_raw_mode();
            pthread_mutex_destroy(&state.clients_mutex);
            pthread_mutex_destroy(&state.slots_mutex);
            return EXIT_FAILURE;
        }
    } else {
        state.is_server = 0;
        if (start_client(&state, host) < 0) {
            disable_raw_mode();
            pthread_mutex_destroy(&state.clients_mutex);
            pthread_mutex_destroy(&state.slots_mutex);
            return EXIT_FAILURE;
        }
    }

    int network_thread_created = 0;
    int render_thread_created = 0;
    int frame_thread_created = 0;
    int input_thread_created = 0;

    if (state.is_server) {
        if (pthread_create(&state.network_thread, NULL, server_network_thread, &state) != 0) {
            fprintf(stderr, "Failed to create server thread\n");
            stop_running(&state);
        } else {
            network_thread_created = 1;
        }
    } else {
        if (pthread_create(&state.network_thread, NULL, client_network_thread, &state) != 0) {
            fprintf(stderr, "Failed to create client thread\n");
            stop_running(&state);
        } else {
            network_thread_created = 1;
        }
    }

    if (pthread_create(&state.render_thread, NULL, render_thread_func, &state) != 0) {
        fprintf(stderr, "Failed to create render thread\n");
        stop_running(&state);
    } else {
        render_thread_created = 1;
    }
    if (pthread_create(&state.frame_thread, NULL, frame_thread_func, &state) != 0) {
        fprintf(stderr, "Failed to create frame thread\n");
        stop_running(&state);
    } else {
        frame_thread_created = 1;
    }
    if (pthread_create(&state.input_thread, NULL, input_thread_func, &state) != 0) {
        fprintf(stderr, "Failed to create input thread\n");
        stop_running(&state);
    } else {
        input_thread_created = 1;
    }

    if (input_thread_created) {
        pthread_join(state.input_thread, NULL);
    }
    state.running = 0;
    stop_running(&state);

    if (frame_thread_created) {
        pthread_join(state.frame_thread, NULL);
    }
    if (render_thread_created) {
        pthread_join(state.render_thread, NULL);
    }
    if (network_thread_created) {
        pthread_join(state.network_thread, NULL);
    }

    disable_raw_mode();
    printf("\033[2J\033[HSession ended.\n");

    pthread_mutex_destroy(&state.clients_mutex);
    pthread_mutex_destroy(&state.slots_mutex);
    return EXIT_SUCCESS;
}
