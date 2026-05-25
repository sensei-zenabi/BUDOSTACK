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

typedef struct {
    const char *url;
    int readable;
    const char *output_path;
} ScrapeOptions;

static void print_help(void) {
    printf("Usage: _SCRAPE <URL> [-readable] [-o <file>]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -readable  Convert HTML body to plain text (human-readable output).\n");
    printf("  -o <file>  Write output to file instead of stdout.\n");
}

static int parse_options(int argc, char *argv[], ScrapeOptions *opts) {
    opts->url = NULL;
    opts->readable = 0;
    opts->output_path = NULL;

    if (argc < 2) {
        print_help();
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-readable") == 0) {
            opts->readable = 1;
            continue;
        }

        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "_SCRAPE: missing file path for -o\n");
                return -1;
            }
            opts->output_path = argv[++i];
            continue;
        }

        if (argv[i][0] == '-') {
            fprintf(stderr, "_SCRAPE: unknown option '%s'\n", argv[i]);
            return -1;
        }

        if (opts->url != NULL) {
            fprintf(stderr, "_SCRAPE: multiple URLs provided\n");
            return -1;
        }
        opts->url = argv[i];
    }

    if (opts->url == NULL) {
        fprintf(stderr, "_SCRAPE: URL is required\n");
        return -1;
    }

    return 0;
}

static int parse_url(const char *url, char *scheme, size_t scheme_size, char *host,
                     size_t host_size, char *port, size_t port_size,
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

static int open_socket_connection(const char *host, const char *port) { /* unchanged */
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
        if (sockfd == -1) continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
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
    if (conn->ssl_ctx == NULL) return -1;
    conn->ssl = SSL_new(conn->ssl_ctx);
    if (conn->ssl == NULL) return -1;
    if (SSL_set_tlsext_host_name(conn->ssl, host) != 1) return -1;
    if (SSL_set_fd(conn->ssl, conn->socket_fd) != 1) return -1;
    if (SSL_connect(conn->ssl) != 1) return -1;
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
    if (conn->ssl != NULL) { SSL_shutdown(conn->ssl); SSL_free(conn->ssl); }
    if (conn->ssl_ctx != NULL) { SSL_CTX_free(conn->ssl_ctx); }
#endif
    if (conn->socket_fd != -1) close(conn->socket_fd);
}

static size_t html_to_text(const char *html, size_t len, char *out, size_t out_cap) {
    size_t i = 0;
    size_t j = 0;
    int in_tag = 0;
    int last_space = 1;

    while (i < len && j + 1 < out_cap) {
        char c = html[i++];

        if (in_tag) {
            if (c == '>') {
                in_tag = 0;
            }
            continue;
        }

        if (c == '<') {
            in_tag = 1;
            continue;
        }

        if (c == '&') {
            if (i + 3 < len && strncmp(&html[i - 1], "&amp;", 5) == 0) {
                c = '&';
                i += 4;
            } else if (i + 2 < len && strncmp(&html[i - 1], "&lt;", 4) == 0) {
                c = '<';
                i += 3;
            } else if (i + 2 < len && strncmp(&html[i - 1], "&gt;", 4) == 0) {
                c = '>';
                i += 3;
            } else if (i + 4 < len && strncmp(&html[i - 1], "&nbsp;", 6) == 0) {
                c = ' ';
                i += 5;
            }
        }

        if (isspace((unsigned char)c)) {
            if (!last_space) {
                out[j++] = ' ';
                last_space = 1;
            }
            continue;
        }

        out[j++] = c;
        last_space = 0;
    }

    out[j] = '\0';
    return j;
}

int main(int argc, char *argv[]) {
    ScrapeOptions opts;
    char scheme[8], host[MAX_HOST_LEN + 1], port[MAX_PORT_LEN + 1], request[2048], buffer[4096];
    const char *path;
    ScrapeConnection conn;
    FILE *output = stdout;
    char *response = NULL;
    size_t response_len = 0;
    size_t response_cap = 0;
    int exit_code = EXIT_FAILURE;

    memset(&conn, 0, sizeof(conn));
    conn.socket_fd = -1;

    if (parse_options(argc, argv, &opts) != 0) return EXIT_FAILURE;
    if (parse_url(opts.url, scheme, sizeof(scheme), host, sizeof(host), port, sizeof(port), &path) != 0) return EXIT_FAILURE;

    conn.use_tls = strcmp(scheme, "https") == 0;
#if !BUDOSTACK_HAVE_OPENSSL
    if (conn.use_tls) {
        fprintf(stderr, "_SCRAPE: HTTPS requested but OpenSSL is not available in this build\n");
        return EXIT_FAILURE;
    }
#endif

    if (opts.output_path != NULL) {
        output = fopen(opts.output_path, "wb");
        if (output == NULL) {
            perror("_SCRAPE: fopen");
            return EXIT_FAILURE;
        }
    }

    conn.socket_fd = open_socket_connection(host, port);
    if (conn.socket_fd == -1) goto cleanup;
#if BUDOSTACK_HAVE_OPENSSL
    if (conn.use_tls && tls_setup(&conn, host) != 0) goto cleanup;
#endif

    int written = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: BUDOSTACK/_SCRAPE\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                           path, host);
    if (written < 0 || (size_t)written >= sizeof(request)) goto cleanup;
    if (conn_send(&conn, request, (size_t)written) != 0) goto cleanup;

    while (1) {
        ssize_t n = conn_recv(&conn, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) goto cleanup;

        if (!opts.readable) {
            if (fwrite(buffer, 1, (size_t)n, output) != (size_t)n) goto cleanup;
        } else {
            if (response_len + (size_t)n + 1 > response_cap) {
                size_t new_cap = response_cap == 0 ? 8192 : response_cap * 2;
                while (new_cap < response_len + (size_t)n + 1) new_cap *= 2;
                char *tmp = realloc(response, new_cap);
                if (tmp == NULL) goto cleanup;
                response = tmp;
                response_cap = new_cap;
            }
            memcpy(response + response_len, buffer, (size_t)n);
            response_len += (size_t)n;
            response[response_len] = '\0';
        }
    }

    if (opts.readable) {
        const char *body = response;
        char *sep = strstr(response, "\r\n\r\n");
        if (sep != NULL) body = sep + 4;
        char *text = malloc(strlen(body) + 1);
        if (text == NULL) goto cleanup;
        html_to_text(body, strlen(body), text, strlen(body) + 1);
        if (fputs(text, output) == EOF) {
            free(text);
            goto cleanup;
        }
        fputc('\n', output);
        free(text);
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    free(response);
    conn_cleanup(&conn);
    if (output != stdout) fclose(output);
    return exit_code;
}
