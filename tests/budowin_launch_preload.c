#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Broker nested graphics listeners as well as clients through socketpairs.
 * Packet transport, FD transfer and rendering still use production code. */
static int peer_for_fd[1024];
static int announce(int fd, char kind, const struct sockaddr *address)
{
    const char *control = getenv("BUDO_LAUNCH_CONTROL_FD");
    if (!control || fd < 0 || fd >= 1024 || peer_for_fd[fd] <= 0) { errno = EINVAL; return -1; }
    char data[109] = {0}, ancillary[CMSG_SPACE(sizeof(int))];
    data[0] = kind;
    snprintf(data + 1, sizeof(data) - 1, "%s", ((const struct sockaddr_un *)address)->sun_path);
    struct iovec iov = {data, sizeof(data)};
    struct msghdr message = {0};
    memset(ancillary, 0, sizeof(ancillary));
    message.msg_iov = &iov; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &peer_for_fd[fd], sizeof(int));
    ssize_t result = sendmsg(atoi(control), &message, MSG_NOSIGNAL);
    close(peer_for_fd[fd]);
    peer_for_fd[fd] = 0;
    return result == (ssize_t)sizeof(data) ? 0 : -1;
}
int socket(int domain, int type, int protocol)
{
    int pair[2];
    if (domain != AF_UNIX || type != SOCK_SEQPACKET || protocol != 0 ||
        socketpair(domain, type, protocol, pair) != 0) { errno = EAFNOSUPPORT; return -1; }
    if (pair[0] >= 1024) { close(pair[0]); close(pair[1]); errno = EMFILE; return -1; }
    peer_for_fd[pair[0]] = pair[1];
    return pair[0];
}
int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)length;
    return announce(fd, 'H', address);
}
int listen(int fd, int backlog)
{
    (void)fd; (void)backlog;
    return 0;
}
int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)length;
    return announce(fd, 'C', address);
}
int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    (void)address; (void)length;
    char data, ancillary[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {&data, 1};
    struct msghdr message = {0};
    message.msg_iov = &iov; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    if (recvmsg(fd, &message, MSG_DONTWAIT) != 1) return -1;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_type != SCM_RIGHTS) { errno = EPROTO; return -1; }
    int peer;
    memcpy(&peer, CMSG_DATA(header), sizeof(peer));
    return peer;
}
