/*
    server.c

    A "switchboard" server that:
    - Listens on TCP port 12345 by default.
    - Accepts multiple clients (up to MAX_CLIENTS).
    - Each client is assumed to have 5 outputs (out0..out4) and 5 inputs (in0..in4).
    - Maintains a routing table so that outX of clientA can be connected to inY of clientB.
    - Provides a simple text-based console UI to:
        * list clients
        * list routes
        * route <A> outX <B> inY
        * help

    The server forwards any line received from a client's "outN:" prefix
    to the client/ channel that is wired in the routing table.

    This sample is purely textual, used to illustrate multi-channel
    routing. For real audio bridging, you'd send audio frames rather
    than lines of text.

    Build:
      gcc -std=c11 -o server server.c
    Run:
      ./server  (optionally specify a port)
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

#define SERVER_PORT        12345
#define MAX_CLIENTS        20
#define CHANNELS_PER_APP   5    // 5 outputs, 5 inputs

typedef struct {
    int sockfd;
    int client_id;
    int active;
    char name[64]; // optional label
} ClientInfo;

// Routing table: route[outClientID][outChannel] -> (inClientID, inChannel)
typedef struct {
    int in_client_id;  // -1 if none
    int in_channel;    // 0..4
} Route;

// We map by [client_id up to MAX_CLIENTS+1][channels], so keep an array big enough:
static Route routing[MAX_CLIENTS + 1][CHANNELS_PER_APP];

// All possible clients, stored in a simple array
static ClientInfo clients[MAX_CLIENTS];
static int next_client_id = 1;  // ID to assign to the next connecting client

// Forward declarations
static void handle_new_connection(int server_fd);
static void handle_client_input(int idx);
static void handle_console_input(void);
static void show_help(void);
static void route_command(int outCID, int outCH, int inCID, int inCH);
static void list_clients(void);
static void list_routes(void);

static int find_client_index(int cid);
static void trim_newline(char *s);

int main(int argc, char *argv[]) {
    unsigned short port = SERVER_PORT;
    if (argc > 1) {
        unsigned short temp = (unsigned short)atoi(argv[1]);
        if (temp > 0) {
            port = temp;
        }
    }

    // Initialize
    memset(clients, 0, sizeof(clients));
    memset(routing, -1, sizeof(routing));
    // route[*][*].in_client_id = -1 means no route

    // Create listening socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Switchboard Server listening on port %hu.\n", port);
    printf("Type 'help' for commands.\n");

    // Main loop with select()
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;

        // Add all active clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].sockfd, &readfds);
                if (clients[i].sockfd > maxfd) {
                    maxfd = clients[i].sockfd;
                }
            }
        }

        // Add stdin for console commands
        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > maxfd) {
            maxfd = STDIN_FILENO;
        }

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        // New connection?
        if (FD_ISSET(server_fd, &readfds)) {
            handle_new_connection(server_fd);
        }

        // Check each client
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].sockfd, &readfds)) {
                handle_client_input(i);
            }
        }

        // Check console input
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            handle_console_input();
        }
    }

    close(server_fd);
    return 0;
}

// Accept new client
static void handle_new_connection(int server_fd) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int client_sock = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (client_sock < 0) {
        perror("accept");
        return;
    }

    // Find free slot
    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        const char *msg = "Server full.\n";
        write(client_sock, msg, strlen(msg));
        close(client_sock);
        return;
    }

    int cid = next_client_id++;
    clients[idx].sockfd    = client_sock;
    clients[idx].client_id = cid;
    clients[idx].active    = 1;
    snprintf(clients[idx].name, sizeof(clients[idx].name),
             "Client%d", cid);

    char greet[128];
    snprintf(greet, sizeof(greet),
             "Welcome to Switchboard. You are client_id=%d, with 5 in / 5 out.\n",
             cid);
    write(client_sock, greet, strlen(greet));

    printf("Client %d connected (slot=%d).\n", cid, idx);
}

// Handle data from client in slot i
static void handle_client_input(int i) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    ssize_t n = recv(clients[i].sockfd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        // Client disconnected or error
        printf("Client %d disconnected.\n", clients[i].client_id);
        close(clients[i].sockfd);
        clients[i].active = 0;
        return;
    }
    // Possibly multiple lines in buf, handle each
    // Look for "outX:" prefix
    char *start = buf;
    while (1) {
        char *nl = strchr(start, '\n');
        if (!nl) break; // no more lines
        *nl = '\0';

        // Trim potential CR
        char *cr = strchr(start, '\r');
        if (cr) *cr = '\0';

        trim_newline(start);

        // Parse e.g. "out0: some text"
        // Identify which out channel
        int out_ch = -1;
        if (strncmp(start, "out", 3) == 0 && isdigit((unsigned char)start[3])) {
            out_ch = start[3] - '0'; // '0'..'4'
        }
        if (out_ch >= 0 && out_ch < CHANNELS_PER_APP) {
            // Skip past "outX:"
            char *colon = strchr(start, ':');
            const char *msg = "";
            if (colon) {
                msg = colon + 1;
                while (*msg == ' ' || *msg == '\t') { msg++; }
            }
            // We know the out channel
            int out_cid = clients[i].client_id; // which client is sending
            Route r = routing[out_cid][out_ch];
            // r.in_client_id might be -1 => not connected
            if (r.in_client_id >= 0) {
                // find the actual slot for in_client_id
                int idx_in = find_client_index(r.in_client_id);
                if (idx_in >= 0 && clients[idx_in].active) {
                    // forward
                    char outbuf[600];
                    snprintf(outbuf, sizeof(outbuf),
                             "in%d from client%d: %s\n",
                             r.in_channel,
                             out_cid,
                             msg);
                    send(clients[idx_in].sockfd, outbuf, strlen(outbuf), 0);
                }
            }
        }
        start = nl + 1;
    }
}

// Handle server console input
static void handle_console_input(void) {
    char cmdline[256];
    if (!fgets(cmdline, sizeof(cmdline), stdin)) {
        return;
    }
    trim_newline(cmdline);

    if (strcmp(cmdline, "") == 0) {
        return;
    }
    if (strcmp(cmdline, "help") == 0) {
        show_help();
        return;
    }
    if (strcmp(cmdline, "list") == 0) {
        list_clients();
        return;
    }
    if (strcmp(cmdline, "routes") == 0) {
        list_routes();
        return;
    }

    // route <outCID> outN <inCID> inM
    // e.g. "route 1 out0 2 in0"
    if (strncmp(cmdline, "route", 5) == 0) {
        char *tok = strtok(cmdline, " ");
        if (!tok) return; // skip 'route'
        char *pOutCID = strtok(NULL, " ");
        char *pOutStr = strtok(NULL, " ");
        char *pInCID  = strtok(NULL, " ");
        char *pInStr  = strtok(NULL, " ");
        if (!pOutCID || !pOutStr || !pInCID || !pInStr) {
            printf("Usage: route <outCID> outN <inCID> inM\n");
            return;
        }
        int outCID = atoi(pOutCID);
        int outCH  = -1;
        if (strncmp(pOutStr, "out", 3) == 0 && isdigit((unsigned char)pOutStr[3])) {
            outCH = pOutStr[3] - '0';
        }
        int inCID = atoi(pInCID);
        int inCH  = -1;
        if (strncmp(pInStr, "in", 2) == 0 && isdigit((unsigned char)pInStr[2])) {
            inCH = pInStr[2] - '0';
        }
        if (outCH < 0 || outCH >= CHANNELS_PER_APP ||
            inCH < 0  || inCH  >= CHANNELS_PER_APP) {
            printf("Invalid channel. Must be 0..4\n");
            return;
        }
        route_command(outCID, outCH, inCID, inCH);
        return;
    }

    printf("Unknown command: %s\n", cmdline);
}

static void show_help(void) {
    printf("Commands:\n");
    printf("  help      - show this help\n");
    printf("  list      - list connected clients\n");
    printf("  routes    - list routing table\n");
    printf("  route X outN Y inM  - connect clientX outN -> clientY inM\n");
    printf("\n");
}

static void route_command(int outCID, int outCH, int inCID, int inCH) {
    // find them in clients
    int idxO = find_client_index(outCID);
    if (idxO < 0) {
        printf("No such client %d\n", outCID);
        return;
    }
    int idxI = find_client_index(inCID);
    if (idxI < 0) {
        printf("No such client %d\n", inCID);
        return;
    }

    routing[outCID][outCH].in_client_id = inCID;
    routing[outCID][outCH].in_channel   = inCH;

    printf("Routed client%d out%d -> client%d in%d\n",
           outCID, outCH, inCID, inCH);
}

static void list_clients(void) {
    printf("Active clients:\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            printf("  clientID=%d sockfd=%d name=%s\n",
                   clients[i].client_id,
                   clients[i].sockfd,
                   clients[i].name);
        }
    }
}

static void list_routes(void) {
    printf("Routes:\n");
    // We track only up to next_client_id - 1
    for (int cid = 1; cid < next_client_id; cid++) {
        // check if client is active
        int idx = find_client_index(cid);
        if (idx < 0) continue; // not connected
        for (int c = 0; c < CHANNELS_PER_APP; c++) {
            int inCID = routing[cid][c].in_client_id;
            if (inCID >= 0) {
                int inCH = routing[cid][c].in_channel;
                printf("  client%d.out%d -> client%d.in%d\n",
                       cid, c, inCID, inCH);
            }
        }
    }
}

static int find_client_index(int cid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].client_id == cid) {
            return i;
        }
    }
    return -1;
}

static void trim_newline(char *s) {
    // remove trailing newline or CR
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
    p = strchr(s, '\r');
    if (p) *p = '\0';
}
