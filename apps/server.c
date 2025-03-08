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
    * route <A> <out-channel> <B> <in-channel>
    * print <clientID> <-- show last data for all channels of the given client
    * help
    * monitor      <-- display in real time the five output values of all connected clients (exit with Q)
 - At startup, the server now checks for a file named "route.rt". If it exists,
   the file is read line-by-line to execute routing commands. If the file is missing
   or a failure occurs during processing, an appropriate message is displayed.
   Otherwise, the file's content is printed.

 Design principles for this modification:
 - We keep a per-client record of the last message seen on each output and each input channel.
 - We add a new monitor mode that polls the client sockets every time the view is updated.
 - We use plain C with -std=c11 and only standard cross-platform libraries.
 - A new helper function monitor_mode() is added that temporarily sets the terminal into non-canonical mode
   so that a single keystroke (Q) can stop the monitoring.
 - We do not remove any existing functionality.
 - Comments throughout explain design choices.
 
 Build:
   gcc -std=c11 -o server server.c

 Run:
   ./server (optionally specify a port)
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>   // Support for bool type.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <termios.h>   // For non-canonical mode in monitor command

#define SERVER_PORT 12345
#define MAX_CLIENTS 20
#define CHANNELS_PER_APP 5  // 5 outputs, 5 inputs
#define MAX_MSG_LENGTH 512  // maximum message length for storing channel data

// Define the monitoring update FPS.
#define MONITOR_FPS 2

typedef struct {
    int sockfd;
    int client_id;
    int active;
    char name[64]; // optional label
} ClientInfo;

// Routing table: route[outClientID][outChannel] -> (inClientID, inChannel)
typedef struct {
    int in_client_id; // -1 if none
    int in_channel;   // 0..4
} Route;

// Structure to keep track of the last message for each channel for a client.
typedef struct {
    char last_out[CHANNELS_PER_APP][MAX_MSG_LENGTH]; // latest message from out channels
    char last_in[CHANNELS_PER_APP][MAX_MSG_LENGTH];  // latest message forwarded to this client's input
} ClientData;

static ClientData client_data[MAX_CLIENTS]; // one per client slot
static ClientInfo clients[MAX_CLIENTS];     // all possible clients
static int next_client_id = 1;                // ID to assign to the next connecting client

// Routing table: indexed by client_id so ensure the array is big enough.
static Route routing[MAX_CLIENTS + 1][CHANNELS_PER_APP];

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

// New helper functions for processing routing file.
static void process_routing_file(void);
static void route_command_from_file(int outCID, int outCH, int inCID, int inCH);

// New helper function for monitor mode.
static void monitor_mode(void);

int main(int argc, char *argv[]) {
    unsigned short port = SERVER_PORT;
    if (argc > 1) {
        unsigned short temp = (unsigned short)atoi(argv[1]);
        if (temp > 0) {
            port = temp;
        }
    }

    // Initialize arrays
    memset(clients, 0, sizeof(clients));
    memset(routing, -1, sizeof(routing));
    memset(client_data, 0, sizeof(client_data)); // initialize client data buffers

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
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
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

    // Process routing file "route.rt" at startup.
    process_routing_file();

    // Main loop using select()
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

// Accept a new client connection.
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
    clients[idx].sockfd = client_sock;
    clients[idx].client_id = cid;
    clients[idx].active = 1;
    snprintf(clients[idx].name, sizeof(clients[idx].name), "Client%d", cid);
    // Initialize client_data for this slot (already zeroed by memset)
    char greet[128];
    snprintf(greet, sizeof(greet),
             "Welcome to Switchboard. You are client_id=%d, with 5 in / 5 out.\n",
             cid);
    write(client_sock, greet, strlen(greet));
    printf("Client %d connected (slot=%d).\n", cid, idx);
}

// Handle incoming data from a client.
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
    // Process each line in the received buffer.
    char *start = buf;
    while (1) {
        char *nl = strchr(start, '\n');
        if (!nl) break; // no more lines
        *nl = '\0';
        // Trim potential CR
        char *cr = strchr(start, '\r');
        if (cr) *cr = '\0';
        trim_newline(start);
        // Expect lines like "outX: message"
        int out_ch = -1;
        if (strncmp(start, "out", 3) == 0 && isdigit((unsigned char)start[3])) {
            out_ch = start[3] - '0'; // channel number
        }
        if (out_ch >= 0 && out_ch < CHANNELS_PER_APP) {
            // Find colon and skip to message text
            char *colon = strchr(start, ':');
            const char *msg = "";
            if (colon) {
                msg = colon + 1;
                while (*msg == ' ' || *msg == '\t') { msg++; }
            }
            // Store the outgoing message for the client.
            strncpy(client_data[i].last_out[out_ch], msg, MAX_MSG_LENGTH - 1);
            client_data[i].last_out[out_ch][MAX_MSG_LENGTH - 1] = '\0';
            int out_cid = clients[i].client_id; // which client is sending
            Route r = routing[out_cid][out_ch];
            if (r.in_client_id >= 0) {
                int idx_in = find_client_index(r.in_client_id);
                if (idx_in >= 0 && clients[idx_in].active) {
                    char outbuf[600];
                    snprintf(outbuf, sizeof(outbuf),
                             "in%d from client%d: %s\n",
                             r.in_channel,
                             out_cid,
                             msg);
                    send(clients[idx_in].sockfd, outbuf, strlen(outbuf), 0);
                    // Also store this message as the latest input on channel r.in_channel for the receiving client.
                    strncpy(client_data[idx_in].last_in[r.in_channel], outbuf, MAX_MSG_LENGTH - 1);
                    client_data[idx_in].last_in[r.in_channel][MAX_MSG_LENGTH - 1] = '\0';
                }
            }
        }
        start = nl + 1;
    }
}

// Handle console input from the server operator.
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
    // New monitor command: enter monitoring mode to display output values.
    if (strcmp(cmdline, "monitor") == 0) {
        monitor_mode();
        return;
    }
    // Modified command: print <clientID>
    if (strncmp(cmdline, "print", 5) == 0) {
        // Skip the command token
        strtok(cmdline, " ");
        char *pClientID = strtok(NULL, " ");
        if (!pClientID) {
            printf("Usage: print <clientID>\n");
            return;
        }
        int clientID = atoi(pClientID);
        int idx = find_client_index(clientID);
        if (idx < 0) {
            printf("No active client with clientID %d\n", clientID);
            return;
        }
        printf("Data for client%d (%s):\n", clientID, clients[idx].name);
        // Print table header: Channel | Output Message | Input Message
        printf("%-8s | %-50s | %-50s\n", "Channel", "Output", "Input");
        printf("--------------------------------------------------------------------------------\n");
        for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
            printf("%-8d | %-50.50s | %-50.50s\n", ch, client_data[idx].last_out[ch], client_data[idx].last_in[ch]);
        }
        return;
    }
    // Modified route command:
    // Accepts either the legacy format "route <outCID> <outCH> <inCID> <inCH>"
    // or the extended format "route <outCID> all <inCID> all" (and combinations)
    if (strncmp(cmdline, "route", 5) == 0) {
        // Skip the command token
        strtok(cmdline, " ");
        char *pOutCID = strtok(NULL, " ");
        char *pOutStr = strtok(NULL, " ");
        char *pInCID = strtok(NULL, " ");
        char *pInStr = strtok(NULL, " ");
        if (!pOutCID || !pOutStr || !pInCID || !pInStr) {
            printf("Usage: route <outCID> <outCH|all> <inCID> <inCH|all>\n");
            return;
        }
        int outCID = atoi(pOutCID);
        int inCID = atoi(pInCID);

        // Determine if the output channel should be all or a fixed channel.
        bool outAll = (strcmp(pOutStr, "all") == 0);
        int fixedOut = -1;
        if (!outAll) {
            if (isdigit((unsigned char)pOutStr[0])) {
                fixedOut = atoi(pOutStr);
            } else if (strncmp(pOutStr, "out", 3) == 0 && isdigit((unsigned char)pOutStr[3])) {
                fixedOut = pOutStr[3] - '0';
            }
        }
        // Determine if the input channel should be all or a fixed channel.
        bool inAll = (strcmp(pInStr, "all") == 0);
        int fixedIn = -1;
        if (!inAll) {
            if (isdigit((unsigned char)pInStr[0])) {
                fixedIn = atoi(pInStr);
            } else if (strncmp(pInStr, "in", 2) == 0 && isdigit((unsigned char)pInStr[2])) {
                fixedIn = pInStr[2] - '0';
            }
        }

        // Validate fixed channels if provided.
        if (!outAll && (fixedOut < 0 || fixedOut >= CHANNELS_PER_APP)) {
            printf("Invalid output channel. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1);
            return;
        }
        if (!inAll && (fixedIn < 0 || fixedIn >= CHANNELS_PER_APP)) {
            printf("Invalid input channel. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1);
            return;
        }

        // Now apply routing based on the tokens.
        if (outAll && inAll) {
            // Route each channel i: client outCID outi -> client inCID ini
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command(outCID, i, inCID, i);
            }
        } else if (outAll && !inAll) {
            // Route all output channels to the fixed input channel.
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command(outCID, i, inCID, fixedIn);
            }
        } else if (!outAll && inAll) {
            // Route the fixed output channel to all input channels.
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command(outCID, fixedOut, inCID, i);
            }
        } else {
            // Single channel routing.
            route_command(outCID, fixedOut, inCID, fixedIn);
        }
        return;
    }
    printf("Unknown command: %s\n", cmdline);
}

static void show_help(void) {
    printf("Commands:\n");
    printf(" help                        - show this help\n");
    printf(" list                        - list connected clients\n");
    printf(" routes                      - list routing table\n");
    // Updated usage information for the modified route command.
    printf(" route X Y Z W               - connect clientX outY -> clientZ inW\n");
    printf("   (Y and/or W can be 'all' to route multiple channels)\n");
    // Updated usage information for the modified print command.
    printf(" print <clientID>            - show last data for all channels of the given client\n");
    // New usage information for the monitor command.
    printf(" monitor                     - display in real time all five output values of all connected clients (exit with Q)\n");
    printf("\n");
}

static void route_command(int outCID, int outCH, int inCID, int inCH) {
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
    routing[outCID][outCH].in_channel = inCH;
    printf("Routed client%d out%d -> client%d in%d\n", outCID, outCH, inCID, inCH);
}

static void list_clients(void) {
    printf("Active clients:\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            printf(" clientID=%d sockfd=%d name=%s\n", clients[i].client_id, clients[i].sockfd, clients[i].name);
        }
    }
}

static void list_routes(void) {
    printf("Routes:\n");
    for (int cid = 1; cid < next_client_id; cid++) {
        int idx = find_client_index(cid);
        if (idx < 0) continue;
        for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
            int inCID = routing[cid][ch].in_client_id;
            if (inCID >= 0) {
                int inCH = routing[cid][ch].in_channel;
                printf(" client%d.out%d -> client%d.in%d\n", cid, ch, inCID, inCH);
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
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
    p = strchr(s, '\r');
    if (p) *p = '\0';
}

/* 
 * process_routing_file()
 *
 * This function attempts to open "route.rt" for reading.
 * For each non-empty line that starts with "route", it parses the tokens in the expected format.
 * A new helper function, route_command_from_file(), is used to update the routing table without checking
 * for active clients (since preconfiguration occurs before clients connect).
 *
 * If the file is not found, or if any command fails to parse, a message is displayed.
 * If all commands are processed successfully, the contents of the file are re-read and displayed.
 */
static void process_routing_file(void) {
    FILE *fp = fopen("route.rt", "r");
    if (!fp) {
        printf("Routing file 'route.rt' not found.\n");
        return;
    }
    char line[256];
    bool all_success = true;
    int cmd_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        // Skip empty lines or lines that do not start with "route"
        if (strlen(line) == 0 || strncmp(line, "route", 5) != 0) {
            continue;
        }
        cmd_count++;
        // Tokenize the line.
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
            if (isdigit((unsigned char)pOutStr[0])) {
                fixedOut = atoi(pOutStr);
            } else if (strncmp(pOutStr, "out", 3) == 0 && isdigit((unsigned char)pOutStr[3])) {
                fixedOut = pOutStr[3] - '0';
            } else {
                printf("Invalid output channel in routing file.\n");
                all_success = false;
                continue;
            }
        }
        bool inAll = (strcmp(pInStr, "all") == 0);
        int fixedIn = -1;
        if (!inAll) {
            if (isdigit((unsigned char)pInStr[0])) {
                fixedIn = atoi(pInStr);
            } else if (strncmp(pInStr, "in", 2) == 0 && isdigit((unsigned char)pInStr[2])) {
                fixedIn = pInStr[2] - '0';
            } else {
                printf("Invalid input channel in routing file.\n");
                all_success = false;
                continue;
            }
        }
        // Validate fixed channels if not "all"
        if (!outAll && (fixedOut < 0 || fixedOut >= CHANNELS_PER_APP)) {
            printf("Invalid output channel value in routing file. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1);
            all_success = false;
            continue;
        }
        if (!inAll && (fixedIn < 0 || fixedIn >= CHANNELS_PER_APP)) {
            printf("Invalid input channel value in routing file. Must be 0..%d or 'all'\n", CHANNELS_PER_APP - 1);
            all_success = false;
            continue;
        }
        // Apply routing based on the tokens using the helper function.
        if (outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command_from_file(outCID, i, inCID, i);
            }
        } else if (outAll && !inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command_from_file(outCID, i, inCID, fixedIn);
            }
        } else if (!outAll && inAll) {
            for (int i = 0; i < CHANNELS_PER_APP; i++) {
                route_command_from_file(outCID, fixedOut, inCID, i);
            }
        } else {
            route_command_from_file(outCID, fixedOut, inCID, fixedIn);
        }
    }
    fclose(fp);
    if (!all_success || cmd_count == 0) {
        printf("Error processing routing file or no valid commands found.\n");
    } else {
        // Re-open the file to display its contents.
        fp = fopen("route.rt", "r");
        if (fp) {
            printf("Routing file executed successfully. Contents of 'route.rt':\n");
            while (fgets(line, sizeof(line), fp)) {
                printf("%s", line);
            }
            fclose(fp);
        }
    }
}

/*
 * route_command_from_file()
 *
 * This helper function is similar to route_command but does not check for active clients.
 * It directly updates the routing table and prints a message indicating the preconfigured route.
 * This ensures that routing commands from the file are stored even if no clients are connected yet.
 */
static void route_command_from_file(int outCID, int outCH, int inCID, int inCH) {
    routing[outCID][outCH].in_client_id = inCID;
    routing[outCID][outCH].in_channel = inCH;
    printf("Preconfigured: client%d out%d -> client%d in%d\n", outCID, outCH, inCID, inCH);
}

/*
 * monitor_mode()
 *
 * This function implements the new "monitor" command. It switches the terminal into non-canonical mode
 * to allow real-time key detection and polls all active client sockets to read fresh data.
 * Every cycle (with a refresh rate defined by MONITOR_FPS), it:
 *  - Checks STDIN for the quit key ('Q' or 'q').
 *  - Processes any new data from all active clients.
 *  - Clears the screen and displays a table of all connected clients and their 5 output channel messages.
 */
static void monitor_mode(void) {
    struct termios orig_termios, new_termios;
    // Save original terminal settings.
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        return;
    }
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1) {
        perror("tcsetattr");
        return;
    }

    printf("Entering monitor mode. Press 'Q' to quit.\n");
    fflush(stdout);

    bool quit = false;
    while (!quit) {
        // Build a file descriptor set for STDIN and all active client sockets.
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

        // Calculate timeout based on MONITOR_FPS.
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000000 / MONITOR_FPS; // e.g., 500000 us for 2 FPS

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            perror("select in monitor_mode");
            break;
        }

        // Process any new data from active client sockets.
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].sockfd, &readfds)) {
                handle_client_input(i);
            }
        }

        // Check STDIN for quit command.
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'Q' || ch == 'q') {
                    quit = true;
                }
            }
        }

        // Clear the screen and display the updated monitoring view.
        system("clear");
        printf("=== Monitoring Mode (press 'Q' to quit) ===\n\n");
        printf("%-10s | %-50s\n", "Client", "Output Channels (0..4)");
        printf("-------------------------------------------------------------\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                printf("client%-4d | ", clients[i].client_id);
                for (int ch = 0; ch < CHANNELS_PER_APP; ch++) {
                    printf("[%d]: %-10.10s ", ch, client_data[i].last_out[ch]);
                }
                printf("\n");
            }
        }
        fflush(stdout);
    }

    // Restore original terminal settings.
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios) == -1) {
        perror("tcsetattr");
    }
    printf("Exiting monitor mode.\n");
}
