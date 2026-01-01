#define _POSIX_C_SOURCE 200809L  // Enable POSIX features, e.g. popen(), pclose()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>         // For timestamp display
#include <termios.h>      // For terminal control
#include <sys/select.h>   // For select()
#include <signal.h>       // For signal handling

#include "../lib/terminal_layout.h"

#define MAX_INPUT 100
#define MAX_INTERFACES 32
#define MAX_BAR_LEN 28  // Base characters for histogram bars within 80 columns

// Table widths for proper alignment on an 80 column display
#define TABLE1_WIDTH 74  // Main statistics table width
#define TABLE2_WIDTH 60   // Additional metrics table width

// Helper function to print a separator line with a given width.
void print_separator(int width) {
    int target_cols = budostack_get_target_cols();
    if (width > target_cols) {
        width = target_cols;
    }
    if (width < 0) {
        return;
    }
    for (int i = 0; i < width; i++) {
         putchar('-');
    }
    putchar('\n');
}

static int get_bar_limit(void) {
    int target_cols = budostack_get_target_cols();
    int scaled = (target_cols * MAX_BAR_LEN) / BUDOSTACK_TARGET_COLS;
    if (scaled < 10) {
        scaled = 10;
    }
    if (scaled > target_cols - 20) {
        scaled = target_cols - 20;
    }
    if (scaled < 1) {
        scaled = 1;
    }
    return scaled;
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
 * Tests internet connectivity by pinging 8.8.8.8.
 * Returns 0 if successful, nonzero otherwise.
 */
int check_connectivity(void) {
    return system("ping -c 1 8.8.8.8 > /dev/null 2>&1");
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
         printf("Diagnostic: No network adapters detected.\n");
         printf("Check drivers or physical connections.\n");
    }
    
    // Check for internet connectivity
    if (check_connectivity() == 0) {
         printf("Diagnostic: Internet connectivity works (ping 8.8.8.8).\n");
    } else {
         printf("Diagnostic: Internet connectivity test failed.\n");
         printf("Check your network connection and router or modem.\n");
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
 *  - Exits when the user presses the plain 'q' key.
 *
 * Design rationale:
 * We configure the terminal in non-canonical mode and use select() to wait for user input or a timeout.
 * Pressing the 'q' key will break out of the monitoring loop.
 */
void monitor_mode(int interval) {
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
    
    NetDevStats prev[MAX_INTERFACES] = {0}, curr[MAX_INTERFACES] = {0};
    unsigned long rx_rates[MAX_INTERFACES] = {0};
    unsigned long tx_rates[MAX_INTERFACES] = {0};
    
    int prev_count = read_netdev_stats(prev, MAX_INTERFACES);
    if (prev_count < 0) {
         printf("Error: Unable to read network statistics.\n");
         goto cleanup;
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
        printf("%-6s %8s %8s %10s %10s %9s %9s\n",
               "IFACE", "RX/s", "TX/s", "RXtot", "TXtot", "RXpkt", "TXpkt");
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
               printf("%-6s %8s %8s %10lu %10lu %9lu %9lu\n",
                      curr[i].name, "N/A", "N/A",
                      curr[i].rx_bytes, curr[i].tx_bytes,
                      curr[i].rx_packets, curr[i].tx_packets);
            } else {
               printf("%-6s %8lu %8lu %10lu %10lu %9lu %9lu\n",
                      curr[i].name, rx_rates[i], tx_rates[i],
                      curr[i].rx_bytes, curr[i].tx_bytes,
                      curr[i].rx_packets, curr[i].tx_packets);
            }
        }
         
         // Print bar view for RX throughput per interface.
         printf("\nMeasured RX Throughput (bytes/sec):\n");
         int bar_limit = get_bar_limit();
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
                 bar_len = (int)((rx_rates[i] * bar_limit) / iface_max_rx);
                 if (bar_len > bar_limit)
                     bar_len = bar_limit;
             }
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < bar_limit; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", rx_rates[i]);
         }
         
         // Print bar view for TX throughput per interface.
         printf("\nMeasured TX Throughput (bytes/sec):\n");
         bar_limit = get_bar_limit();
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
                 bar_len = (int)((tx_rates[i] * bar_limit) / iface_max_tx);
                 if (bar_len > bar_limit)
                     bar_len = bar_limit;
             }
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < bar_limit; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", tx_rates[i]);
         }
         
         // --- Additional Metrics Section ---
        printf("\nAdditional Metrics (per-second differences):\n");
        printf("%-6s %10s %10s %7s %7s %7s %7s\n",
               "IFACE", "RXp/s", "TXp/s", "RXer", "TXer", "RXdp", "TXdp");
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
                 
                printf("%-6s %10lu %10lu %6.2f%% %6.2f%% %6.2f%% %6.2f%%\n",
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
                printf("%-6s %10s %10s %7s %7s %7s %7s\n",
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

    while (1) {
        // Display the interactive menu.
        printf("\n--- Network Manager ---\n");
        printf("1. Search available wireless networks\n");
        printf("2. Connect to a wireless network\n");
        printf("3. Disconnect from the current wireless network\n");
        printf("4. Run diagnostics\n");
        printf("5. Monitoring mode\n");
        printf("6. Exit\n");
        printf("Enter your choice: ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            continue;
        }
        
        choice = atoi(input);

        switch (choice) {
            case 1:
                // List available wireless networks using nmcli.
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
                    printf("Connection attempt failed.\n");
                    printf("Run diagnostics (option 4) for more details.\n");
                }
                break;
            }
            case 3:
                // Disconnect from the current wireless network (assumes interface is wlan0).
                printf("Disconnecting from current wireless network...\n");
                if (system("nmcli device disconnect wlan0") != 0) {
                    printf("Disconnect command failed.\n");
                }
                break;
            case 4:
                // Run diagnostics.
                run_diagnostics();
                break;
            case 5: {
                // Prompt the user for a refresh interval (in seconds) before entering monitoring mode.
                char interval_str[MAX_INPUT];
                int interval;
                printf("Enter refresh interval in seconds (default 1): ");
                if (fgets(interval_str, sizeof(interval_str), stdin) != NULL) {
                    interval = atoi(interval_str);
                    if (interval <= 0)
                        interval = 1;
                } else {
                    interval = 1;
                }
                monitor_mode(interval);
                break;
            }
            case 6:
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
