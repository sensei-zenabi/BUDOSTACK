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

static int control[2], connections, nested_connections;
static struct { char path[108]; int fd; } listeners[64];
static int listener_count;
static char outer_path[108];
static int launch_socket(int domain, int type, int protocol) {
    assert(domain == AF_UNIX && type == SOCK_SEQPACKET && protocol == 0);
    return dup(control[0]);
}
static int launch_bind(int fd, const struct sockaddr *address, socklen_t length) {
    (void)fd; (void)address; (void)length; return 0;
}
static int launch_listen(int fd, int backlog) { (void)fd; (void)backlog; return 0; }
static int launch_accept(int fd, struct sockaddr *address, socklen_t *length) {
    (void)address; (void)length;
    char data[109], ancillary[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {data, sizeof(data)}; struct msghdr message = {0};
    message.msg_iov = &iov; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    if (recvmsg(fd, &message, MSG_DONTWAIT) != (ssize_t)sizeof(data)) return -1;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    assert(header && header->cmsg_type == SCM_RIGHTS);
    int peer; memcpy(&peer, CMSG_DATA(header), sizeof(peer));
    if (data[0] == 'H') {
        assert(listener_count < 64);
        snprintf(listeners[listener_count].path, sizeof(listeners[listener_count].path), "%s", data + 1);
        listeners[listener_count++].fd = peer;
        errno = EAGAIN;
        return -1;
    }
    if (!strcmp(data + 1, outer_path)) { ++connections; return peer; }
    for (int i = 0; i < listener_count; ++i) {
        if (strcmp(listeners[i].path, data + 1)) continue;
        char ready = 1;
        struct iovec out_iov = {&ready, 1};
        struct msghdr out = {0};
        memset(ancillary, 0, sizeof(ancillary));
        out.msg_iov = &out_iov; out.msg_iovlen = 1;
        out.msg_control = ancillary; out.msg_controllen = sizeof(ancillary);
        header = CMSG_FIRSTHDR(&out);
        header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(header), &peer, sizeof(peer));
        assert(sendmsg(listeners[i].fd, &out, MSG_NOSIGNAL) == 1);
        close(peer);
        ++nested_connections;
        errno = EAGAIN;
        return -1;
    }
    assert(0 && "Unknown nested graphics endpoint");
    return -1;
}
#define socket launch_socket
#define bind launch_bind
#define listen launch_listen
#define accept launch_accept
#include "../lib/budo_gfx.c"
#undef socket
#undef bind
#undef listen
#undef accept
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control) == 0);
    struct budo_gfx_host *host = NULL;
    assert(budo_gfx_host_open(&host) == 0);
    assert(setenv("BUDOSTACK_GFX_SOCKET", budo_gfx_host_path(host), 1) == 0);
    snprintf(outer_path, sizeof(outer_path), "%s", budo_gfx_host_path(host));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        char fd[32]; snprintf(fd, sizeof(fd), "%d", control[1]);
        close(control[0]);
        assert(setenv("BUDO_LAUNCH_CONTROL_FD", fd, 1) == 0);
        assert(setenv("LD_PRELOAD", argv[2], 1) == 0);
        execl(argv[1], argv[1], (char *)NULL); _exit(127);
    }
    close(control[1]);
    int done = 0, status = 0;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            (void)budo_gfx_host_pixels(host, &w, &h, &dirty);
            assert(w == 640 && h == 480);
            /* Double-click both executables while the same desktop is alive. */
            int start = iteration >= 2500 ? 2500 : 500;
            int tick = iteration - start;
            if (tick == 0 || tick == 40 || tick == 100 || tick == 140) {
                struct budo_gfx_event event = {0};
                event.type = tick == 0 || tick == 100 ? BUDO_GFX_MOUSE_DOWN : BUDO_GFX_MOUSE_UP;
                event.button = 1;
                event.x = start == 500 ? 348 : 418;
                event.y = 111;
                budo_gfx_host_event(host, &event);
            }
            if (iteration == 5000) {
                struct budo_gfx_event event = {0}; event.type = BUDO_GFX_QUIT;
                budo_gfx_host_event(host, &event);
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) { done = 1; break; }
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    if (!done) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    assert(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && connections == 1 && nested_connections == 2);
    for (int i = 0; i < listener_count; ++i) close(listeners[i].fd);
    budo_gfx_host_close(host);
    puts("Explorer: example/rocket use separate hosted graphics endpoints while one desktop stays alive.");
    return 0;
}
