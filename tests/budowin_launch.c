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

static int control[2], connections;
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
    char data, ancillary[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {&data, 1}; struct msghdr message = {0};
    message.msg_iov = &iov; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    if (recvmsg(fd, &message, MSG_DONTWAIT) != 1) return -1;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    assert(header && header->cmsg_type == SCM_RIGHTS);
    int peer; memcpy(&peer, CMSG_DATA(header), sizeof(peer));
    ++connections; return peer;
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
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        char fd[32]; snprintf(fd, sizeof(fd), "%d", control[1]);
        close(control[0]);
        assert(setenv("BUDO_LAUNCH_CONTROL_FD", fd, 1) == 0);
        assert(setenv("LD_PRELOAD", argv[2], 1) == 0);
        execl(argv[1], argv[1], (char *)NULL); _exit(127);
    }
    close(control[1]);
    int seen = 0, frames = 0, done = 0, status = 0;
    for (int iteration = 0; iteration < 15000; ++iteration) {
        budo_gfx_host_poll(host);
        if (seen != connections) { seen = connections; frames = 0; }
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            (void)budo_gfx_host_pixels(host, &w, &h, &dirty);
            if (dirty) {
                ++frames;
                if (seen == 1 || seen == 3 || seen == 5) {
                    assert(w == 640 && h == 480);
                    if (seen < 5 && (frames == 5 || frames == 7 || frames == 9 || frames == 11)) {
                        struct budo_gfx_event event = {0};
                        event.type = (frames == 5 || frames == 9) ? BUDO_GFX_MOUSE_DOWN : BUDO_GFX_MOUSE_UP;
                        event.button = 1; event.x = seen == 1 ? 348 : 418; event.y = 111; /* Content icons follow the folder pane and File menu. */
                        budo_gfx_host_event(host, &event);
                    }
                    if (seen == 5 && frames == 5) {
                        struct budo_gfx_event event = {0}; event.type = BUDO_GFX_QUIT;
                        budo_gfx_host_event(host, &event);
                    }
                } else { assert(w == 320 && h == 240); }
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) { done = 1; break; }
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    if (!done) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    assert(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && connections == 5);
    budo_gfx_host_close(host);
    puts("Explorer: double-click example/rocket, launch, graphics handoff and desktop resume passed.");
    return 0;
}
