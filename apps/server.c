/*
 server.c

 A "switchboard" server that:
 - Listens on TCP port 12345 by default for local client connections.
 - Uses a UDP socket bound to BROADCAST_PORT (12346) for all node-to-node communication.
 - Accepts multiple local clients (up to MAX_CLIENTS), each with 5 outputs (out0..out4) and 5 inputs (in0..in4).
 - Maintains a routing table so that outX of one client can be connected to inY of another.
 - Provides a simple text-based console UI with the following commands:
    * help       - show available commands
    * list       - list local clients and connected remote nodes (with their reported clients)
    * routes     - list local routing table
    * route <outCID> <outCH|all> <inCID> <inCH|all>
                 - establish a route (for remote endpoints, use the format "IP:clientID")
    * print <clientID>
                 - display channel data for a client (for remote, use "IP:clientID")
    * monitor [FPS]
                 - start monitor mode showing real-time outputs from both local and remote clients
    * broadcast  - send a UDP discovery message (remote nodes reply)
    * connect <IP>
                 - send a UDP node connection request to a remote node
    * transmit <ms>
                 - set the transmit rate (in milliseconds) at which this node sends update messages
    * exit       - shut down the current tmux session (all windows)

 - At startup, the server reads "route.rt" (if present) for preconfigured routes.
 - All inter-node communication (connection, client updates, routing) is done over UDP.

 Build:
   gcc -std=c11 -o server server.c

 Run:
   ./server [port]
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>  // Added for terminal width determination in monitor mode

#define SERVER_PORT 12345
#define BROADCAST_PORT 12346   // UDP port for node-to-node messages and discovery
#define MAX_CLIENTS 20
#define CHANNELS_PER_APP 5    // 5 outputs, 5 inputs
#define MAX_MSG_LENGTH 512

// For remote nodes:
#define MAX_REMOTE_NODES 10
#define MAX_REMOTE_CLIENTS 20

#define DEFAULT_MONITOR_FPS 2

// Global transmit rate in milliseconds (default 1000 ms)
static unsigned int transmit_rate_ms = 1000;

// Structures for local clients.
typedef struct {
    int sockfd;
    int client_id;
    int active;
    char name[64];
} ClientInfo;

typedef struct {
    int in_client_id;  // local destination client id; -1 means no route
    int in_channel;    // 0..4
} Route;

typedef struct {
    char last_out[CHANNELS_PER_APP][MAX_MSG_LENGTH];
    char last_in[CHANNELS_PER_APP][MAX_MSG_LENGTH];
} ClientData;

static ClientData client_data[MAX_CLIENTS];
static ClientInfo clients[MAX_CLIENTS];
static int next_client_id = 1;
static Route routing[MAX_CLIENTS + 1][CHANNELS_PER_APP];

// Structures for remote nodes.
typedef struct {
    struct sockaddr_in addr; // UDP address of remote node
    char ip[INET_ADDRSTRLEN];
    bool connected;
    int client_count;
    struct {
        int client_id;
        char name[64];
        char last_out[CHANNELS_PER_APP][MAX_MSG_LENGTH];
        char last_in[CHANNELS_PER_APP][MAX_MSG_LENGTH];
    } clients[MAX_REMOTE_CLIENTS];
} RemoteNode;

static RemoteNode remote_nodes[MAX_REMOTE_NODES];
static int remote_node_count = 0;

// Global UDP socket for node communication.
static int udp_fd;

// Global variable to track last transmit time.
static struct timeval last_transmit;

// Forward declarations.
static void handle_new_connection(int server_fd);
static void handle_client_input(int idx);
static void handle_console_input(void);
static void show_help(void);
static void route_command(const char *outCID_str, int outCH, const char *inCID_str, int inCH);
static void list_clients(void);
static void list_routes(void);
static int find_client_index(int cid);
static void trim_newline(char *s);
static void shutdown_tmux(void);
static void process_routing_file(void);
static void route_command_from_file(int outCID, int outCH, int inCID, int inCH);
static void monitor_mode(int fps);

// UDP node communication functions.
static void process_udp_message(void);
static void connect_to_node(const char *ip);
static bool is_remote_id(const char *id_str);
static void forward_route_to_remote(const char *outCID_str, int outCH, const char *inCID_str, int inCH);
static void send_update_messages(void);

int main(int argc, char *argv[]) {
    unsigned short port = SERVER_PORT;
    if (argc > 1) {
        unsigned short temp = (unsigned short)atoi(argv[1]);
        if (temp > 0)
            port = temp;
    }

    memset(clients, 0, sizeof(clients));
    memset(routing, -1, sizeof(routing));
    memset(client_data, 0, sizeof(client_data));

    // Create TCP socket for local client connections.
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, 5) < 0) { perror("listen"); close(server_fd); return 1; }
    printf("Switchboard Server listening on TCP port %hu.\n", port);
    printf("Type 'help' for commands.\n");

    // Create UDP socket for node-to-node messages.
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("UDP socket"); exit(1); }
    int broadcastEnable = 1;
    if (setsockopt(udp_fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt (SO_BROADCAST)"); exit(1);
    }
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(BROADCAST_PORT);
    if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("bind UDP socket"); exit(1);
    }

    process_routing_file();

    // Initialize last_transmit time.
    gettimeofday(&last_transmit, NULL);

    // Main loop using select with a timeout based on transmit_rate_ms.
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].sockfd, &readfds);
                if (clients[i].sockfd > maxfd)
                    maxfd = clients[i].sockfd;
            }
        }
        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > maxfd)
            maxfd = STDIN_FILENO;
        FD_SET(udp_fd, &readfds);
        if (udp_fd > maxfd)
            maxfd = udp_fd;

        struct timeval now, timeout;
        gettimeofday(&now, NULL);
        unsigned int elapsed_ms = (now.tv_sec - last_transmit.tv_sec) * 1000 +
                                  (now.tv_usec - last_transmit.tv_usec) / 1000;
        unsigned int wait_ms = (elapsed_ms >= transmit_rate_ms) ? 0 : (transmit_rate_ms - elapsed_ms);
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0) { perror("select"); break; }
        if (FD_ISSET(server_fd, &readfds))
            handle_new_connection(server_fd);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].sockfd, &readfds))
                handle_client_input(i);
        }
        if (FD_ISSET(STDIN_FILENO, &readfds))
            handle_console_input();
        if (FD_ISSET(udp_fd, &readfds))
            process_udp_message();

        // Time to send update messages?
        gettimeofday(&now, NULL);
        elapsed_ms = (now.tv_sec - last_transmit.tv_sec) * 1000 +
                     (now.tv_usec - last_transmit.tv_usec) / 1000;
        if (elapsed_ms >= transmit_rate_ms) {
            send_update_messages();
            last_transmit = now;
        }
    }
    close(server_fd);
    return 0;
}

// Sends an UPDATE message for each local client to all connected nodes.
static void send_update_messages(void) {
    char msg[1024];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active)
            continue;
        char channel_data[512] = "";
        for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
            strcat(channel_data, client_data[i].last_out[ch]);
            if (ch < CHANNELS_PER_APP - 1)
                strcat(channel_data, ";");
        }
        snprintf(msg, sizeof(msg), "UPDATE %d %s\n", clients[i].client_id, channel_data);
        for (int j = 0; j < remote_node_count; j++) {
            sendto(udp_fd, msg, strlen(msg), 0,
                   (struct sockaddr *)&remote_nodes[j].addr, sizeof(remote_nodes[j].addr));
        }
    }
}

// Accept a new TCP connection from a local client.
static void handle_new_connection(int server_fd) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int client_sock = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (client_sock < 0) {
        perror("accept");
        return;
    }
    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) { idx = i; break; }
    }
    if (idx < 0) {
        const char *msg = "Server full.\n";
        write(client_sock, msg, strlen(msg));
        close(client_sock);
        return;
    }
    int cid = next_client_id++;
    clients[idx].sockfd = client_sock;
    clients[idx].client_id = cid;
    clients[idx].active = 1;
    snprintf(clients[idx].name, sizeof(clients[idx].name), "Client%d", cid);
    char greet[128];
    snprintf(greet, sizeof(greet),
             "Welcome to Switchboard. You are client_id=%d, with 5 in / 5 out.\n", cid);
    write(client_sock, greet, strlen(greet));
    printf("Local client %d connected (slot=%d).\n", cid, idx);
}

// Process data from a local client.
static void handle_client_input(int i) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    ssize_t n = recv(clients[i].sockfd, buf, sizeof(buf)-1, 0);
    if (n <= 0) {
        printf("Local client %d disconnected.\n", clients[i].client_id);
        close(clients[i].sockfd);
        clients[i].active = 0;
        return;
    }
    char *start = buf;
    while (1) {
        char *nl = strchr(start, '\n');
        if (!nl) break;
        *nl = '\0';
        trim_newline(start);
        int out_ch = -1;
        if (strncmp(start, "out", 3) == 0 && isdigit((unsigned char)start[3]))
            out_ch = start[3] - '0';
        if (out_ch >= 0 && out_ch < CHANNELS_PER_APP) {
            char *colon = strchr(start, ':');
            const char *msg = "";
            if (colon) {
                msg = colon+1;
                while (*msg == ' ' || *msg=='\t') msg++;
            }
            strncpy(client_data[i].last_out[out_ch], msg, MAX_MSG_LENGTH-1);
            client_data[i].last_out[out_ch][MAX_MSG_LENGTH-1] = '\0';
            int out_cid = clients[i].client_id;
            Route r = routing[out_cid][out_ch];
            if (r.in_client_id >= 0) {
                int idx_in = find_client_index(r.in_client_id);
                if (idx_in >= 0 && clients[idx_in].active) {
                    char outbuf[600];
                    snprintf(outbuf, sizeof(outbuf), "in%d from client%d: %s\n", r.in_channel, out_cid, msg);
                    send(clients[idx_in].sockfd, outbuf, strlen(outbuf), 0);
                    strncpy(client_data[idx_in].last_in[r.in_channel], outbuf, MAX_MSG_LENGTH-1);
                    client_data[idx_in].last_in[r.in_channel][MAX_MSG_LENGTH-1] = '\0';
                }
            }
        }
        start = nl + 1;
    }
}

// Process console input from the operator.
static void handle_console_input(void) {
    char cmdline[256];
    if (!fgets(cmdline, sizeof(cmdline), stdin))
        return;
    trim_newline(cmdline);
    if (strcmp(cmdline, "") == 0)
        return;
    if (strcmp(cmdline, "help") == 0) { show_help(); return; }
    if (strcmp(cmdline, "list") == 0) { list_clients(); return; }
    if (strcmp(cmdline, "routes") == 0) { list_routes(); return; }
    if (strcmp(cmdline, "exit") == 0) { shutdown_tmux(); return; }
    if (strncmp(cmdline, "monitor", 7) == 0) {
        strtok(cmdline, " ");
        int fps = DEFAULT_MONITOR_FPS;
        char *arg = strtok(NULL, " ");
        if (arg) { int temp = atoi(arg); if (temp > 0) fps = temp; }
        monitor_mode(fps);
        return;
    }
    if (strncmp(cmdline, "broadcast", 9) == 0) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(BROADCAST_PORT);
        dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
        char request[] = "DISCOVER_REQUEST\n";
        if (sendto(udp_fd, request, strlen(request), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
            perror("sendto DISCOVER_REQUEST");
        else { printf("Broadcast sent. Waiting for responses...\n"); fflush(stdout); }
        return;
    }
    if (strncmp(cmdline, "connect", 7) == 0) {
        strtok(cmdline, " ");
        char *ip = strtok(NULL, " ");
        if (!ip) { printf("Usage: connect <IP>\n"); return; }
        connect_to_node(ip);
        return;
    }
    if (strncmp(cmdline, "transmit", 8) == 0) {
        strtok(cmdline, " ");
        char *rate_str = strtok(NULL, " ");
        if (!rate_str) { printf("Usage: transmit <rate_in_ms>\n"); return; }
        unsigned int rate = (unsigned int)atoi(rate_str);
        if (rate == 0) {
            printf("Transmit rate must be > 0\n");
            return;
        }
        transmit_rate_ms = rate;
        printf("Transmit rate set to %u ms.\n", transmit_rate_ms);
        return;
    }
    if (strncmp(cmdline, "print", 5) == 0) {
        strtok(cmdline, " ");
        char *pClientID = strtok(NULL, " ");
        if (!pClientID) { printf("Usage: print <clientID> (for remote, use IP:clientID)\n"); return; }
        if (strchr(pClientID, ':')) {
            char node_ip[INET_ADDRSTRLEN];
            int remote_id;
            if (sscanf(pClientID, "%15[^:]:%d", node_ip, &remote_id) == 2) {
                bool found = false;
                for (int i = 0; i < remote_node_count; i++) {
                    if (strcmp(remote_nodes[i].ip, node_ip) == 0) {
                        for (int j = 0; j < remote_nodes[i].client_count; j++) {
                            if (remote_nodes[i].clients[j].client_id == remote_id) {
                                printf("Data for remote client %s:%d (%s):\n",
                                       node_ip, remote_id, remote_nodes[i].clients[j].name);
                                printf("%-8s | %-50s | %-50s\n", "Channel", "Output", "Input");
                                printf("-------------------------------------------------------------\n");
                                for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
                                    printf("%-8d | %-50.50s | %-50.50s\n", ch,
                                           remote_nodes[i].clients[j].last_out[ch],
                                           remote_nodes[i].clients[j].last_in[ch]);
                                }
                                found = true;
                                break;
                            }
                        }
                    }
                }
                if (!found)
                    printf("Remote client %s not found.\n", pClientID);
            }
        } else {
            int clientID = atoi(pClientID);
            int idx = find_client_index(clientID);
            if (idx < 0) { printf("No active local client with clientID %d\n", clientID); return; }
            printf("Data for local client %d (%s):\n", clientID, clients[idx].name);
            printf("%-8s | %-50s | %-50s\n", "Channel", "Output", "Input");
            printf("-------------------------------------------------------------\n");
            for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
                printf("%-8d | %-50.50s | %-50.50s\n", ch,
                       client_data[idx].last_out[ch],
                       client_data[idx].last_in[ch]);
            }
        }
        return;
    }
    if (strncmp(cmdline, "route", 5) == 0) {
        strtok(cmdline, " ");
        char *pOutCID = strtok(NULL, " ");
        char *pOutStr = strtok(NULL, " ");
        char *pInCID = strtok(NULL, " ");
        char *pInStr = strtok(NULL, " ");
        if (!pOutCID || !pOutStr || !pInCID || !pInStr) {
            printf("Usage: route <outCID> <outCH|all> <inCID> <inCH|all>\n"); return;
        }
        if (strchr(pOutCID, ':') || strchr(pInCID, ':')) {
            forward_route_to_remote(pOutCID, atoi(pOutStr), pInCID, atoi(pInStr));
            return;
        }
        bool outAll = (strcmp(pOutStr, "all") == 0);
        int fixedOut = -1;
        if (!outAll) {
            if (isdigit((unsigned char)pOutStr[0]))
                fixedOut = atoi(pOutStr);
            else if (strncmp(pOutStr, "out", 3) == 0 && isdigit((unsigned char)pOutStr[3]))
                fixedOut = pOutStr[3] - '0';
        }
        bool inAll = (strcmp(pInStr, "all") == 0);
        int fixedIn = -1;
        if (!inAll) {
            if (isdigit((unsigned char)pInStr[0]))
                fixedIn = atoi(pInStr);
            else if (strncmp(pInStr, "in", 2) == 0 && isdigit((unsigned char)pInStr[2]))
                fixedIn = pInStr[2] - '0';
        }
        if (!outAll && (fixedOut < 0 || fixedOut >= CHANNELS_PER_APP)) {
            printf("Invalid output channel. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1); return;
        }
        if (!inAll && (fixedIn < 0 || fixedIn >= CHANNELS_PER_APP)) {
            printf("Invalid input channel. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1); return;
        }
        if (outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command(pOutCID, i, pInCID, i);
        } else if (outAll && !inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command(pOutCID, i, pInCID, fixedIn);
        } else if (!outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command(pOutCID, fixedOut, pInCID, i);
        } else {
            route_command(pOutCID, fixedOut, pInCID, fixedIn);
        }
        return;
    }
    printf("Unknown command: %s\n", cmdline);
}

static void show_help(void) {
    printf("Commands:\n");
    printf(" help                        - show this help\n");
    printf(" list                        - list local clients and connected nodes\n");
    printf(" routes                      - list local routing table\n");
    printf(" route <outCID> <outCH|all> <inCID> <inCH|all>\n");
    printf(" print <clientID>            - show channel data (for remote, use IP:clientID)\n");
    printf(" monitor [FPS]               - display real time outputs (local and remote)\n");
    printf(" broadcast                   - send a UDP discovery request\n");
    printf(" connect <IP>                - send a UDP node connection request to a remote node\n");
    printf(" transmit <ms>               - set transmit rate (in ms) for sending update messages\n");
    printf(" exit                        - shutdown the current tmux session (all windows)\n");
    printf("\n");
}

// Update local routing table.
static void route_command(const char *outCID_str, int outCH, const char *inCID_str, int inCH) {
    int outCID = atoi(outCID_str);
    int idxO = find_client_index(outCID);
    if (idxO < 0) { printf("No such local client %d\n", outCID); return; }
    int inCID = atoi(inCID_str);
    int idxI = find_client_index(inCID);
    if (idxI < 0) { printf("No such local client %d\n", inCID); return; }
    routing[outCID][outCH].in_client_id = inCID;
    routing[outCID][outCH].in_channel = inCH;
    printf("Routed local client%d out%d -> local client%d in%d\n", outCID, outCH, inCID, inCH);
}

// List local clients and remote node clients.
static void list_clients(void) {
    printf("Local Clients:\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active)
            printf(" clientID=%d sockfd=%d name=%s\n", clients[i].client_id, clients[i].sockfd, clients[i].name);
    }
    printf("\nConnected Nodes:\n");
    for (int i = 0; i < remote_node_count; i++) {
        printf(" Node %s:\n", remote_nodes[i].ip);
        for (int j = 0; j < remote_nodes[i].client_count; j++) {
            printf("   clientID=%d (Identifier: %s:%d) name=%s\n",
                   remote_nodes[i].clients[j].client_id,
                   remote_nodes[i].ip, remote_nodes[i].clients[j].client_id,
                   remote_nodes[i].clients[j].name);
        }
    }
}

// List local routing table.
static void list_routes(void) {
    printf("Local Routes:\n");
    for (int cid = 1; cid < next_client_id; cid++) {
        int idx = find_client_index(cid);
        if (idx < 0) continue;
        for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
            int inCID = routing[cid][ch].in_client_id;
            if (inCID >= 0) {
                int inCH = routing[cid][ch].in_channel;
                printf(" local client%d.out%d -> local client%d.in%d\n", cid, ch, inCID, inCH);
            }
        }
    }
}

// Return index of local client with given client id.
static int find_client_index(int cid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].client_id == cid)
            return i;
    }
    return -1;
}

// Remove newline/carriage returns.
static void trim_newline(char *s) {
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
    p = strchr(s, '\r');
    if (p) *p = '\0';
}

// Shutdown the tmux session.
static void shutdown_tmux(void) {
    FILE *fp = popen("tmux display-message -p '#S'", "r");
    if (!fp) { perror("popen"); return; }
    char session_name[64];
    if (fgets(session_name, sizeof(session_name), fp) == NULL) {
        pclose(fp);
        fprintf(stderr, "Failed to get tmux session name.\n");
        return;
    }
    trim_newline(session_name);
    pclose(fp);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "tmux kill-session -t %s", session_name);
    printf("Executing: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0)
        fprintf(stderr, "tmux kill-session command failed with code %d.\n", ret);
    exit(0);
}

// Process routing file "route.rt" for preconfigured routes.
static void process_routing_file(void) {
    FILE *fp = fopen("route.rt", "r");
    if (!fp) { printf("Routing file 'route.rt' not found.\n"); return; }
    char line[256];
    bool all_success = true;
    int cmd_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (strlen(line) == 0 || strncmp(line, "route", 5) != 0)
            continue;
        cmd_count++;
        char *token = strtok(line, " ");
        if (!token || strcmp(token, "route") != 0) {
            printf("Invalid command in routing file.\n");
            all_success = false;
            continue;
        }
        char *pOutCID = strtok(NULL, " ");
        char *pOutStr = strtok(NULL, " ");
        char *pInCID = strtok(NULL, " ");
        char *pInStr = strtok(NULL, " ");
        if (!pOutCID || !pOutStr || !pInCID || !pInStr) {
            printf("Incomplete routing command in file.\n");
            all_success = false;
            continue;
        }
        int outCID = atoi(pOutCID);
        int inCID = atoi(pInCID);
        bool outAll = (strcmp(pOutStr, "all") == 0);
        int fixedOut = -1;
        if (!outAll) {
            if (isdigit((unsigned char)pOutStr[0]))
                fixedOut = atoi(pOutStr);
            else if (strncmp(pOutStr, "out", 3) == 0 && isdigit((unsigned char)pOutStr[3]))
                fixedOut = pOutStr[3] - '0';
            else { printf("Invalid output channel in routing file.\n"); all_success = false; continue; }
        }
        bool inAll = (strcmp(pInStr, "all") == 0);
        int fixedIn = -1;
        if (!inAll) {
            if (isdigit((unsigned char)pInStr[0]))
                fixedIn = atoi(pInStr);
            else if (strncmp(pInStr, "in", 2) == 0 && isdigit((unsigned char)pInStr[2]))
                fixedIn = pInStr[2] - '0';
            else { printf("Invalid input channel in routing file.\n"); all_success = false; continue; }
        }
        if (!outAll && (fixedOut < 0 || fixedOut >= CHANNELS_PER_APP)) {
            printf("Invalid output channel value in routing file.\n"); all_success = false; continue;
        }
        if (!inAll && (fixedIn < 0 || fixedIn >= CHANNELS_PER_APP)) {
            printf("Invalid input channel value in routing file.\n"); all_success = false; continue;
        }
        if (outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command_from_file(outCID, i, inCID, i);
        } else if (outAll && !inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command_from_file(outCID, i, inCID, fixedIn);
        } else if (!outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++)
                route_command_from_file(outCID, fixedOut, inCID, i);
        } else {
            route_command_from_file(outCID, fixedOut, inCID, fixedIn);
        }
    }
    fclose(fp);
    if (!all_success || cmd_count == 0)
        printf("Error processing routing file or no valid commands found.\n");
    else {
        fp = fopen("route.rt", "r");
        if (fp) {
            printf("Routing file executed successfully. Contents of 'route.rt':\n");
            while (fgets(line, sizeof(line), fp))
                printf("%s", line);
            fclose(fp);
        }
    }
}

// Similar to route_command, but used when processing the routing file.
static void route_command_from_file(int outCID, int outCH, int inCID, int inCH) {
    routing[outCID][outCH].in_client_id = inCID;
    routing[outCID][outCH].in_channel = inCH;
    printf("Preconfigured: local client%d out%d -> local client%d in%d\n", outCID, outCH, inCID, inCH);
}

/*
 * monitor_mode()
 * Displays both local and remote client data.
 * FIX: Terminal width is obtained once at mode start and used for fixed field widths.
 */
static void monitor_mode(int fps) {
    struct termios orig_termios, new_termios;
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) { perror("tcgetattr"); return; }
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1) { perror("tcsetattr"); return; }

    // Get terminal width once at the start of monitor mode.
    struct winsize ws;
    int term_width = 80;  // default width
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_width = ws.ws_col;
    }
    // Compute a fixed column width for channel data.
    int col_width = (term_width - 20) / CHANNELS_PER_APP;
    if (col_width < 10) {
        col_width = 10;
    }

    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    bool recording = false;
    FILE *log_file = NULL;
    char log_filename[256] = "";
    int recorded_client_indices[MAX_CLIENTS];
    int recorded_client_count = 0;
    printf("Entering monitor mode at %d FPS.\nPress 'Q' to quit, 'R' to toggle recording.\n", fps);
    fflush(stdout);
    bool quit = false;
    while (!quit) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].sockfd, &readfds);
                if (clients[i].sockfd > maxfd)
                    maxfd = clients[i].sockfd;
            }
        }
        FD_SET(udp_fd, &readfds);
        if (udp_fd > maxfd)
            maxfd = udp_fd;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000000 / fps;
        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) { perror("select in monitor_mode"); break; }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].sockfd, &readfds))
                handle_client_input(i);
        }
        if (FD_ISSET(udp_fd, &readfds))
            process_udp_message();
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'Q' || ch == 'q')
                    quit = true;
                else if (ch == 'R' || ch == 'r') {
                    if (!recording) {
                        if (mkdir("logs", 0755) == -1 && errno != EEXIST) perror("mkdir");
                        time_t now = time(NULL);
                        struct tm *tm_info = localtime(&now);
                        char timestamp[64];
                        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
                        snprintf(log_filename, sizeof(log_filename), "logs/monitor_%s.csv", timestamp);
                        log_file = fopen(log_filename, "w");
                        if (!log_file) perror("fopen for log file");
                        else {
                            recorded_client_count = 0;
                            for (int i = 0; i < MAX_CLIENTS; i++) {
                                if (clients[i].active)
                                    recorded_client_indices[recorded_client_count++] = i;
                            }
                            fprintf(log_file, "timestamp,");
                            for (int j = 0; j < recorded_client_count; j++) {
                                int idx = recorded_client_indices[j];
                                fprintf(log_file, "client%d", clients[idx].client_id);
                                if (j != recorded_client_count - 1)
                                    fprintf(log_file, ",");
                            }
                            for (int i = 0; i < remote_node_count; i++) {
                                for (int j = 0; j < remote_nodes[i].client_count; j++)
                                    fprintf(log_file, ",%s:%d", remote_nodes[i].ip, remote_nodes[i].clients[j].client_id);
                            }
                            fprintf(log_file, "\n");
                            fflush(log_file);
                            recording = true;
                        }
                    } else {
                        if (log_file) { fclose(log_file); log_file = NULL; }
                        recording = false;
                    }
                }
            }
        }
        if (recording && log_file) {
            struct timeval now, delta;
            gettimeofday(&now, NULL);
            delta.tv_sec = now.tv_sec - start_time.tv_sec;
            delta.tv_usec = now.tv_usec - start_time.tv_usec;
            if (delta.tv_usec < 0) { delta.tv_sec--; delta.tv_usec += 1000000; }
            fprintf(log_file, "\"%ld.%06ld\",", delta.tv_sec, delta.tv_usec);
            for (int j = 0; j < recorded_client_count; j++) {
                int idx = recorded_client_indices[j];
                char data[MAX_MSG_LENGTH];
                strncpy(data, client_data[idx].last_out[0], MAX_MSG_LENGTH - 1);
                data[MAX_MSG_LENGTH - 1] = '\0';
                fprintf(log_file, "\"%s\",", data);
            }
            for (int i = 0; i < remote_node_count; i++) {
                for (int j = 0; j < remote_nodes[i].client_count; j++)
                    fprintf(log_file, "\"%s\"", remote_nodes[i].clients[j].last_out[0]);
            }
            fprintf(log_file, "\n");
            fflush(log_file);
        }
        system("clear");
        printf("=== Monitor Mode (FPS: %d) ===\n", fps);
        printf("Press 'Q' to quit, 'R' to toggle recording.\n");
        if (recording)
            printf("Recording: ON (file: %s)\n", log_filename);
        else
            printf("Recording: OFF\n");
        printf("-------------------------------------------------------------\n");
        printf("Local Clients:\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                printf("client%d | ", clients[i].client_id);
                for (int ch = 0; ch < CHANNELS_PER_APP; ch++)
                    printf("[%d]: %-*.*s ", ch, col_width, col_width, client_data[i].last_out[ch]);
                printf("\n");
            }
        }
        printf("\nRemote Clients:\n");
        for (int i = 0; i < remote_node_count; i++) {
            printf("Node %s:\n", remote_nodes[i].ip);
            for (int j = 0; j < remote_nodes[i].client_count; j++) {
                printf(" %s:%d | ", remote_nodes[i].ip, remote_nodes[i].clients[j].client_id);
                for (int ch = 0; ch < CHANNELS_PER_APP; ch++)
                    printf("[%d]: %-*.*s ", ch, col_width, col_width, remote_nodes[i].clients[j].last_out[ch]);
                printf("\n");
            }
        }
        fflush(stdout);
    }
    if (log_file) { fclose(log_file); log_file = NULL; }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios) == -1)
        perror("tcsetattr");
    printf("Exiting monitor mode.\n");
}

// ----- UDP Message Processing -----
static void process_udp_message(void) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    struct sockaddr_in sender_addr;
    socklen_t addrlen = sizeof(sender_addr);
    ssize_t n = recvfrom(udp_fd, buf, sizeof(buf)-1, 0, (struct sockaddr *)&sender_addr, &addrlen);
    if (n < 0) { perror("recvfrom"); return; }
    buf[n] = '\0';
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));
    // Discovery request.
    if (strcmp(buf, "DISCOVER_REQUEST\n") == 0) {
        char response[] = "DISCOVER_RESPONSE\n";
        sendto(udp_fd, response, strlen(response), 0, (struct sockaddr *)&sender_addr, addrlen);
        return;
    }
    // FIX: Handle discovery responses.
    if (strcmp(buf, "DISCOVER_RESPONSE\n") == 0) {
        bool exists = false;
        for (int i = 0; i < remote_node_count; i++) {
            if (strcmp(remote_nodes[i].ip, sender_ip) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists && remote_node_count < MAX_REMOTE_NODES) {
            RemoteNode *rn = &remote_nodes[remote_node_count];
            rn->addr = sender_addr;
            strncpy(rn->ip, sender_ip, sizeof(rn->ip)-1);
            rn->client_count = 0;
            rn->connected = true;
            remote_node_count++;
            printf("Discovered remote node %s via DISCOVER_RESPONSE.\n", sender_ip);
        }
        return;
    }
    // Node connection messages.
    if (strncmp(buf, "NODE_CONNECT_REQUEST", 20) == 0) {
        char response[] = "NODE_CONNECT_ACK\n";
        sendto(udp_fd, response, strlen(response), 0, (struct sockaddr *)&sender_addr, addrlen);
        bool exists = false;
        for (int i = 0; i < remote_node_count; i++) {
            if (strcmp(remote_nodes[i].ip, sender_ip) == 0) { exists = true; break; }
        }
        if (!exists && remote_node_count < MAX_REMOTE_NODES) {
            RemoteNode *rn = &remote_nodes[remote_node_count];
            rn->addr = sender_addr;
            strncpy(rn->ip, sender_ip, sizeof(rn->ip)-1);
            rn->client_count = 0;
            rn->connected = true;
            remote_node_count++;
            printf("Added remote node %s (via NODE_CONNECT_REQUEST).\n", rn->ip);
        }
        return;
    }
    if (strncmp(buf, "NODE_CONNECT_ACK", 16) == 0) {
        bool exists = false;
        for (int i = 0; i < remote_node_count; i++) {
            if (strcmp(remote_nodes[i].ip, sender_ip) == 0) { exists = true; break; }
        }
        if (!exists && remote_node_count < MAX_REMOTE_NODES) {
            RemoteNode *rn = &remote_nodes[remote_node_count];
            rn->addr = sender_addr;
            strncpy(rn->ip, sender_ip, sizeof(rn->ip)-1);
            rn->client_count = 0;
            rn->connected = true;
            remote_node_count++;
            printf("Received NODE_CONNECT_ACK. Added remote node %s.\n", rn->ip);
        }
        return;
    }
    // Handle client list updates.
    if (strncmp(buf, "CLIENT", 6) == 0) {
        int cid;
        char cname[64];
        if (sscanf(buf, "CLIENT %d %63s", &cid, cname) == 2) {
            for (int i = 0; i < remote_node_count; i++) {
                if (strcmp(remote_nodes[i].ip, sender_ip) == 0) {
                    bool found = false;
                    for (int j = 0; j < remote_nodes[i].client_count; j++) {
                        if (remote_nodes[i].clients[j].client_id == cid) {
                            strncpy(remote_nodes[i].clients[j].name, cname, sizeof(cname));
                            found = true;
                            break;
                        }
                    }
                    if (!found && remote_nodes[i].client_count < MAX_REMOTE_CLIENTS) {
                        remote_nodes[i].clients[remote_nodes[i].client_count].client_id = cid;
                        strncpy(remote_nodes[i].clients[remote_nodes[i].client_count].name, cname, sizeof(cname));
                        remote_nodes[i].client_count++;
                    }
                }
            }
        }
        return;
    }
    // Handle UPDATE messages.
    if (strncmp(buf, "UPDATE", 6) == 0) {
        int cid;
        char *p = buf + 6;
        while (isspace((unsigned char)*p)) p++;
        cid = atoi(p);
        char *space = strchr(p, ' ');
        if (!space) return;
        space++;
        char channel_data[256];
        strncpy(channel_data, space, sizeof(channel_data)-1);
        channel_data[sizeof(channel_data)-1] = '\0';
        char *token;
        int ch = 0;
        char channels[CHANNELS_PER_APP][MAX_MSG_LENGTH];
        token = strtok(channel_data, ";");
        while (token && ch < CHANNELS_PER_APP) {
            strncpy(channels[ch], token, MAX_MSG_LENGTH-1);
            channels[ch][MAX_MSG_LENGTH-1] = '\0';
            ch++;
            token = strtok(NULL, ";");
        }
        for (int i = 0; i < remote_node_count; i++) {
            if (strcmp(remote_nodes[i].ip, sender_ip) == 0) {
                bool found = false;
                for (int j = 0; j < remote_nodes[i].client_count; j++) {
                    if (remote_nodes[i].clients[j].client_id == cid) {
                        for (int k = 0; k < CHANNELS_PER_APP; k++)
                            strncpy(remote_nodes[i].clients[j].last_out[k], channels[k], MAX_MSG_LENGTH-1);
                        found = true;
                        break;
                    }
                }
                if (!found && remote_nodes[i].client_count < MAX_REMOTE_CLIENTS) {
                    remote_nodes[i].clients[remote_nodes[i].client_count].client_id = cid;
                    strncpy(remote_nodes[i].clients[remote_nodes[i].client_count].name, "RemoteClient", sizeof(remote_nodes[i].clients[remote_nodes[i].client_count].name)-1);
                    for (int k = 0; k < CHANNELS_PER_APP; k++)
                        strncpy(remote_nodes[i].clients[remote_nodes[i].client_count].last_out[k], channels[k], MAX_MSG_LENGTH-1);
                    remote_nodes[i].client_count++;
                }
                break;
            }
        }
        return;
    }
    // Handle remote routing commands.
    if (strncmp(buf, "ROUTE", 5) == 0) {
        printf("Received remote routing command: %s\n", buf);
        // (Additional remote routing handling can be added here.)
        return;
    }
}

// ----- UDP-based Node Communication -----
static void connect_to_node(const char *ip) {
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(BROADCAST_PORT);
    if (inet_pton(AF_INET, ip, &remote_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return;
    }
    char msg[] = "NODE_CONNECT_REQUEST\n";
    if (sendto(udp_fd, msg, strlen(msg), 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0)
        perror("sendto NODE_CONNECT_REQUEST");
    else
        printf("Sent NODE_CONNECT_REQUEST to %s\n", ip);
}

static bool is_remote_id(const char *id_str) {
    return (strchr(id_str, ':') != NULL);
}

static void forward_route_to_remote(const char *outCID_str, int outCH, const char *inCID_str, int inCH) {
    char remote_ip[INET_ADDRSTRLEN] = "";
    if (is_remote_id(outCID_str))
        sscanf(outCID_str, "%15[^:]", remote_ip);
    else if (is_remote_id(inCID_str))
        sscanf(inCID_str, "%15[^:]", remote_ip);
    else {
        printf("Routing command: no remote endpoint detected.\n");
        return;
    }
    for (int i = 0; i < remote_node_count; i++) {
        if (strcmp(remote_nodes[i].ip, remote_ip) == 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "ROUTE %s %d %s %d\n", outCID_str, outCH, inCID_str, inCH);
            sendto(udp_fd, msg, strlen(msg), 0,
                   (struct sockaddr *)&remote_nodes[i].addr, sizeof(remote_nodes[i].addr));
            printf("Forwarded route command to node %s: %s", remote_ip, msg);
            return;
        }
    }
    printf("Remote node %s not found for routing.\n", remote_ip);
}
