#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HTTP_PORT "80"
#define MAX_HOST_LEN 255
#define MAX_PORT_LEN 15

static int parse_url(const char *url,
                     char *scheme,
                     size_t scheme_size,
                     char *host,
                     size_t host_size,
                     char *port,
                     size_t port_size,
                     const char **path_start) {
    const char *scheme_end = strstr(url, "://");
    const char *authority = NULL;
    const char *path = NULL;
    size_t scheme_len;
    size_t host_len;

    if (scheme_end == NULL) {
        fprintf(stderr, "_SCRAPE: URL must include scheme (e.g. http://)\n");
        return -1;
    }

    scheme_len = (size_t)(scheme_end - url);
    if (scheme_len == 0 || scheme_len >= scheme_size) {
        fprintf(stderr, "_SCRAPE: invalid URL scheme\n");
        return -1;
    }

    memcpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';

    for (size_t i = 0; i < scheme_len; ++i) {
        scheme[i] = (char)tolower((unsigned char)scheme[i]);
    }

    if (strcmp(scheme, "http") != 0) {
        fprintf(stderr, "_SCRAPE: unsupported scheme '%s' (only http is supported without TLS dependencies)\n", scheme);
        return -1;
    }

    authority = scheme_end + 3;
    path = strchr(authority, '/');
    if (path == NULL) {
        path = authority + strlen(authority);
    }

    if (authority == path) {
        fprintf(stderr, "_SCRAPE: missing host in URL\n");
        return -1;
    }

    const char *port_sep = memchr(authority, ':', (size_t)(path - authority));

    if (port_sep != NULL) {
        host_len = (size_t)(port_sep - authority);
        if (host_len == 0 || host_len >= host_size) {
            fprintf(stderr, "_SCRAPE: invalid host\n");
            return -1;
        }
        memcpy(host, authority, host_len);
        host[host_len] = '\0';

        size_t port_len = (size_t)(path - (port_sep + 1));
        if (port_len == 0 || port_len >= port_size) {
            fprintf(stderr, "_SCRAPE: invalid port\n");
            return -1;
        }
        memcpy(port, port_sep + 1, port_len);
        port[port_len] = '\0';
    } else {
        host_len = (size_t)(path - authority);
        if (host_len == 0 || host_len >= host_size) {
            fprintf(stderr, "_SCRAPE: invalid host\n");
            return -1;
        }
        memcpy(host, authority, host_len);
        host[host_len] = '\0';

        strncpy(port, DEFAULT_HTTP_PORT, port_size - 1);
        port[port_size - 1] = '\0';
    }

    *path_start = (*path == '\0') ? "/" : path;
    return 0;
}

static int open_connection(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int sockfd = -1;
    int gai_rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    gai_rc = getaddrinfo(host, port, &hints, &result);
    if (gai_rc != 0) {
        fprintf(stderr, "_SCRAPE: getaddrinfo failed: %s\n", gai_strerror(gai_rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd == -1) {
        fprintf(stderr, "_SCRAPE: failed to connect to %s:%s\n", host, port);
        return -1;
    }

    return sockfd;
}

int main(int argc, char *argv[]) {
    char scheme[8];
    char host[MAX_HOST_LEN + 1];
    char port[MAX_PORT_LEN + 1];
    const char *path = NULL;
    int sockfd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "Usage: _SCRAPE <URL>\n");
        return EXIT_FAILURE;
    }

    if (parse_url(argv[1], scheme, sizeof(scheme), host, sizeof(host), port, sizeof(port), &path) != 0) {
        return EXIT_FAILURE;
    }

    sockfd = open_connection(host, port);
    if (sockfd == -1) {
        return EXIT_FAILURE;
    }

    char request[2048];
    int written = snprintf(request,
                           sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: BUDOSTACK/_SCRAPE\r\n"
                           "Accept: */*\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           path,
                           host);

    if (written < 0 || (size_t)written >= sizeof(request)) {
        fprintf(stderr, "_SCRAPE: request too large\n");
        goto cleanup;
    }

    ssize_t total_sent = 0;
    ssize_t to_send = (ssize_t)written;
    while (total_sent < to_send) {
        ssize_t sent = send(sockfd, request + total_sent, (size_t)(to_send - total_sent), 0);
        if (sent <= 0) {
            perror("_SCRAPE: send");
            goto cleanup;
        }
        total_sent += sent;
    }

    char buffer[4096];
    for (;;) {
        ssize_t received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            perror("_SCRAPE: recv");
            goto cleanup;
        }

        size_t written_out = fwrite(buffer, 1, (size_t)received, stdout);
        if (written_out != (size_t)received) {
            perror("_SCRAPE: fwrite");
            goto cleanup;
        }
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if (sockfd != -1) {
        close(sockfd);
    }

    return exit_code;
}
