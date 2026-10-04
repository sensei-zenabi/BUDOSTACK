#define _POSIX_C_SOURCE 200809L
#include "../lib/budo_gfx.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* This sandbox permits socketpair but not socket(AF_UNIX). Replace only
 * endpoint establishment; packets, ancillary FD transfer, poll, mmap and
 * disconnect handling run through the production implementation unchanged.
 */
static int endpoints[2];
static int socket_calls;
static int accepted;

static int test_socketpair_mode(void) {
    const char *value = getenv("BUDO_GFX_TEST_SOCKETPAIR");
    return value && strcmp(value, "1") == 0;
}

static int test_socket(int domain, int type, int protocol) {
    assert(domain == AF_UNIX && type == SOCK_SEQPACKET && protocol == 0);
    if (!test_socketpair_mode()) {
        return socket(domain, type, protocol);
    }
    return dup(endpoints[socket_calls++ == 0 ? 0 : 1]);
}
static int test_bind(int fd, const struct sockaddr *address, socklen_t length) {
    return test_socketpair_mode() ? 0 : bind(fd, address, length);
}
static int test_listen(int fd, int backlog) {
    return test_socketpair_mode() ? 0 : listen(fd, backlog);
}
static int test_connect(int fd, const struct sockaddr *address, socklen_t length) {
    return test_socketpair_mode() ? 0 : connect(fd, address, length);
}
static int test_accept(int fd, struct sockaddr *address, socklen_t *length) {
    if (!test_socketpair_mode()) {
        return accept(fd, address, length);
    }
    if (accepted++) {
        errno = EAGAIN;
        return -1;
    }
    return dup(fd);
}
#define socket test_socket
#define bind test_bind
#define listen test_listen
#define connect test_connect
#define accept test_accept
#include "../lib/budo_gfx.c"
#undef socket
#undef bind
#undef listen
#undef connect
#undef accept

static void test_pause(void) {
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}

static struct budo_gfx_host *test_host(void) {
    socket_calls = 0;
    accepted = 0;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, endpoints) == 0);
    struct budo_gfx_host *host = NULL;
    assert(budo_gfx_host_open(&host) == 0);
    assert(setenv("BUDOSTACK_GFX_SOCKET", budo_gfx_host_path(host), 1) == 0);
    return host;
}

static void test_frames(unsigned int format) {
    struct budo_gfx_host *host = test_host();
    const int width = format == BUDO_GFX_INDEX8 ? 320 : 640;
    const int height = format == BUDO_GFX_INDEX8 ? 200 : 360;
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(endpoints[0]);
        close(host->listener);
        struct budo_gfx *gfx = NULL;
        assert(budo_gfx_open(&gfx, (unsigned int)width, (unsigned int)height, format) == 0);
        size_t count = (size_t)width * height;
        uint32_t *argb = calloc(count, sizeof(*argb));
        uint8_t *indexed = calloc(count, 1u);
        assert(argb && indexed);
        uint32_t palette[256] = {0};
        for (uint32_t frame = 1; frame <= 40; frame++) {
            for (size_t p = 0; p < count; p++) {
                argb[p] = 0xaa120000u | (frame << 8) | (uint32_t)(p % 256u);
                indexed[p] = (uint8_t)(p % 256u);
            }
            for (uint32_t i = 0; i < 256; i++) {
                palette[i] = 0xff120000u | (frame << 8) | i;
            }
            assert(budo_gfx_present(gfx, format == BUDO_GFX_INDEX8 ? (void *)indexed : (void *)argb,
                                    format == BUDO_GFX_INDEX8 ? palette : NULL) == 0);
        }
        struct budo_gfx_event event;
        for (uint32_t type = BUDO_GFX_KEY_DOWN; type <= BUDO_GFX_KEY_UP; type++) {
            int result;
            do {
                result = budo_gfx_poll_event(gfx, &event);
                assert(result >= 0);
                test_pause();
            } while (result == 0);
            assert(event.type == type && event.key == 32 && event.scancode == 44);
        }
        budo_gfx_close(gfx);
        free(argb);
        free(indexed);
        close(endpoints[1]);
        _exit(0);
    }
    close(endpoints[0]);
    close(endpoints[1]);
    int frames = 0;
    int sent_events = 0;
    int status = 0;
    int saw_active = 0;
    for (int iteration = 0; iteration < 10000; iteration++) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            saw_active = 1;
            int w;
            int h;
            int dirty;
            const uint8_t *pixels = budo_gfx_host_pixels(host, &w, &h, &dirty);
            assert(w == width && h == height);
            if (dirty && pixels[0] == 0x12u) {
                uint8_t frame = pixels[1];
                for (size_t p = 0; p < (size_t)w * h; p++) {
                    assert(pixels[p * 4u] == 0x12u);
                    assert(pixels[p * 4u + 1u] == frame);
                    assert(pixels[p * 4u + 2u] == p % 256u);
                    assert(pixels[p * 4u + 3u] == 255u);
                }
                frames++;
                if (frame == 40 && !sent_events) {
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_KEY_DOWN;
                    event.key = 32;
                    event.scancode = 44;
                    budo_gfx_host_event(host, &event);
                    event.type = BUDO_GFX_KEY_UP;
                    budo_gfx_host_event(host, &event);
                    sent_events = 1;
                }
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) {
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            break;
        }
        test_pause();
        assert(iteration < 9999);
    }
    budo_gfx_host_poll(host);
    assert(saw_active && frames > 0 && sent_events);
    assert(!budo_gfx_host_active(host));
    budo_gfx_host_close(host);
}

static void test_bad_packet(void) {
    struct budo_gfx_host *host = test_host();
    struct gfx_packet packet = gfx_packet(GFX_HELLO);
    packet.version++;
    int fd = endpoints[1];
    if (!test_socketpair_mode()) {
        fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        assert(fd >= 0);
        struct sockaddr_un address = {0};
        address.sun_family = AF_UNIX;
        snprintf(address.sun_path, sizeof(address.sun_path), "%s", budo_gfx_host_path(host));
        assert(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    }
    assert(send(fd, &packet, sizeof(packet), 0) == (ssize_t)sizeof(packet));
    if (fd != endpoints[1]) {
        close(fd);
    }
    budo_gfx_host_poll(host);
    assert(host->fd == -1 && !budo_gfx_host_active(host));
    close(endpoints[0]);
    close(endpoints[1]);
    budo_gfx_host_close(host);
}

static void test_crash(void) {
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(endpoints[0]);
        close(host->listener);
        struct budo_gfx *gfx = NULL;
        assert(budo_gfx_open(&gfx, 320, 200, BUDO_GFX_INDEX8) == 0);
        for (;;) {
            test_pause();
        }
    }
    close(endpoints[0]);
    close(endpoints[1]);
    for (int i = 0; !budo_gfx_host_active(host); i++) {
        assert(i < 2000);
        budo_gfx_host_poll(host);
        test_pause();
    }
    assert(kill(child, SIGKILL) == 0);
    assert(waitpid(child, NULL, 0) == child);
    budo_gfx_host_poll(host);
    assert(!budo_gfx_host_active(host));
    budo_gfx_host_close(host);
}

static void test_motion_queue(void) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
    struct budo_gfx client = {0};
    client.fd = pair[1];
    struct gfx_packet packet = gfx_packet(GFX_EVENT), received;
    packet.event.type = BUDO_GFX_MOUSE_MOVE;
    for (int i = 0; i < 1000; ++i) {
        packet.event.x = i;
        assert(gfx_send(pair[0], &packet) == 0);
        assert(gfx_client_receive(&client, &received) == 1);
    }
    assert(client.count == 2 && client.events[0].x == 0 && client.events[1].x == 999);
    packet.event.x = 998;
    assert(gfx_send(pair[0], &packet) == 0);
    assert(gfx_client_receive(&client, &received) == 1);
    assert(client.count == 3 && client.events[1].x == 999 && client.events[2].x == 998);
    client.head = client.count = 0;
    packet.event.x = 999;
    assert(gfx_send(pair[0], &packet) == 0);
    assert(gfx_client_receive(&client, &received) == 1);
    packet.event.type = BUDO_GFX_MOUSE_DOWN;
    assert(gfx_send(pair[0], &packet) == 0);
    assert(gfx_client_receive(&client, &received) == 1);
    packet.event.type = BUDO_GFX_MOUSE_MOVE;
    for (int i = 0; i < 1000; ++i) {
        packet.event.x = i + 1000;
        assert(gfx_send(pair[0], &packet) == 0);
        assert(gfx_client_receive(&client, &received) == 1);
    }
    packet.event.type = BUDO_GFX_MOUSE_UP;
    assert(gfx_send(pair[0], &packet) == 0);
    assert(gfx_client_receive(&client, &received) == 1);
    assert(client.count == 5);
    assert(client.events[0].x == 999);
    assert(client.events[1].type == BUDO_GFX_MOUSE_DOWN);
    assert(client.events[2].x == 1000 && client.events[3].x == 1999);
    assert(client.events[4].type == BUDO_GFX_MOUSE_UP);
    close(pair[0]); close(pair[1]);
}

int main(void) {
    test_motion_queue();
    size_t bytes;
    size_t length;
    assert(gfx_layout(0, 200, BUDO_GFX_INDEX8, &bytes, &length) < 0);
    assert(gfx_layout(1921, 200, BUDO_GFX_INDEX8, &bytes, &length) < 0);
    assert(gfx_layout(320, 200, 99, &bytes, &length) < 0);
    assert(gfx_layout(320, 200, BUDO_GFX_INDEX8, &bytes, &length) == 0);
    assert(bytes == 64000u && length == 130048u);
    unsetenv("BUDOSTACK_GFX_SOCKET");
    struct budo_gfx *gfx = NULL;
    assert(budo_gfx_open(&gfx, 320, 200, BUDO_GFX_INDEX8) < 0 && errno == ENOTCONN);
    test_frames(BUDO_GFX_ARGB8888);
    test_frames(BUDO_GFX_INDEX8);
    test_bad_packet();
    test_crash();
    puts("Graphics transport: ARGB/INDEX8 frames, palette, input, validation, exit and crash passed.");
    return 0;
}
