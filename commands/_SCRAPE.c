#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if BUDOSTACK_HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#define MAX_HOST_LEN 255
#define MAX_PORT_LEN 15

typedef struct {
    int use_tls;
    int socket_fd;
#if BUDOSTACK_HAVE_OPENSSL
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#endif
} ScrapeConnection;

static int parse_url(const char *url,
                     char *scheme,
                     size_t scheme_size,
                     char *host,
                     size_t host_size,
                     char *port,
                     size_t port_size,
                     const char **path_start) {
    const char *scheme_end = strstr(url, "://");
    const char *authority;
    const char *path;
    const char *port_sep;
    size_t scheme_len;
    size_t host_len;

    if (scheme_end == NULL) {
        fprintf(stderr, "_SCRAPE: URL must include scheme (e.g. http:// or https://)\n");
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

    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) {
        fprintf(stderr, "_SCRAPE: unsupported scheme '%s'\n", scheme);
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

    port_sep = memchr(authority, ':', (size_t)(path - authority));
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

        snprintf(port, port_size, "%s", strcmp(scheme, "https") == 0 ? "443" : "80");
    }

    *path_start = (*path == '\0') ? "/" : path;
    return 0;
}

static int open_socket_connection(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
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

#if BUDOSTACK_HAVE_OPENSSL
static int tls_setup(ScrapeConnection *conn, const char *host) {
    const SSL_METHOD *method;

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    method = TLS_client_method();
    conn->ssl_ctx = SSL_CTX_new(method);
    if (conn->ssl_ctx == NULL) {
        fprintf(stderr, "_SCRAPE: SSL_CTX_new failed\n");
        return -1;
    }

    conn->ssl = SSL_new(conn->ssl_ctx);
    if (conn->ssl == NULL) {
        fprintf(stderr, "_SCRAPE: SSL_new failed\n");
        return -1;
    }

    if (SSL_set_tlsext_host_name(conn->ssl, host) != 1) {
        fprintf(stderr, "_SCRAPE: failed to set TLS server name\n");
        return -1;
    }

    if (SSL_set_fd(conn->ssl, conn->socket_fd) != 1) {
        fprintf(stderr, "_SCRAPE: SSL_set_fd failed\n");
        return -1;
    }

    if (SSL_connect(conn->ssl) != 1) {
        fprintf(stderr, "_SCRAPE: SSL_connect failed\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }

    return 0;
}
#endif

static int conn_send(ScrapeConnection *conn, const char *buf, size_t len) {
    size_t sent_total = 0;
    while (sent_total < len) {
        int sent;
        if (conn->use_tls) {
#if BUDOSTACK_HAVE_OPENSSL
            sent = SSL_write(conn->ssl, buf + sent_total, (int)(len - sent_total));
#else
            sent = -1;
#endif
        } else {
            sent = (int)send(conn->socket_fd, buf + sent_total, len - sent_total, 0);
        }

        if (sent <= 0) {
            fprintf(stderr, "_SCRAPE: failed while sending request\n");
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

static ssize_t conn_recv(ScrapeConnection *conn, char *buf, size_t len) {
    if (conn->use_tls) {
#if BUDOSTACK_HAVE_OPENSSL
        return (ssize_t)SSL_read(conn->ssl, buf, (int)len);
#else
        return -1;
#endif
    }
    return recv(conn->socket_fd, buf, len, 0);
}

static void conn_cleanup(ScrapeConnection *conn) {
#if BUDOSTACK_HAVE_OPENSSL
    if (conn->ssl != NULL) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->ssl_ctx != NULL) {
        SSL_CTX_free(conn->ssl_ctx);
        conn->ssl_ctx = NULL;
    }
#endif
    if (conn->socket_fd != -1) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }
}

int main(int argc, char *argv[]) {
    char scheme[8];
    char host[MAX_HOST_LEN + 1];
    char port[MAX_PORT_LEN + 1];
    const char *path;
    char request[2048];
    int written;
    ScrapeConnection conn;
    int exit_code = EXIT_FAILURE;

    memset(&conn, 0, sizeof(conn));
    conn.socket_fd = -1;

    if (argc != 2) {
        fprintf(stderr, "Usage: _SCRAPE <URL>\n");
        return EXIT_FAILURE;
    }

    if (parse_url(argv[1], scheme, sizeof(scheme), host, sizeof(host), port, sizeof(port), &path) != 0) {
        return EXIT_FAILURE;
    }

    conn.use_tls = strcmp(scheme, "https") == 0;
#if !BUDOSTACK_HAVE_OPENSSL
    if (conn.use_tls) {
        fprintf(stderr, "_SCRAPE: HTTPS requested but OpenSSL is not available in this build\n");
        return EXIT_FAILURE;
    }
#endif

    conn.socket_fd = open_socket_connection(host, port);
    if (conn.socket_fd == -1) {
        goto cleanup;
    }

#if BUDOSTACK_HAVE_OPENSSL
    if (conn.use_tls && tls_setup(&conn, host) != 0) {
        goto cleanup;
    }
#endif

    written = snprintf(request,
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

    if (conn_send(&conn, request, (size_t)written) != 0) {
        goto cleanup;
    }

    for (;;) {
        char buffer[4096];
        ssize_t received = conn_recv(&conn, buffer, sizeof(buffer));
        if (received == 0) {
            break;
        }
        if (received < 0) {
            fprintf(stderr, "_SCRAPE: failed while reading response\n");
            goto cleanup;
        }
        if (fwrite(buffer, 1, (size_t)received, stdout) != (size_t)received) {
            perror("_SCRAPE: fwrite");
            goto cleanup;
        }
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    conn_cleanup(&conn);
    return exit_code;
}
