#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
/* Give the actual desktop and its successors fresh socketpair connections. */
int socket(int domain, int type, int protocol) {
    const char *control = getenv("BUDO_LAUNCH_CONTROL_FD");
    int pair[2];
    if (!control || domain != AF_UNIX || type != SOCK_SEQPACKET || protocol != 0 ||
        socketpair(domain, type, protocol, pair) != 0) { errno = EAFNOSUPPORT; return -1; }
    char data = 1, ancillary[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {&data, 1};
    struct msghdr message = {0};
    memset(ancillary, 0, sizeof(ancillary));
    message.msg_iov = &iov; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &pair[0], sizeof(int));
    ssize_t result = sendmsg(atoi(control), &message, MSG_NOSIGNAL);
    close(pair[0]);
    if (result != 1) { close(pair[1]); return -1; }
    return pair[1];
}
int connect(int fd, const struct sockaddr *address, socklen_t length) {
    (void)fd; (void)address; (void)length; return 0;
}
