#define _POSIX_C_SOURCE 200809L
#include "budo_gfx.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>

#define GFX_MAGIC 0x42474658u
#define GFX_HELLO 1u
#define GFX_ACK 2u
#define GFX_FRAME 3u
#define GFX_EVENT 4u
#define GFX_QUEUE 256u
#define GFX_MAX_WIDTH 1920u
#define GFX_MAX_HEIGHT 1080u

struct gfx_packet {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t slot;
    struct budo_gfx_event event;
};

_Static_assert(sizeof(struct gfx_packet) == 56u, "graphics packet layout changed");

struct budo_gfx {
    int fd;
    uint8_t *mapping;
    size_t length;
    size_t frame_bytes;
    unsigned int slot;
    unsigned int format;
    int keyboard_grab;
    struct budo_gfx_event events[GFX_QUEUE];
    size_t head;
    size_t count;
    int motion_direction_x;
    int motion_direction_y;
};

struct budo_gfx_host {
    int listener;
    int fd;
    char directory[96];
    char path[108];
    uint8_t *mapping;
    uint8_t *rgba;
    size_t length;
    size_t frame_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    int dirty;
    int keyboard_grab;
    int64_t handshake_started;
};

static struct gfx_packet gfx_packet(uint32_t type) {
    struct gfx_packet packet = {0};
    packet.magic = GFX_MAGIC;
    packet.version = BUDO_GFX_VERSION;
    packet.type = type;
    return packet;
}

static int64_t gfx_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int gfx_open_failed(struct budo_gfx *gfx) {
    int saved_errno = errno;
    budo_gfx_close(gfx);
    errno = saved_errno;
    return -1;
}

static int gfx_layout(uint32_t width, uint32_t height, uint32_t format,
                       size_t *bytes, size_t *length) {
    if (width == 0u || height == 0u || width > GFX_MAX_WIDTH || height > GFX_MAX_HEIGHT ||
        (format != BUDO_GFX_ARGB8888 && format != BUDO_GFX_INDEX8)) {
        errno = EINVAL;
        return -1;
    }
    *bytes = (size_t)width * height * (format == BUDO_GFX_INDEX8 ? 1u : 4u);
    *length = 2u * (*bytes + 256u * sizeof(uint32_t));
    return 0;
}

static int gfx_socket(void) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int gfx_send(int fd, const struct gfx_packet *packet) {
    ssize_t result;
    do {
        result = send(fd, packet, sizeof(*packet), MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (result < 0 && errno == EINTR);
    return result == (ssize_t)sizeof(*packet) ? 0 : -1;
}

static int gfx_receive(int fd, struct gfx_packet *packet, int *shared_fd) {
    struct iovec iov = {packet, sizeof(*packet)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr message = {0};
    memset(&control, 0, sizeof(control));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    *shared_fd = -1;
    ssize_t result = recvmsg(fd, &message, MSG_DONTWAIT);
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    int invalid_rights = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message); cmsg;
         cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; i++) {
                int received_fd;
                memcpy(&received_fd, (char *)CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));
                if (*shared_fd < 0 && !invalid_rights) {
                    *shared_fd = received_fd;
                } else {
                    close(received_fd);
                    invalid_rights = 1;
                }
            }
        }
    }
    if (invalid_rights || result != (ssize_t)sizeof(*packet) ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
        packet->magic != GFX_MAGIC || packet->version != BUDO_GFX_VERSION) {
        if (*shared_fd >= 0) {
            close(*shared_fd);
            *shared_fd = -1;
        }
        errno = EPROTO;
        return -1;
    }
    return 1;
}

static int gfx_client_receive(struct budo_gfx *gfx, struct gfx_packet *packet) {
    int shared_fd;
    int result = gfx_receive(gfx->fd, packet, &shared_fd);
    if (shared_fd >= 0) {
        close(shared_fd);
        return -1;
    }
    if (result == 1 && packet->type == GFX_EVENT) {
        /* Pointer motion is a position snapshot. Keep the newest position
         * between discrete events so high-rate mice cannot fill the queue
         * with stale coordinates while a frame acknowledgement is pending. */
        if (packet->event.type == BUDO_GFX_MOUSE_MOVE && gfx->count > 0u) {
            size_t last = (gfx->head + gfx->count - 1u) % GFX_QUEUE;
            if (gfx->events[last].type == BUDO_GFX_MOUSE_MOVE) {
                int64_t dx = (int64_t)packet->event.x - gfx->events[last].x;
                int64_t dy = (int64_t)packet->event.y - gfx->events[last].y;
                int direction_x = (dx > 0) - (dx < 0);
                int direction_y = (dy > 0) - (dy < 0);
                int reversed = ((!gfx->motion_direction_x && !gfx->motion_direction_y) &&
                                (direction_x || direction_y)) ||
                               (direction_x && gfx->motion_direction_x &&
                                direction_x != gfx->motion_direction_x) ||
                               (direction_y && gfx->motion_direction_y &&
                                direction_y != gfx->motion_direction_y);
                if (direction_x) gfx->motion_direction_x = direction_x;
                if (direction_y) gfx->motion_direction_y = direction_y;
                /* Retain turning points: edge-clamped clients must see the
                 * outward endpoint before the first inward movement. */
                if (!reversed) {
                    gfx->events[last] = packet->event;
                    return result;
                }
            }
        } else {
            gfx->motion_direction_x = gfx->motion_direction_y = 0;
        }
        if (gfx->count == GFX_QUEUE) {
            /* Stop rather than lose a key release and leave a key stuck. */
            errno = ENOBUFS;
            return -1;
        }
        gfx->events[(gfx->head + gfx->count) % GFX_QUEUE] = packet->event;
        gfx->count++;
    }
    return result;
}

static int gfx_wait_ack(struct budo_gfx *gfx) {
    /* Absolute deadline remains bounded even under a stream of input. */
    int64_t deadline = gfx_milliseconds() + 2000;
    while (gfx_milliseconds() < deadline) {
        struct pollfd pollfd = {gfx->fd, POLLIN, 0};
        int result = poll(&pollfd, 1, 10);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return -1;
        }
        if (result == 0) {
            continue;
        }
        struct gfx_packet packet;
        result = gfx_client_receive(gfx, &packet);
        if (result < 0) {
            return -1;
        }
        if (result == 1 && packet.type == GFX_ACK) {
            return 0;
        }
        if (result == 1 && packet.type != GFX_EVENT) {
            errno = EPROTO;
            return -1;
        }
    }
    errno = ETIMEDOUT;
    return -1;
}

int budo_gfx_open(struct budo_gfx **out, unsigned int width,
                  unsigned int height, unsigned int format) {
    const char *path = getenv("BUDOSTACK_GFX_SOCKET");
    size_t bytes;
    size_t length;
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        errno = ENOTCONN;
        return -1;
    }
    if (gfx_layout(width, height, format, &bytes, &length) < 0) {
        return -1;
    }
    struct budo_gfx *gfx = calloc(1, sizeof(*gfx));
    if (!gfx) {
        return -1;
    }
    gfx->fd = gfx_socket();
    gfx->mapping = MAP_FAILED;
    gfx->length = length;
    gfx->frame_bytes = bytes;
    gfx->format = format;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    if (gfx->fd < 0 || connect(gfx->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return gfx_open_failed(gfx);
    }
    char temp[512];
    const char *temp_dir = getenv("TMPDIR");
    if (!temp_dir || !*temp_dir) {
        temp_dir = "/tmp";
    }
    int written = snprintf(temp, sizeof(temp), "%s/budogfx-frame-XXXXXX", temp_dir);
    if (written < 0 || (size_t)written >= sizeof(temp)) {
        errno = ENAMETOOLONG;
        return gfx_open_failed(gfx);
    }
    int shared_fd = mkstemp(temp);
    if (shared_fd < 0) {
        return gfx_open_failed(gfx);
    }
    unlink(temp);
    if (ftruncate(shared_fd, (off_t)length) < 0) {
        close(shared_fd);
        return gfx_open_failed(gfx);
    }
    gfx->mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
    if (gfx->mapping == MAP_FAILED) {
        close(shared_fd);
        return gfx_open_failed(gfx);
    }
    struct gfx_packet packet = gfx_packet(GFX_HELLO);
    packet.width = width;
    packet.height = height;
    packet.format = format;
    struct iovec iov = {&packet, sizeof(packet)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    memset(&control, 0, sizeof(control));
    struct msghdr message = {0};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &shared_fd, sizeof(int));
    ssize_t sent = sendmsg(gfx->fd, &message, MSG_NOSIGNAL);
    close(shared_fd);
    if (sent != (ssize_t)sizeof(packet) || gfx_wait_ack(gfx) < 0) {
        return gfx_open_failed(gfx);
    }
    *out = gfx;
    return 0;
}

int budo_gfx_set_keyboard_grab(struct budo_gfx *gfx, int enabled) {
    if (!gfx || (enabled != 0 && enabled != 1)) {
        errno = EINVAL;
        return -1;
    }
    gfx->keyboard_grab = enabled;
    return 0;
}

int budo_gfx_present(struct budo_gfx *gfx, const void *pixels,
                     const uint32_t palette[256]) {
    if (!gfx || !pixels || (gfx->format == BUDO_GFX_INDEX8 && !palette)) {
        errno = EINVAL;
        return -1;
    }
    uint8_t *slot = gfx->mapping + gfx->slot * (gfx->frame_bytes + 1024u);
    if (palette) {
        memcpy(slot, palette, 1024u);
    }
    memcpy(slot + 1024u, pixels, gfx->frame_bytes);
    struct gfx_packet packet = gfx_packet(GFX_FRAME);
    /* FRAME width was reserved/zero in v1; older hosts safely ignore it. */
    packet.width = (uint32_t)gfx->keyboard_grab;
    packet.slot = gfx->slot;
    if (gfx_send(gfx->fd, &packet) < 0 || gfx_wait_ack(gfx) < 0) {
        return -1;
    }
    gfx->slot ^= 1u;
    return 0;
}

int budo_gfx_poll_event(struct budo_gfx *gfx, struct budo_gfx_event *event) {
    if (!gfx || !event) {
        errno = EINVAL;
        return -1;
    }
    if (gfx->count == 0u) {
        struct gfx_packet packet;
        int result = gfx_client_receive(gfx, &packet);
        if (result <= 0) {
            return result;
        }
        if (packet.type != GFX_EVENT) {
            errno = EPROTO;
            return -1;
        }
    }
    *event = gfx->events[gfx->head];
    gfx->head = (gfx->head + 1u) % GFX_QUEUE;
    gfx->count--;
    return 1;
}

void budo_gfx_close(struct budo_gfx *gfx) {
    if (gfx) {
        if (gfx->mapping != MAP_FAILED) {
            munmap(gfx->mapping, gfx->length);
        }
        if (gfx->fd >= 0) {
            close(gfx->fd);
        }
        free(gfx);
    }
}

static void gfx_host_disconnect(struct budo_gfx_host *host) {
    if (host->fd >= 0) {
        close(host->fd);
    }
    if (host->mapping) {
        munmap(host->mapping, host->length);
    }
    free(host->rgba);
    host->fd = -1;
    host->mapping = NULL;
    host->rgba = NULL;
    host->keyboard_grab = 0;
    host->dirty = 1;
}

int budo_gfx_host_open(struct budo_gfx_host **out) {
    *out = NULL;
    struct budo_gfx_host *host = calloc(1, sizeof(*host));
    if (!host) {
        return -1;
    }
    host->fd = -1;
    host->listener = -1;
    const char *temp_dir = getenv("TMPDIR");
    if (!temp_dir || !*temp_dir) {
        temp_dir = "/tmp";
    }
    int written = snprintf(host->directory, sizeof(host->directory), "%s/budogfx-XXXXXX", temp_dir);
    if (written < 0 || (size_t)written >= sizeof(host->directory)) {
        free(host);
        errno = ENAMETOOLONG;
        return -1;
    }
    if (!mkdtemp(host->directory)) {
        free(host);
        return -1;
    }
    snprintf(host->path, sizeof(host->path), "%s/screen", host->directory);
    host->listener = gfx_socket();
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", host->path);
    if (host->listener < 0 ||
        fcntl(host->listener, F_SETFL, O_NONBLOCK) < 0 ||
        bind(host->listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(host->listener, 4) < 0) {
        int saved_errno = errno;
        budo_gfx_host_close(host);
        errno = saved_errno;
        return -1;
    }
    *out = host;
    return 0;
}

const char *budo_gfx_host_path(const struct budo_gfx_host *host) {
    return host->path;
}

void budo_gfx_host_poll(struct budo_gfx_host *host) {
    if (!host) {
        return;
    }
    /* An application may close and launch its successor before the next
     * UI poll. Release the old peer before accepting that new connection. */
    if (host->fd >= 0) {
        unsigned char byte;
        ssize_t pending = recv(host->fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (pending == 0 || (pending < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            gfx_host_disconnect(host);
        }
    }
    int fd = accept(host->listener, NULL, NULL);
    if (fd >= 0) {
        if (host->fd >= 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(fd);
        } else {
            host->fd = fd;
            host->handshake_started = gfx_milliseconds();
        }
    }
    if (host->fd >= 0 && !host->mapping &&
        gfx_milliseconds() - host->handshake_started >= 2000) {
        gfx_host_disconnect(host);
    }
    /* Bound transport work per UI iteration. */
    for (int i = 0; host->fd >= 0 && i < 32; i++) {
        struct gfx_packet packet;
        int shared_fd;
        int result = gfx_receive(host->fd, &packet, &shared_fd);
        if (result == 0) {
            break;
        }
        if (result < 0) {
            gfx_host_disconnect(host);
            break;
        }
        if (packet.type == GFX_HELLO && !host->mapping && shared_fd >= 0) {
            size_t bytes;
            size_t length;
            struct stat status;
            if (gfx_layout(packet.width, packet.height, packet.format, &bytes, &length) < 0 ||
                fstat(shared_fd, &status) < 0 || !S_ISREG(status.st_mode) ||
                status.st_size != (off_t)length) {
                close(shared_fd);
                gfx_host_disconnect(host);
                break;
            }
            void *mapping = mmap(NULL, length, PROT_READ, MAP_SHARED, shared_fd, 0);
            close(shared_fd);
            if (mapping == MAP_FAILED) {
                gfx_host_disconnect(host);
                break;
            }
            host->mapping = mapping;
            host->length = length;
            host->frame_bytes = bytes;
            host->width = packet.width;
            host->height = packet.height;
            host->format = packet.format;
            host->rgba = calloc((size_t)packet.width * packet.height, 4u);
            if (!host->rgba) {
                gfx_host_disconnect(host);
                break;
            }
            host->dirty = 1;
        } else if (packet.type == GFX_FRAME && host->mapping && packet.slot < 2u && shared_fd < 0) {
            host->keyboard_grab = packet.width == 1u;
            const uint8_t *slot = host->mapping + packet.slot * (host->frame_bytes + 1024u);
            const uint8_t *pixels = slot + 1024u;
            size_t count = (size_t)host->width * host->height;
            for (size_t p = 0; p < count; p++) {
                uint32_t color;
                if (host->format == BUDO_GFX_INDEX8) {
                    memcpy(&color, slot + (size_t)pixels[p] * 4u, 4u);
                } else {
                    memcpy(&color, pixels + p * 4u, 4u);
                }
                host->rgba[p * 4u] = (uint8_t)(color >> 16);
                host->rgba[p * 4u + 1u] = (uint8_t)(color >> 8);
                host->rgba[p * 4u + 2u] = (uint8_t)color;
                host->rgba[p * 4u + 3u] = 255u;
            }
            host->dirty = 1;
        } else {
            if (shared_fd >= 0) {
                close(shared_fd);
            }
            gfx_host_disconnect(host);
            break;
        }
        struct gfx_packet ack = gfx_packet(GFX_ACK);
        if (gfx_send(host->fd, &ack) < 0) {
            gfx_host_disconnect(host);
        }
    }
}

int budo_gfx_host_active(const struct budo_gfx_host *host) {
    return host && host->mapping != NULL;
}

int budo_gfx_host_keyboard_grab(const struct budo_gfx_host *host) {
    return budo_gfx_host_active(host) && host->keyboard_grab;
}

const uint8_t *budo_gfx_host_pixels(struct budo_gfx_host *host,
                                   int *width, int *height, int *dirty) {
    *width = (int)host->width;
    *height = (int)host->height;
    if (dirty) {
        *dirty = host->dirty;
        host->dirty = 0;
    }
    return host->rgba;
}

void budo_gfx_host_event(struct budo_gfx_host *host,
                         const struct budo_gfx_event *event) {
    if (budo_gfx_host_active(host)) {
        struct gfx_packet packet = gfx_packet(GFX_EVENT);
        packet.event = *event;
        if (gfx_send(host->fd, &packet) < 0) {
            gfx_host_disconnect(host);
        }
    }
}

void budo_gfx_host_close(struct budo_gfx_host *host) {
    if (host) {
        gfx_host_disconnect(host);
        if (host->listener >= 0) {
            close(host->listener);
        }
        unlink(host->path);
        rmdir(host->directory);
        free(host);
    }
}
