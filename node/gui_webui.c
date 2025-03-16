/*
 * Single-file minimal HTTP server to control tmux windows on a Raspberry Pi.
 *
 * Design Principles:
 * - Single file: All functionality is implemented in one file.
 * - Plain C: Written in C11 using only standard libraries and POSIX APIs.
 * - Minimal HTTP server: Listens on port 8080 and serves a dynamic HTML page.
 * - tmux control: Three endpoints:
 *      /next       -> moves to the next window
 *      /prev       -> moves to the previous window
 *      /send_enter -> sends keys to tmux and then an Enter key so that the command is executed.
 *                     - If a pane is provided (after URL-decoding), that pane is used.
 *                     - Otherwise, the message is handled by the backend.
 * - Command History: Commands are logged (newest on top) for up to 1000 commands.
 *
 * Modification:
 * - setlocale is called to support UTF-8 characters (like ö, å, ä).
 * - stdout is set to unbuffered so output appears immediately.
 * - When the target pane is "server" (or no pane is provided), the message is printed
 *   to stdout with a timestamp (formatted as [day-month-year hour:minutes]) on the first line,
 *   then the message on the following line, and finally an empty line.
 *
 * Build:
 *   gcc -std=c11 -o webtmux server.c
 *
 * Usage:
 *   ./webtmux
 *
 * In your browser, navigate to:
 *   http://<your_ip>:8080
 *
 * Note:
 * - Ensure tmux is running with the socket /tmp/tmux_server.sock and at least one client is attached.
 * - For production, add proper error handling and security measures.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <locale.h>

extern void prettyprint(const char *message, unsigned int delay_ms);

#define PORT 8080
#define BUF_SIZE 4096
#define HISTORY_MAX 1000

#define BUFFER_SIZE 1024

// Structure to store a command record with its timestamp.
struct CommandRecord {
    time_t timestamp;
    char command[256];
};

// Global command history array and count.
static struct CommandRecord history[HISTORY_MAX];
static int historyCount = 0;

/*
 * add_history:
 * Adds a command record with the current timestamp.
 * If the history is full, the oldest record is dropped.
 */
void add_history(const char *cmd) {
    if (historyCount < HISTORY_MAX) {
        history[historyCount].timestamp = time(NULL);
        strncpy(history[historyCount].command, cmd, sizeof(history[historyCount].command)-1);
        history[historyCount].command[sizeof(history[historyCount].command)-1] = '\0';
        historyCount++;
    } else {
        for (int i = 1; i < HISTORY_MAX; i++) {
            history[i-1] = history[i];
        }
        history[HISTORY_MAX-1].timestamp = time(NULL);
        strncpy(history[HISTORY_MAX-1].command, cmd, sizeof(history[HISTORY_MAX-1].command)-1);
        history[HISTORY_MAX-1].command[sizeof(history[HISTORY_MAX-1].command)-1] = '\0';
    }
}

/*
 * url_decode:
 * Decodes a URL-encoded string.
 * Returns a newly allocated string that must be freed.
 */
char *url_decode(const char *src) {
    char *dest = malloc(strlen(src) + 1);
    if (!dest) return NULL;
    char *d = dest;
    while (*src) {
        if (*src == '%') {
            if (*(src+1) && *(src+2)) {
                char hex[3] = { *(src+1), *(src+2), '\0' };
                *d++ = (char)strtol(hex, NULL, 16);
                src += 3;
            } else {
                *d++ = *src++;
            }
        } else if (*src == '+') {
            *d++ = ' ';
            src++;
        } else {
            *d++ = *src++;
        }
    }
    *d = '\0';
    return dest;
}

/*
 * send_main_page:
 * Generates and sends the main HTML page with:
 *  - NEXT and PREVIOUS window buttons.
 *  - A form for sending commands (with ENTER) including a command and pane text boxes.
 *  - The command history (newest first).
 */
void send_main_page(int socket_fd) {
    size_t buf_size = 262144;
    char *html = malloc(buf_size);
    if (!html) {
        perror("malloc failed");
        return;
    }
    int len = snprintf(html, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"  // Ensure browser uses UTF-8
        "Connection: close\r\n\r\n"
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
            "<meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>tmux Controller</title>"
            "<link href=\"https://fonts.googleapis.com/css?family=Roboto:400,700&display=swap\" rel=\"stylesheet\">"
            "<style>"
                "body { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Roboto', sans-serif; margin: 0; padding: 20px; }"
                "h1, h2 { text-align: center; }"
                "form { margin-bottom: 20px; }"
                "input[type=text] {"
                    "width: 100%%;"
                    "padding: 15px;"
                    "margin: 10px 0;"
                    "box-sizing: border-box;"
                    "border: 2px solid #444;"
                    "border-radius: 4px;"
                    "background-color: #2e2e2e;"
                    "color: #d4d4d4;"
                    "font-size: 16px;"
                "}"
                "button {"
                    "width: 100%%;"
                    "padding: 15px;"
                    "font-size: 16px;"
                    "border: none;"
                    "border-radius: 4px;"
                    "background-color: #007acc;"
                    "color: white;"
                "}"
                "button:active { opacity: 0.8; }"
                "div.command { padding: 5px 0; border-bottom: 1px solid #444; }"
            "</style>"
        "</head>"
        "<body>"
            "<h1>tmux Controller</h1>"
            "<form action=\"/next\" method=\"get\">"
                "<button type=\"submit\">NEXT WINDOW</button>"
            "</form>"
            "<form action=\"/prev\" method=\"get\">"
                "<button type=\"submit\">PREVIOUS WINDOW</button>"
            "</form>"
            "<!-- Only the SEND with ENTER form remains -->"
            "<form action=\"/send_enter\" method=\"get\">"
                "<input type=\"text\" name=\"cmd\" placeholder=\"Type command or keys\">"
                "<input type=\"text\" name=\"pane\" placeholder=\"Specify pane (optional)\">"
                "<button type=\"submit\">SEND with ENTER</button>"
            "</form>"
            "<hr/>"
            "<h2>Command History</h2>"
    );
    
    // Append command history (newest first)
    for (int i = historyCount - 1; i >= 0; i--) {
        char timebuf[64];
        struct tm *tm_info = localtime(&history[i].timestamp);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
        int n = snprintf(html + len, buf_size - len,
                         "<div class=\"command\">[%s] %s</div>\n", timebuf, history[i].command);
        if (n < 0 || (size_t)n >= buf_size - len)
            break;
        len += n;
    }
    
    snprintf(html + len, buf_size - len, "</body></html>");
    send(socket_fd, html, strlen(html), 0);
    free(html);
}

/*
 * process_request:
 * Parses the HTTP request and performs the appropriate tmux command:
 *  - /next: switches to the next window.
 *  - /prev: switches to the previous window.
 *  - /send_enter: sends the command (and an Enter key) either to a specified pane
 *                 or (if no pane is provided) to the backend.
 *                 When handled by the backend, the message is printed to stdout
 *                 with a timestamp and empty lines.
 */
void process_request(int socket_fd) {
    char buffer[BUF_SIZE] = {0};
    int valread = read(socket_fd, buffer, BUF_SIZE-1);
    if (valread <= 0) {
        perror("read failed");
        return;
    }
    buffer[valread] = '\0';
    
    int action = 0; // 0: main page, 1: next, 2: prev, 3: send with ENTER
    char cmd_param[256] = {0};
    char pane_param[32] = {0};
    
    if (strstr(buffer, "GET /next") != NULL) {
        action = 1;
    } else if (strstr(buffer, "GET /prev") != NULL) {
        action = 2;
    } else if (strstr(buffer, "GET /send_enter?") != NULL) {
        action = 3;
    }
    
    char system_cmd[512] = {0};
    int ret;
    
    if (action == 1) {
        const char *cmd = "tmux -S /tmp/tmux_server.sock next-window -t server";
        ret = system(cmd);
        if (ret == -1)
            perror("system call failed for /next");
        add_history(cmd);
    } else if (action == 2) {
        const char *cmd = "tmux -S /tmp/tmux_server.sock previous-window -t server";
        ret = system(cmd);
        if (ret == -1)
            perror("system call failed for /prev");
        add_history(cmd);
    } else if (action == 3) {
        // Extract cmd parameter (stop at space or '&')
        char *p = strstr(buffer, "cmd=");
        if (p) {
            p += strlen("cmd=");
            char *end = p;
            while (*end && *end != ' ' && *end != '&')
                end++;
            size_t cmd_len = end - p;
            if (cmd_len < sizeof(cmd_param)) {
                strncpy(cmd_param, p, cmd_len);
                cmd_param[cmd_len] = '\0';
            }
        }
        // Extract optional pane parameter (stop at space or '&')
        char *q = strstr(buffer, "pane=");
        if (q) {
            q += strlen("pane=");
            char *end = q;
            while (*end && *end != ' ' && *end != '&')
                end++;
            size_t pane_len = end - q;
            if (pane_len < sizeof(pane_param)) {
                strncpy(pane_param, q, pane_len);
                pane_param[pane_len] = '\0';
            }
        }
        
        char *decoded_cmd = url_decode(cmd_param);
        if (decoded_cmd) {
            char *decoded_pane = NULL;
            if (strlen(pane_param) > 0)
                decoded_pane = url_decode(pane_param);
            
            // If no pane was explicitly provided, force the target to be "server"
            char target_pane[32] = {0};
            if (decoded_pane && strlen(decoded_pane) > 0) {
                strncpy(target_pane, decoded_pane, sizeof(target_pane)-1);
            } else {
                strncpy(target_pane, "server", sizeof(target_pane)-1);
            }
            
            /*
             * If the target pane is "server", print the message to stdout with:
             * - A timestamp on the first line in the format [day-month-year hour:minutes],
             * - Followed immediately by the message on the next line,
             * - And then an empty line.
             */
            if (strcmp(target_pane, "server") == 0) {
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                char timebuf[32];
                strftime(timebuf, sizeof(timebuf), "[%d-%m-%Y %H:%M]", tm_info);
				char buffer[BUFFER_SIZE];
		        snprintf(buffer, BUFFER_SIZE, "%s\n%s\n\n", timebuf, decoded_cmd);
		        prettyprint(buffer, 15);
                fflush(stdout);
                add_history(decoded_cmd);
            } else {
                // Send the command as literal text to the determined pane.
                snprintf(system_cmd, sizeof(system_cmd),
                         "tmux -S /tmp/tmux_server.sock send-keys -t %s -l '%s'",
                         target_pane, decoded_cmd);
                ret = system(system_cmd);
                if (ret == -1)
                    perror("system call failed for /send_enter");
                add_history(system_cmd);
                
                // Send an extra Enter key to execute the command.
                snprintf(system_cmd, sizeof(system_cmd),
                         "tmux -S /tmp/tmux_server.sock send-keys -t %s Enter",
                         target_pane);
                ret = system(system_cmd);
                if (ret == -1)
                    perror("system call failed when sending Enter key");
                add_history(system_cmd);
            }
            
            free(decoded_cmd);
            if (decoded_pane)
                free(decoded_pane);
        }
    }
    
    send_main_page(socket_fd);
}

int main(void) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Set locale to support UTF-8 characters (like ö, å, ä)
    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "Warning: Unable to set locale.\n");
    }
    // Set stdout to be unbuffered so output appears immediately.
    setvbuf(stdout, NULL, _IONBF, 0);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 3) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server running on port %d\n", PORT);
    
    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept failed");
            continue;
        }
        process_request(new_socket);
        close(new_socket);
    }
    
    close(server_fd);
    return 0;
}
