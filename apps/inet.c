#define _POSIX_C_SOURCE 200809L  // Enable POSIX features, e.g. popen(), pclose()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>         // For timestamp display
#include <termios.h>      // For terminal control
#include <sys/select.h>   // For select()
#include <signal.h>       // For signal handling

#define MAX_INPUT 100
#define MAX_INTERFACES 32
#define MAX_BAR_LEN 40  // Maximum characters for histogram bars

// Table widths for proper alignment
#define TABLE1_WIDTH 100  // Main statistics table width
#define TABLE2_WIDTH 84   // Additional metrics table width

// Helper function to print a separator line with a given width.
void print_separator(int width) {
    for (int i = 0; i < width; i++) {
         putchar('-');
    }
    putchar('\n');
}

static void trim_trailing_whitespace(char *value) {
    if (value == NULL) {
        return;
    }
    size_t len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1])) {
        value[len - 1] = '\0';
        len--;
    }
}

static int confirm_action(const char *prompt) {
    char response[8];
    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return 0;
    }
    return response[0] == 'y' || response[0] == 'Y';
}

static int read_first_line(const char *path, char *buffer, size_t size) {
    if (path == NULL || buffer == NULL || size == 0) {
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(buffer, (int)size, fp) == NULL) {
        fclose(fp);
        buffer[0] = '\0';
        return -1;
    }
    fclose(fp);
    trim_trailing_whitespace(buffer);
    return 0;
}

static int check_iw(void) {
    return system("command -v iw > /dev/null 2>&1");
}

static void print_compliance_banner(void) {
    printf("This tool only inspects your own device statistics.\n");
    printf("When using it in libraries or other shared spaces, obtain permission before running scans or pings.\n");
    printf("All optional active probes now require confirmation so you can respect venue policies.\n\n");
}

static void show_sysfs_info(const char *iface) {
    char path[128];
    char value[128];

    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);
    if (read_first_line(path, value, sizeof(value)) == 0) {
        printf("  Link state : %s\n", value);
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", iface);
    if (read_first_line(path, value, sizeof(value)) == 0) {
        if (strcmp(value, "1") == 0) {
            printf("  Carrier    : detected\n");
        } else if (strcmp(value, "0") == 0) {
            printf("  Carrier    : not detected\n");
        } else {
            printf("  Carrier    : %s\n", value);
        }
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    if (read_first_line(path, value, sizeof(value)) == 0) {
        printf("  MAC addr   : %s\n", value);
    }
}

static int read_nmcli_value(const char *device, const char *field, char *out, size_t out_size) {
    char command[256];
    if (snprintf(command, sizeof(command), "nmcli -g %s device show %s 2>/dev/null", field, device) >= (int)sizeof(command)) {
        return -1;
    }
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(out, (int)out_size, fp) == NULL) {
        pclose(fp);
        out[0] = '\0';
        return -1;
    }
    pclose(fp);
    trim_trailing_whitespace(out);
    return out[0] == '\0' ? -1 : 0;
}

static void show_wireless_info(const char *iface) {
    if (check_iw() != 0) {
        printf("  Wireless link info unavailable (iw not installed).\n");
        return;
    }
    char command[256];
    if (snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null", iface) >= (int)sizeof(command)) {
        return;
    }
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return;
    }
    char line[256];
    bool printed_header = false;
    while (fgets(line, sizeof(line), fp)) {
        trim_trailing_whitespace(line);
        if (line[0] == '\0') {
            continue;
        }
        if (!printed_header) {
            printf("  Wireless link info:\n");
            printed_header = true;
        }
        printf("    %s\n", line);
    }
    pclose(fp);
    if (!printed_header) {
        printf("  Wireless link info: interface is not associated.\n");
    }
}

static void show_nmcli_device_details(const char *iface) {
    char state[128];
    char connection[256];
    char address[256];
    char gateway[128];
    char dns[256];

    if (read_nmcli_value(iface, "GENERAL.STATE", state, sizeof(state)) != 0) {
        snprintf(state, sizeof(state), "Unavailable");
    }
    if (read_nmcli_value(iface, "GENERAL.CONNECTION", connection, sizeof(connection)) != 0) {
        snprintf(connection, sizeof(connection), "None");
    }
    if (read_nmcli_value(iface, "IP4.ADDRESS", address, sizeof(address)) != 0) {
        snprintf(address, sizeof(address), "None");
    }
    if (read_nmcli_value(iface, "IP4.GATEWAY", gateway, sizeof(gateway)) != 0) {
        snprintf(gateway, sizeof(gateway), "None");
    }
    if (read_nmcli_value(iface, "IP4.DNS", dns, sizeof(dns)) != 0) {
        snprintf(dns, sizeof(dns), "None");
    }

    printf("  NM state   : %s\n", state);
    printf("  Connection : %s\n", connection);
    printf("  IPv4 addr  : %s\n", address);
    printf("  Gateway    : %s\n", gateway);
    printf("  DNS        : %s\n", dns);
}

int check_nmcli(void);

static void show_connection_details(void) {
    if (check_nmcli() != 0) {
        printf("'nmcli' is required to show connection details.\n");
        return;
    }

    FILE *fp = popen("nmcli -t -f DEVICE,TYPE,STATE device status 2>/dev/null", "r");
    if (fp == NULL) {
        printf("Unable to query device status.\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        trim_trailing_whitespace(line);
        if (line[0] == '\0') {
            continue;
        }
        char *token = strtok(line, ":");
        if (token == NULL) {
            continue;
        }
        char device[64];
        snprintf(device, sizeof(device), "%s", token);

        token = strtok(NULL, ":");
        if (token == NULL) {
            continue;
        }
        char type[64];
        snprintf(type, sizeof(type), "%s", token);

        token = strtok(NULL, ":");
        if (token == NULL) {
            continue;
        }
        char state[64];
        snprintf(state, sizeof(state), "%s", token);

        printf("\nInterface: %s (%s)\n", device, type);
        printf("  Reported state: %s\n", state);
        show_sysfs_info(device);
        show_nmcli_device_details(device);

        if (strcasecmp(type, "wifi") == 0 || strcasecmp(type, "802-11-wireless") == 0) {
            show_wireless_info(device);
        }
    }

    pclose(fp);
    printf("\nReminder: These details are limited to your own interfaces and do not inspect other patrons' traffic.\n");
}

// Structure to store network interface statistics from /proc/net/dev
typedef struct {
    char name[32];
    unsigned long rx_bytes;
    unsigned long rx_packets;
    unsigned long rx_errs;
    unsigned long rx_drop;
    unsigned long tx_bytes;
    unsigned long tx_packets;
    unsigned long tx_errs;
    unsigned long tx_drop;
} NetDevStats;

/*
 * read_netdev_stats
 *
 * Reads /proc/net/dev to fill an array of NetDevStats structures.
 * Returns the number of interfaces read, or -1 on error.
 */
int read_netdev_stats(NetDevStats stats[], int max) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }
    char line[256];
    int count = 0;
    int line_num = 0;
    
    // Skip the first two header lines.
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (line_num <= 2)
            continue;
        if (count >= max)
            break;
        
        // Expected format from /proc/net/dev:
        // iface:  rx_bytes rx_packets rx_errs rx_drop ... tx_bytes tx_packets tx_errs tx_drop ...
        char iface[32];
        unsigned long r_bytes, r_packets, r_errs, r_drop;
        unsigned long dummy1, dummy2, dummy3, dummy4;
        unsigned long t_bytes, t_packets, t_errs, t_drop;
        int matched = sscanf(line,
                             " %31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                             iface,
                             &r_bytes, &r_packets, &r_errs, &r_drop,
                             &dummy1, &dummy2, &dummy3, &dummy4,
                             &t_bytes, &t_packets, &t_errs, &t_drop);
        if (matched == 13) {
            snprintf(stats[count].name, sizeof(stats[count].name), "%s", iface);
            stats[count].rx_bytes   = r_bytes;
            stats[count].rx_packets = r_packets;
            stats[count].rx_errs    = r_errs;
            stats[count].rx_drop    = r_drop;
            stats[count].tx_bytes   = t_bytes;
            stats[count].tx_packets = t_packets;
            stats[count].tx_errs    = t_errs;
            stats[count].tx_drop    = t_drop;
            count++;
        }
    }
    fclose(fp);
    return count;
}

/*
 * check_nmcli
 *
 * Checks if the 'nmcli' tool is available on the system.
 * Returns 0 if found, nonzero otherwise.
 */
int check_nmcli(void) {
    return system("command -v nmcli > /dev/null 2>&1");
}

/*
 * check_adapters
 *
 * Checks if any network adapters are detected using 'nmcli device status'.
 * Returns 0 if adapters are detected, -1 otherwise.
 */
int check_adapters(void) {
    FILE *fp = popen("nmcli device status", "r");
    if (fp == NULL) {
        return -1;
    }
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
         count++;
    }
    pclose(fp);
    return (count < 2) ? -1 : 0;
}

/*
 * check_connectivity
 *
 * Tests internet connectivity by pinging the provided host.
 * Returns 0 if successful, nonzero otherwise.
 */
int check_connectivity(const char *host) {
    if (host == NULL || host[0] == '\0') {
        return -1;
    }
    char command[256];
    if (snprintf(command, sizeof(command), "ping -c 1 %s > /dev/null 2>&1", host) >= (int)sizeof(command)) {
        return -1;
    }
    return system(command);
}

/*
 * run_diagnostics
 *
 * Provides fallback checks to help the user understand what might be missing.
 */
void run_diagnostics(void) {
    printf("\n--- Running Diagnostics ---\n");
    
    // Check for nmcli
    if (check_nmcli() == 0) {
         printf("Diagnostic: 'nmcli' is available.\n");
    } else {
         printf("Diagnostic: 'nmcli' not found. Please install NetworkManager.\n");
    }
    
    // Check for network adapters
    if (check_adapters() == 0) {
         printf("Diagnostic: Network adapters detected.\n");
    } else {
         printf("Diagnostic: No network adapters detected. Check drivers or physical connections.\n");
    }
    
    if (confirm_action("Run a single ping test (y/N)? ")) {
         char host[MAX_INPUT];
         printf("Enter host to ping (default 8.8.8.8): ");
         if (fgets(host, sizeof(host), stdin) == NULL) {
             host[0] = '\0';
         }
         trim_trailing_whitespace(host);
         if (host[0] == '\0') {
             snprintf(host, sizeof(host), "8.8.8.8");
         }

         if (check_connectivity(host) == 0) {
             printf("Diagnostic: Internet connectivity is working (ping to %s successful).\n", host);
         } else {
             printf("Diagnostic: Internet connectivity test to %s failed. Check your network connection and venue policies.\n", host);
         }
    } else {
         printf("Diagnostic: Connectivity test skipped to avoid unsolicited traffic.\n");
    }
    
    printf("--- End of Diagnostics ---\n");
}

/*
 * monitor_mode
 *
 * Enhanced monitoring mode:
 *  - Accepts a user-specified refresh interval.
 *  - Uses ANSI escape codes to clear the screen.
 *  - Displays a timestamp for each update.
 *  - Shows detailed additional metrics per interface.
 *  - Each interface’s histogram bar is scaled based on its own maximum observed throughput.
 *  - Horizontal separator lines are printed to match the table widths.
 *  - Optionally logs snapshots to a CSV file when provided.
 *  - Exits when the user presses the plain 'q' key.
 *
 * Design rationale:
 * We configure the terminal in non-canonical mode and use select() to wait for user input or a timeout.
 * Pressing the 'q' key will break out of the monitoring loop.
 */
void monitor_mode(int interval, FILE *log_fp) {
    if (interval <= 0) {
        interval = 1;  // Ensure a valid interval
    }

    // Save current terminal settings and switch to non-canonical mode.
    struct termios old_tio, new_tio;
    if (tcgetattr(STDIN_FILENO, &old_tio) == -1) {
        perror("tcgetattr");
        return;
    }
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_tio) == -1) {
        perror("tcsetattr");
        return;
    }
    
    // Ignore SIGINT so that CTRL+C does not interrupt the monitoring mode.
    struct sigaction old_sa, new_sa;
    new_sa.sa_handler = SIG_IGN;
    sigemptyset(&new_sa.sa_mask);
    new_sa.sa_flags = 0;
    sigaction(SIGINT, &new_sa, &old_sa);
    
    // Structure to track maximum throughput for each interface.
    typedef struct {
        char name[32];
        unsigned long max_rx;
        unsigned long max_tx;
    } MaxStats;
    
    MaxStats max_stats[MAX_INTERFACES] = {0};
    int max_stats_count = 0;
    int header_written = 0;

    NetDevStats prev[MAX_INTERFACES] = {0}, curr[MAX_INTERFACES] = {0};
    unsigned long rx_rates[MAX_INTERFACES] = {0};
    unsigned long tx_rates[MAX_INTERFACES] = {0};

    int prev_count = read_netdev_stats(prev, MAX_INTERFACES);
    if (prev_count < 0) {
         printf("Error: Unable to read network statistics.\n");
         goto cleanup;
    }

    if (log_fp != NULL) {
         printf("Passive CSV logging is active. Measurements are stored locally only.\n");
    }

    while (1) {
         // Use select() to wait for either input or the refresh timeout.
         fd_set readfds;
         FD_ZERO(&readfds);
         FD_SET(STDIN_FILENO, &readfds);
         struct timeval tv;
         tv.tv_sec = interval;
         tv.tv_usec = 0;
         int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
         if (ret < 0) {
             perror("select");
             break;
         }
         if (ret > 0) {
             char ch;
             if (read(STDIN_FILENO, &ch, 1) > 0 && ch == 'q') { // plain 'q'
                 break;
             }
         }
         
         // Read current network statistics.
         int curr_count = read_netdev_stats(curr, MAX_INTERFACES);
         if (curr_count < 0) {
              printf("Error: Unable to read network statistics.\n");
              break;
         }
         
         // Compute per-second throughput rates and update per-interface maximums.
         for (int i = 0; i < curr_count; i++) {
             int found_prev = 0;
             for (int j = 0; j < prev_count; j++) {
                 if (strcmp(curr[i].name, prev[j].name) == 0) {
                     found_prev = 1;
                     rx_rates[i] = (curr[i].rx_bytes - prev[j].rx_bytes) / interval;
                     tx_rates[i] = (curr[i].tx_bytes - prev[j].tx_bytes) / interval;
                     break;
                 }
             }
             if (!found_prev) {
                rx_rates[i] = 0;
                tx_rates[i] = 0;
             }
             
             // Update per-interface maximum stats.
             int found_max = 0;
             for (int k = 0; k < max_stats_count; k++) {
                 if (strcmp(curr[i].name, max_stats[k].name) == 0) {
                     found_max = 1;
                     if (rx_rates[i] > max_stats[k].max_rx)
                         max_stats[k].max_rx = rx_rates[i];
                     if (tx_rates[i] > max_stats[k].max_tx)
                         max_stats[k].max_tx = tx_rates[i];
                     break;
                 }
             }
             if (!found_max) {
                 size_t nlen = strnlen(curr[i].name, sizeof(max_stats[max_stats_count].name) - 1);
                 memcpy(max_stats[max_stats_count].name, curr[i].name, nlen);
                 max_stats[max_stats_count].name[nlen] = '\0';
                 max_stats[max_stats_count].max_rx = rx_rates[i];
                 max_stats[max_stats_count].max_tx = tx_rates[i];
                 max_stats_count++;
             }
         }
         
         // Clear the terminal using ANSI escape codes.
         printf("\033[H\033[J");
         
         // Display current timestamp.
         time_t now = time(NULL);
         printf("Updated: %s", ctime(&now)); // ctime() adds a newline

         printf("Network Monitoring Mode (refresh every %d second(s)). Press 'q' to exit.\n", interval);
         print_separator(TABLE1_WIDTH);

         // Main table header.
         printf("%-6s %8s %8s %10s %10s %8s %8s %8s %8s %8s %8s\n",
                "IFACE", "RX/s", "TX/s", "RXTot", "TXTOT", "RXpkts", "TXpkts",
                "RXerr", "TXerr", "RXdp", "TXdp");
         print_separator(TABLE1_WIDTH);
         
         // Print interface statistics.
         for (int i = 0; i < curr_count; i++) {
             int found_prev = 0;
             for (int j = 0; j < prev_count; j++) {
                 if (strcmp(curr[i].name, prev[j].name) == 0) {
                     found_prev = 1;
                     break;
                 }
             }
             if (!found_prev) {
                printf("%-6s %8s %8s %10lu %10lu %8lu %8lu %8lu %8lu %8lu %8lu\n",
                       curr[i].name, "N/A", "N/A",
                       curr[i].rx_bytes, curr[i].tx_bytes,
                       curr[i].rx_packets, curr[i].tx_packets,
                       curr[i].rx_errs, curr[i].tx_errs,
                       curr[i].rx_drop, curr[i].tx_drop);
             } else {
                printf("%-6s %8lu %8lu %10lu %10lu %8lu %8lu %8lu %8lu %8lu %8lu\n",
                       curr[i].name, rx_rates[i], tx_rates[i],
                       curr[i].rx_bytes, curr[i].tx_bytes,
                       curr[i].rx_packets, curr[i].tx_packets,
                       curr[i].rx_errs, curr[i].tx_errs,
                       curr[i].rx_drop, curr[i].tx_drop);
             }
         }
         
         // Print bar view for RX throughput per interface.
         printf("\nMeasured RX Throughput (bytes/sec):\n");
         for (int i = 0; i < curr_count; i++) {
             unsigned long iface_max_rx = 0;
             for (int k = 0; k < max_stats_count; k++) {
                 if (strcmp(curr[i].name, max_stats[k].name) == 0) {
                     iface_max_rx = max_stats[k].max_rx;
                     break;
                 }
             }
             int bar_len = 0;
             if (iface_max_rx > 0) {
                 bar_len = (int)((rx_rates[i] * MAX_BAR_LEN) / iface_max_rx);
                 if (bar_len > MAX_BAR_LEN)
                     bar_len = MAX_BAR_LEN;
             }
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < MAX_BAR_LEN; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", rx_rates[i]);
         }
         
         // Print bar view for TX throughput per interface.
         printf("\nMeasured TX Throughput (bytes/sec):\n");
         for (int i = 0; i < curr_count; i++) {
             unsigned long iface_max_tx = 0;
             for (int k = 0; k < max_stats_count; k++) {
                 if (strcmp(curr[i].name, max_stats[k].name) == 0) {
                     iface_max_tx = max_stats[k].max_tx;
                     break;
                 }
             }
             int bar_len = 0;
             if (iface_max_tx > 0) {
                 bar_len = (int)((tx_rates[i] * MAX_BAR_LEN) / iface_max_tx);
                 if (bar_len > MAX_BAR_LEN)
                     bar_len = MAX_BAR_LEN;
             }
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < MAX_BAR_LEN; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", tx_rates[i]);
         }
         
         // --- Additional Metrics Section ---
         printf("\nAdditional Metrics (per-second differences):\n");
         printf("%-6s %12s %12s %12s %12s %12s %12s\n",
                "IFACE", "RX_pkts/s", "TX_pkts/s", "RX_err%%", "TX_err%%", "RX_dp%%", "TX_dp%%");
         print_separator(TABLE2_WIDTH);
         
         unsigned long total_delta_rx_pkts = 0, total_delta_tx_pkts = 0;
         unsigned long total_delta_rx_err = 0, total_delta_tx_err = 0;
         unsigned long total_delta_rx_dp = 0, total_delta_tx_dp = 0;
         int metrics_count = 0;
         
         for (int i = 0; i < curr_count; i++) {
             int found = 0;
             for (int j = 0; j < prev_count; j++) {
                 if (strcmp(curr[i].name, prev[j].name) == 0) {
                     found = 1;
                     break;
                 }
             }
             if (found) {
                 unsigned long delta_rx_pkts = curr[i].rx_packets - prev[i].rx_packets;
                 unsigned long delta_tx_pkts = curr[i].tx_packets - prev[i].tx_packets;
                 unsigned long delta_rx_err  = curr[i].rx_errs - prev[i].rx_errs;
                 unsigned long delta_tx_err  = curr[i].tx_errs - prev[i].tx_errs;
                 unsigned long delta_rx_dp   = curr[i].rx_drop - prev[i].rx_drop;
                 unsigned long delta_tx_dp   = curr[i].tx_drop - prev[i].tx_drop;
                 
                 double rx_err_percent = (delta_rx_pkts > 0) ? (delta_rx_err * 100.0 / delta_rx_pkts) : 0.0;
                 double tx_err_percent = (delta_tx_pkts > 0) ? (delta_tx_err * 100.0 / delta_tx_pkts) : 0.0;
                 double rx_dp_percent  = (delta_rx_pkts > 0) ? (delta_rx_dp * 100.0 / delta_rx_pkts) : 0.0;
                 double tx_dp_percent  = (delta_tx_pkts > 0) ? (delta_tx_dp * 100.0 / delta_tx_pkts) : 0.0;
                 
                 printf("%-6s %12lu %12lu %11.2f%% %11.2f%% %11.2f%% %11.2f%%\n",
                        curr[i].name, delta_rx_pkts, delta_tx_pkts,
                        rx_err_percent, tx_err_percent,
                        rx_dp_percent, tx_dp_percent);
                 
                 total_delta_rx_pkts += delta_rx_pkts;
                 total_delta_tx_pkts += delta_tx_pkts;
                 total_delta_rx_err  += delta_rx_err;
                 total_delta_tx_err  += delta_tx_err;
                 total_delta_rx_dp   += delta_rx_dp;
                 total_delta_tx_dp   += delta_tx_dp;
                 metrics_count++;
             } else {
                 printf("%-6s %12s %12s %12s %12s %12s %12s\n",
                        curr[i].name, "N/A", "N/A", "N/A", "N/A", "N/A", "N/A");
             }
         }
         
         if (metrics_count > 0) {
             double agg_rx_err_percent = (total_delta_rx_pkts > 0) ? (total_delta_rx_err * 100.0 / total_delta_rx_pkts) : 0.0;
             double agg_tx_err_percent = (total_delta_tx_pkts > 0) ? (total_delta_tx_err * 100.0 / total_delta_tx_pkts) : 0.0;
             double agg_rx_dp_percent  = (total_delta_rx_pkts > 0) ? (total_delta_rx_dp * 100.0 / total_delta_rx_pkts) : 0.0;
             double agg_tx_dp_percent  = (total_delta_tx_pkts > 0) ? (total_delta_tx_dp * 100.0 / total_delta_tx_pkts) : 0.0;
             
             printf("\nAggregate Summary (per-second differences across all interfaces):\n");
             printf("Total RX_pkts/s: %lu, Total TX_pkts/s: %lu\n", total_delta_rx_pkts, total_delta_tx_pkts);
             printf("Aggregate RX Errors: %.2f%%, TX Errors: %.2f%%\n", agg_rx_err_percent, agg_tx_err_percent);
             printf("Aggregate RX Drops : %.2f%%, TX Drops : %.2f%%\n", agg_rx_dp_percent, agg_tx_dp_percent);
         }
         print_separator(TABLE1_WIDTH);

         if (log_fp != NULL) {
             if (!header_written) {
                 fprintf(log_fp, "timestamp,interface,rx_bytes_per_sec,tx_bytes_per_sec,rx_total,tx_total\n");
                 header_written = 1;
             }
             char timestamp[32] = "unknown";
             struct tm *tm_info = localtime(&now);
             if (tm_info != NULL) {
                 if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
                     snprintf(timestamp, sizeof(timestamp), "unknown");
                 }
             }
             for (int i = 0; i < curr_count; i++) {
                 fprintf(log_fp, "%s,%s,%lu,%lu,%lu,%lu\n",
                         timestamp,
                         curr[i].name,
                         rx_rates[i],
                         tx_rates[i],
                         curr[i].rx_bytes,
                         curr[i].tx_bytes);
             }
             fflush(log_fp);
         }

         // Update previous statistics for the next interval.
         prev_count = curr_count;
         for (int i = 0; i < curr_count; i++) {
              prev[i] = curr[i];
         }
    }
    
cleanup:
    // Restore terminal settings and original SIGINT handler.
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    sigaction(SIGINT, &old_sa, NULL);
}
 
int main(void) {
    char input[MAX_INPUT];
    int choice;

    print_compliance_banner();

    while (1) {
        // Display the interactive menu.
        printf("\n--- Network Manager ---\n");
        printf("1. Search available wireless networks\n");
        printf("2. Connect to a wireless network\n");
        printf("3. Disconnect from the current wireless network\n");
        printf("4. Run diagnostics\n");
        printf("5. Monitoring mode\n");
        printf("6. Show current connection details\n");
        printf("7. Exit\n");
        printf("Enter your choice: ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            continue;
        }
        
        choice = atoi(input);

        switch (choice) {
            case 1:
                // List available wireless networks using nmcli.
                if (!confirm_action("This action may trigger a Wi-Fi scan. Proceed? (y/N): ")) {
                    printf("Scan cancelled to respect venue policies.\n");
                    break;
                }
                printf("Searching for available wireless networks...\n");
                if (system("nmcli device wifi list") != 0) {
                    printf("Command failed.\n");
                }
                break;
            case 2: {
                // Connect to a wireless network.
                char ssid[MAX_INPUT];
                char password[MAX_INPUT];
                char command[256];

                printf("Only join networks you are authorized to use.\n");
                printf("Enter SSID: ");
                if (fgets(ssid, sizeof(ssid), stdin) == NULL) break;
                ssid[strcspn(ssid, "\n")] = '\0';

                printf("Enter Password (leave blank if open): ");
                if (fgets(password, sizeof(password), stdin) == NULL) break;
                password[strcspn(password, "\n")] = '\0';

                if (strlen(password) == 0) {
                    snprintf(command, sizeof(command), "nmcli device wifi connect '%s'", ssid);
                } else {
                    snprintf(command, sizeof(command), "nmcli device wifi connect '%s' password '%s'", ssid, password);
                }
                printf("Attempting to connect...\n");
                if (system(command) != 0) {
                    printf("Connection attempt failed. Consider running diagnostics (option 4) for more details.\n");
                }
                break;
            }
            case 3: {
                // Disconnect from the current wireless network.
                char device[64];
                char command[256];
                printf("Enter device to disconnect (default wlan0): ");
                if (fgets(device, sizeof(device), stdin) == NULL) {
                    snprintf(device, sizeof(device), "wlan0");
                } else {
                    trim_trailing_whitespace(device);
                    if (device[0] == '\0') {
                        snprintf(device, sizeof(device), "wlan0");
                    }
                }
                if (snprintf(command, sizeof(command), "nmcli device disconnect '%s'", device) >= (int)sizeof(command)) {
                    printf("Device name too long.\n");
                    break;
                }
                printf("Disconnecting interface %s...\n", device);
                if (system(command) != 0) {
                    printf("Disconnect command failed.\n");
                }
                break;
            }
            case 4:
                // Run diagnostics.
                run_diagnostics();
                break;
            case 5: {
                // Prompt the user for a refresh interval (in seconds) before entering monitoring mode.
                char interval_str[MAX_INPUT];
                int interval;
                FILE *log_fp = NULL;
                char log_path[256];

                printf("Enter refresh interval in seconds (default 1): ");
                if (fgets(interval_str, sizeof(interval_str), stdin) != NULL) {
                    interval = atoi(interval_str);
                    if (interval <= 0)
                        interval = 1;
                } else {
                    interval = 1;
                }

                if (confirm_action("Enable passive CSV logging for this session? (y/N): ")) {
                    printf("Enter log file path (default inet_log.csv): ");
                    if (fgets(log_path, sizeof(log_path), stdin) == NULL) {
                        snprintf(log_path, sizeof(log_path), "inet_log.csv");
                    } else {
                        trim_trailing_whitespace(log_path);
                        if (log_path[0] == '\0') {
                            snprintf(log_path, sizeof(log_path), "inet_log.csv");
                        }
                    }
                    log_fp = fopen(log_path, "a");
                    if (log_fp == NULL) {
                        perror("fopen");
                    } else {
                        printf("Logging interface counters to %s.\n", log_path);
                    }
                }

                monitor_mode(interval, log_fp);

                if (log_fp != NULL) {
                    fclose(log_fp);
                    printf("Saved passive log to %s.\n", log_path);
                }
                break;
            }
            case 6:
                show_connection_details();
                break;
            case 7:
                // Exit the application.
                printf("Exiting...\n");
                return 0;
            default:
                printf("Invalid choice, please try again.\n");
                break;
        }
    }
    return 0;
}
