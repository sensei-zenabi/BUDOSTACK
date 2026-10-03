#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
/* Test-only inherited socketpair endpoint for the actual demo binaries. */
int socket(int domain, int type, int protocol) {
    const char *value = getenv("BUDO_TEST_FD");
    if (value && domain == AF_UNIX && type == SOCK_SEQPACKET && protocol == 0) {
        return dup(atoi(value));
    }
    errno = EAFNOSUPPORT;
    return -1;
}
int connect(int fd, const struct sockaddr *address, socklen_t length) {
    (void)fd;
    (void)address;
    (void)length;
    return 0;
}
