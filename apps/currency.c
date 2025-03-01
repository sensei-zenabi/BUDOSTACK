/*
    This C application retrieves and displays all available currencies (with their code, name, 
    and exchange rate relative to EUR) in a sorted grid that fills the terminal screen. The user may:
      - Press '9' for the next page,
      - Press '8' for the previous page,
      - Press 'U' (or 'u') to update the data from the API,
      - Press '0' to exit.
      
    Design principles:
      - Plain C using -std=c11.
      - Single source file (no header files).
      - Only standard cross-platform libraries (and native sockets).
      - ANSI escape codes are used for screen clearing and UI.
      - Terminal non-canonical mode is enabled (using termios on POSIX and conio.h on Windows)
        so that key presses are processed without waiting for Enter.
      - Dynamic allocation is used for storing currency records.
      - Rudimentary JSON parsing is performed by searching for expected key patterns.
      
    Note: The JSON from floatrates.com is assumed to be in a known format.
*/

// Define POSIX version to ensure proper definitions for getaddrinfo and related functions
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <conio.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>      // For getaddrinfo, freeaddrinfo, and struct addrinfo
    #include <unistd.h>
    #include <termios.h>
    #include <sys/ioctl.h>
    typedef int socket_t;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

// Structure to store currency data
typedef struct {
    char code[16];
    char name[64];
    double rate;
} Currency;

// Dynamic array to hold currencies
typedef struct {
    Currency *items;
    size_t count;
    size_t capacity;
} CurrencyArray;

// Initialize the dynamic array
void init_currency_array(CurrencyArray *arr) {
    arr->count = 0;
    arr->capacity = 32;
    arr->items = malloc(arr->capacity * sizeof(Currency));
    if (!arr->items) {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }
}

// Append an item to the dynamic array
void append_currency(CurrencyArray *arr, const Currency *c) {
    if (arr->count == arr->capacity) {
        arr->capacity *= 2;
        Currency *temp = realloc(arr->items, arr->capacity * sizeof(Currency));
        if (!temp) {
            fprintf(stderr, "Memory allocation error\n");
            free(arr->items);
            exit(EXIT_FAILURE);
        }
        arr->items = temp;
    }
    arr->items[arr->count++] = *c;
}

// Free the dynamic array
void free_currency_array(CurrencyArray *arr) {
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

#ifdef _WIN32
// Windows: use _getch() for immediate key input
int getch_custom(void) {
    return _getch();
}
#else
// POSIX: use termios to disable canonical mode and echo
static struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int getch_custom(void) {
    int c;
    c = getchar();
    return c;
}
#endif

// Function to initialize sockets (Windows requires WSAStartup)
int init_sockets(void) {
#ifdef _WIN32
    WSADATA wsa;
    return (WSAStartup(MAKEWORD(2,2), &wsa) == 0);
#else
    return 1;
#endif
}

// Function to cleanup sockets (Windows requires WSACleanup)
void cleanup_sockets(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Cross-platform sleep for seconds (only used optionally)
void sleep_sec(int sec) {
#ifdef _WIN32
    Sleep(sec * 1000);
#else
    sleep(sec);
#endif
}

// Create a TCP connection to the specified hostname and port
socket_t create_connection(const char *hostname, const char *port) {
    struct addrinfo hints, *res, *p;
    socket_t sockfd = INVALID_SOCKET;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    if (getaddrinfo(hostname, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return INVALID_SOCKET;
    }
    
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = (socket_t)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == INVALID_SOCKET)
            continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == SOCKET_ERROR) {
#ifdef _WIN32
            closesocket(sockfd);
#else
            close(sockfd);
#endif
            sockfd = INVALID_SOCKET;
            continue;
        }
        break; // Successfully connected
    }
    
    freeaddrinfo(res);
    return sockfd;
}

// Send an HTTP GET request to the API endpoint
int send_http_request(socket_t sockfd, const char *hostname, const char *path) {
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: C-ExchangeRate-App\r\n"
             "Connection: close\r\n\r\n",
             path, hostname);
    
    size_t len = strlen(request);
    if (send(sockfd, request, (int)len, 0) == SOCKET_ERROR) {
        perror("send");
        return 0;
    }
    return 1;
}

// Receive the complete HTTP response
char *receive_response(socket_t sockfd) {
    size_t buffer_size = 8192;
    size_t total_received = 0;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        fprintf(stderr, "Memory allocation error\n");
        return NULL;
    }
    
    while (1) {
        if (total_received + 1024 > buffer_size) {
            buffer_size *= 2;
            char *temp = realloc(buffer, buffer_size);
            if (!temp) {
                fprintf(stderr, "Memory allocation error\n");
                free(buffer);
                return NULL;
            }
            buffer = temp;
        }
        int received = recv(sockfd, buffer + total_received, 1024, 0);
        if (received == SOCKET_ERROR) {
            perror("recv");
            free(buffer);
            return NULL;
        }
        if (received == 0) {  // Connection closed
            break;
        }
        total_received += received;
    }
    
    buffer[total_received] = '\0';
    return buffer;
}

/*
    Rudimentary JSON parsing:
    We search for each occurrence of "code" (which starts a currency object) and then,
    within the same JSON object (delimited by '}'), we extract:
       - "code": string value
       - "name": string value (if available)
       - "rate": a double value
*/
void parse_json_currencies(const char *json, CurrencyArray *arr) {
    const char *p = json;
    // Skip HTTP header if any
    const char *body = strstr(p, "\r\n\r\n");
    if (body)
        body += 4;
    else
        body = json;
    
    while ((p = strstr(body, "\"code\"")) != NULL) {
        Currency cur = {0};
        const char *start, *end, *obj_end;
        
        // Extract code value
        start = strchr(p, ':');
        if (!start) break;
        start++; // skip colon
        while (*start && isspace((unsigned char)*start)) start++;
        if (*start != '\"') { body = p + 1; continue; }
        start++; // skip opening quote
        end = strchr(start, '\"');
        if (!end) break;
        size_t len = end - start;
        if (len >= sizeof(cur.code))
            len = sizeof(cur.code) - 1;
        strncpy(cur.code, start, len);
        cur.code[len] = '\0';
        
        // Find the end of this currency object (assume it ends with '}')
        obj_end = strchr(p, '}');
        if (!obj_end) break;
        
        // Extract name value within the same object
        const char *name_key = strstr(p, "\"name\"");
        if (name_key && name_key < obj_end) {
            start = strchr(name_key, ':');
            if (start) {
                start++; // skip colon
                while (*start && isspace((unsigned char)*start)) start++;
                if (*start == '\"') {
                    start++; // skip opening quote
                    end = strchr(start, '\"');
                    if (end) {
                        len = end - start;
                        if (len >= sizeof(cur.name))
                            len = sizeof(cur.name) - 1;
                        strncpy(cur.name, start, len);
                        cur.name[len] = '\0';
                    }
                }
            }
        }
        
        // Extract rate value within the same object
        const char *rate_key = strstr(p, "\"rate\"");
        if (rate_key && rate_key < obj_end) {
            start = strchr(rate_key, ':');
            if (start) {
                start++; // skip colon
                while (*start && isspace((unsigned char)*start)) start++;
                cur.rate = atof(start);
            }
        }
        
        append_currency(arr, &cur);
        body = obj_end + 1;
    }
}

// Comparison function for qsort (sort by code)
int cmp_currency(const void *a, const void *b) {
    const Currency *ca = a;
    const Currency *cb = b;
    return strcmp(ca->code, cb->code);
}

/*
   Get terminal dimensions.
   On POSIX, we use ioctl() with TIOCGWINSZ to get the actual terminal size.
   On Windows, we fall back to using environment variables.
*/
void get_terminal_size(int *cols, int *rows) {
#ifdef _WIN32
    const char *env_cols = getenv("COLUMNS");
    const char *env_rows = getenv("LINES");
    *cols = (env_cols) ? atoi(env_cols) : 80;
    *rows = (env_rows) ? atoi(env_rows) : 24;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        const char *env_cols = getenv("COLUMNS");
        const char *env_rows = getenv("LINES");
        *cols = (env_cols) ? atoi(env_cols) : 80;
        *rows = (env_rows) ? atoi(env_rows) : 24;
    }
#endif
    if (*cols < 40) *cols = 40;
    if (*rows < 10) *rows = 10;
}

// Clear screen using ANSI escape sequences
void clear_screen(void) {
    printf("\033[H\033[J");
}

// Display one page of currencies in a grid layout that fills the terminal.
// The grid fills every row (except the last reserved for the menu).
// If there are not enough items, blank cells are printed.
void display_page(Currency *currencies, size_t total, size_t page, int term_cols, int term_rows) {
    clear_screen();
    
    // Fixed field widths:
    //  code: 3, name: 20, rate: 8 => total = 33 characters per cell.
    // Add one extra space between cells for separation => effective cell width = 34.
    const int cell_width = 33;
    const int effective_width = 34;
    int num_cols = term_cols / effective_width;
    if (num_cols < 1)
        num_cols = 1;
    int grid_rows = term_rows - 1; // reserve last row for menu/instructions
    size_t items_per_page = num_cols * grid_rows;
    size_t total_pages = (total + items_per_page - 1) / items_per_page;
    if (page >= total_pages)
        page = total_pages - 1;
    size_t start_index = page * items_per_page;
    
    // Print grid row by row in column-major order
    for (int i = 0; i < grid_rows; i++) {
        for (int j = 0; j < num_cols; j++) {
            size_t idx = start_index + j * grid_rows + i;
            char cell[cell_width + 1]; // buffer for fixed-width cell (plus null terminator)
            if (idx < total) {
                Currency *c = &currencies[idx];
                // Build a cell using fixed width formatting
                snprintf(cell, sizeof(cell), "%-3.3s %-20.20s %8.4f", c->code, c->name, c->rate);
            } else {
                // Empty cell if no record exists
                snprintf(cell, sizeof(cell), "%-*s", cell_width, "");
            }
            // Print the cell and then a space as a separator
            printf("%-33s ", cell);
        }
        printf("\n");
    }
    
    // Print menu/instructions in the last row (using inverse video)
    printf("\033[7m"); 
    printf("Page %zu/%zu: 8: Prev  9: Next  U: Update  0: Exit", page + 1, total_pages);
    int menu_len = (int)strlen("Page X/X: 8: Prev  9: Next  U: Update  0: Exit");
    for (int i = menu_len; i < term_cols; i++) {
        putchar(' ');
    }
    printf("\033[0m\n");
}

// Function to fetch and parse currencies from the API.
// Returns a dynamically allocated CurrencyArray.
CurrencyArray fetch_currencies(void) {
    const char *hostname = "www.floatrates.com";
    const char *port = "80";
    const char *path = "/daily/eur.json";
    
    CurrencyArray arr;
    init_currency_array(&arr);
    
    if (!init_sockets()) {
        fprintf(stderr, "Socket initialization failed\n");
        exit(EXIT_FAILURE);
    }
    
    socket_t sockfd = create_connection(hostname, port);
    if (sockfd == INVALID_SOCKET) {
        fprintf(stderr, "Could not connect to %s:%s\n", hostname, port);
        cleanup_sockets();
        exit(EXIT_FAILURE);
    }
    
    if (!send_http_request(sockfd, hostname, path)) {
        fprintf(stderr, "Failed to send HTTP request\n");
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        cleanup_sockets();
        exit(EXIT_FAILURE);
    }
    
    char *response = receive_response(sockfd);
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    cleanup_sockets();
    
    if (!response) {
        fprintf(stderr, "Failed to receive response\n");
        exit(EXIT_FAILURE);
    }
    
    parse_json_currencies(response, &arr);
    free(response);
    
    // Sort currencies by code
    qsort(arr.items, arr.count, sizeof(Currency), cmp_currency);
    return arr;
}

int main(void) {
#ifndef _WIN32
    enable_raw_mode();
#endif

    CurrencyArray arr = fetch_currencies();
    
    int term_cols, term_rows;
    get_terminal_size(&term_cols, &term_rows);
    
    // Use effective cell width (34 characters) to compute layout.
    const int effective_width = 34;
    int num_cols = term_cols / effective_width;
    if (num_cols < 1)
        num_cols = 1;
    int grid_rows = term_rows - 1; // reserving last row for menu/instructions
    size_t items_per_page = num_cols * grid_rows;
    size_t total_pages = (arr.count + items_per_page - 1) / items_per_page;
    
    size_t current_page = 0;
    int choice;
    do {
        display_page(arr.items, arr.count, current_page, term_cols, term_rows);
        choice = getch_custom();
        if (choice == '9' && current_page < total_pages - 1) {
            current_page++;
        } else if (choice == '8' && current_page > 0) {
            current_page--;
        } else if (choice == 'U' || choice == 'u') {
            free_currency_array(&arr);
            arr = fetch_currencies();
            total_pages = (arr.count + items_per_page - 1) / items_per_page;
            current_page = 0;
        }
    } while (choice != '0');
    
    free_currency_array(&arr);
    return EXIT_SUCCESS;
}
