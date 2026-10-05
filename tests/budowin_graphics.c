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


int main(int argc, char **argv) {
    assert(argc == 4);
    assert(setenv("BUDO_GFX_TEST_SOCKETPAIR", "1", 1) == 0);
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        char fd[32];
        snprintf(fd, sizeof(fd), "%d", endpoints[1]);
        close(endpoints[0]);
        assert(setenv("BUDO_TEST_FD", fd, 1) == 0);
        assert(setenv("LD_PRELOAD", argv[2], 1) == 0);
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    close(endpoints[1]);
    unsigned char bottom_right[3] = {0};
    int frames = 0, status = 0, done = 0;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            const uint8_t *pixels = budo_gfx_host_pixels(host, &w, &h, &dirty);
            assert(w == 640 && h == 480);
            if (dirty) {
                ++frames;
                if (frames == 5) {
                    memcpy(bottom_right, pixels + (480u * 640u - 1u) * 4u, 3);
                    FILE *file = fopen(argv[3], "wb");
                    assert(file);
                    fputs("P6\n640 480\n255\n", file);
                    for (int p = 0; p < w * h; ++p) assert(fwrite(pixels + p * 4, 1, 3, file) == 3);
                    assert(fclose(file) == 0);
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_MOUSE_MOVE;
                    for (int move = 0; move < 100; ++move) {
                        event.x = 401 + move; event.y = 300;
                        budo_gfx_host_event(host, &event);
                    }
                }
                if (frames == 9) {
                    /* Latest position must win before all stale samples replay. */
                    size_t pixel = (301u * 640u + 500u) * 4u;
                    assert(pixels[pixel + 1u] == 0 && pixels[pixel + 2u] == 0);
                }
                if (frames == 9 || frames == 12 || frames == 15 || frames == 18) {
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_MOUSE_MOVE;
                    event.x = (frames == 9 || frames == 15) ? -10000 : 10000;
                    event.y = (frames == 9 || frames == 12) ? -10000 : 10000;
                    budo_gfx_host_event(host, &event);
                }
                if (frames == 12 || frames == 15 || frames == 18 || frames == 21) {
                    int x = (frames == 12 || frames == 18) ? 0 : 639;
                    int y = (frames == 12 || frames == 15) ? 0 : 479;
                    /* The cursor's top-left pixel is transparent. Check its
                     * opaque black/grey neighbor, or the exact background
                     * when the bottom-right corner clips every opaque pixel. */
                    if (y < 479) {
                        size_t pixel = ((size_t)(y + 1) * 640u + (size_t)x) * 4u;
                        assert(pixels[pixel] == 0 && pixels[pixel + 1u] == 0 && pixels[pixel + 2u] == 0);
                    } else if (x == 0) {
                        size_t pixel = (479u * 640u + 1u) * 4u;
                        assert(pixels[pixel] == 128 && pixels[pixel + 1u] == 128 && pixels[pixel + 2u] == 128);
                    } else {
                        assert(memcmp(pixels + (480u * 640u - 1u) * 4u, bottom_right, 3) == 0);
                    }
                }
                if (frames == 22 || frames == 31) {
                    struct budo_gfx_event event = {0}; event.type = BUDO_GFX_RESET;
                    budo_gfx_host_event(host, &event);
                    if (frames == 22) {
                        event.type = BUDO_GFX_MOUSE_MOVE; event.x = 0; event.y = 200;
                        budo_gfx_host_event(host, &event);
                    }
                }
                if (frames == 24) {
                    struct budo_gfx_event event = {0}; event.type = BUDO_GFX_MOUSE_MOVE;
                    event.y = 200;
                    event.x = -1000; budo_gfx_host_event(host, &event);
                    event.x = -999; budo_gfx_host_event(host, &event);
                }
                if (frames == 30) {
                    size_t pixel = (201u * 640u + 1u) * 4u;
                    assert(pixels[pixel] == 0 && pixels[pixel + 1u] == 0 && pixels[pixel + 2u] == 0);
                }
                if (frames == 36 || frames == 38 || frames == 40 || frames == 42) {
                    struct budo_gfx_event event = {0};
                    event.type = (frames == 36 || frames == 40) ? BUDO_GFX_MOUSE_DOWN : BUDO_GFX_MOUSE_UP;
                    event.button = 1; event.x = 350; event.y = 56;
                    budo_gfx_host_event(host, &event);
                }
                if (frames == 50) {
                    char path[4096];
                    assert(snprintf(path, sizeof(path), "%s.paint.ppm", argv[3]) < (int)sizeof(path));
                    FILE *file = fopen(path, "wb"); assert(file);
                    fputs("P6\n640 480\n255\n", file);
                    for (int p = 0; p < w * h; ++p) assert(fwrite(pixels + p * 4, 1, 3, file) == 3);
                    assert(fclose(file) == 0);
                    assert(pixels[(200 * w + 200) * 4] != 0);
                }
                if (frames == 52) {
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_MOUSE_DOWN;
                    event.button = 1;
                    event.x = 606;
                    event.y = 32;
                    budo_gfx_host_event(host, &event);
                }
                if (frames == 54) {
                    /* Pressing Close must leave Paint open until release. */
                    assert(pixels[(200 * w + 200) * 4] != 0);
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_MOUSE_UP;
                    event.button = 1;
                    event.x = 0;
                    event.y = 200;
                    budo_gfx_host_event(host, &event);
                }
                if (frames == 56) {
                    /* Releasing outside cancels; Escape can still close it. */
                    assert(pixels[(200 * w + 200) * 4] != 0);
                    struct budo_gfx_event event = {0};
                    event.type = BUDO_GFX_KEY_DOWN; event.key = 27; event.scancode = 41;
                    budo_gfx_host_event(host, &event);
                }
                if (frames == 59) {
                    struct budo_gfx_event event = {0}; event.type = BUDO_GFX_QUIT;
                    budo_gfx_host_event(host, &event);
                }
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) { done = 1; break; }
        test_pause();
    }
    if (!done) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    assert(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && frames >= 10);
    budo_gfx_host_poll(host);
    assert(!budo_gfx_host_active(host));
    budo_gfx_host_close(host);
    puts("BUDOWIN: 640x480 palette frames, mouse input, Escape and graphics cleanup passed.");
    return 0;
}
