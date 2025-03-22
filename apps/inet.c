#define _POSIX_C_SOURCE 200809L  // Enable POSIX features, e.g. popen(), pclose()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_INPUT 100
#define MAX_INTERFACES 32
#define MAX_BAR_LEN 40  // Maximum characters for histogram bars

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
 *
 * The file /proc/net/dev contains two header lines, then one line per interface.
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
        
        // Expected format:
        // iface:  rx_bytes rx_packets rx_errs rx_drop rx_fifo rx_frame rx_compressed rx_multicast
        //         tx_bytes tx_packets tx_errs tx_drop tx_fifo tx_colls tx_carrier tx_compressed
        // Read the needed fields and read suppressed ones into dummy variables.
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
            strncpy(stats[count].name, iface, sizeof(stats[count].name) - 1);
            stats[count].name[sizeof(stats[count].name) - 1] = '\0';
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
         printf("Diagnostic: No network adapters detected. Check drivers or physical connections.\n");
    }
    
    // Check for internet connectivity
    if (check_connectivity() == 0) {
         printf("Diagnostic: Internet connectivity is working (ping to 8.8.8.8 successful).\n");
    } else {
         printf("Diagnostic: Internet connectivity test failed. Check your network connection and router/modem.\n");
    }
    
    printf("--- End of Diagnostics ---\n");
}

/*
 * monitor_mode
 *
 * Enters a continuous monitoring loop where the app periodically reads
 * network interface statistics, computes throughput (bytes per second),
 * and displays key meta data in a formatted table that fits into a single terminal screen.
 *
 * Additionally, it prints a histogram view of RX and TX throughput below the table
 * and includes instructions regarding the displayed metrics.
 *
 * Engineers can observe live status; press Ctrl+C to exit monitoring mode.
 */
void monitor_mode(void) {
    const int interval = 1; // Refresh interval in seconds
    NetDevStats prev[MAX_INTERFACES] = {0}, curr[MAX_INTERFACES] = {0};
    unsigned long rx_rates[MAX_INTERFACES] = {0};
    unsigned long tx_rates[MAX_INTERFACES] = {0};
    int prev_count = read_netdev_stats(prev, MAX_INTERFACES);
    if (prev_count < 0) {
         printf("Error: Unable to read network statistics.\n");
         return;
    }
    
    while (1) {
         sleep(interval);
         int curr_count = read_netdev_stats(curr, MAX_INTERFACES);
         if (curr_count < 0) {
              printf("Error: Unable to read network statistics.\n");
              break;
         }
         // Clear the terminal screen.
         system("clear");
         printf("Network Monitoring Mode (refresh every %d second(s)). Press Ctrl+C to exit.\n", interval);
         printf("--------------------------------------------------------------------------------\n");
         // Table header: Interface, RX/s, TX/s, Total RX bytes, Total TX bytes, Packets, Errors and Drops.
         printf("%-6s %8s %8s %10s %10s %8s %8s %6s %6s %6s %6s\n",
                "IFACE", "RX/s", "TX/s", "RXTot", "TXTOT", "RXpkts", "TXpkts",
                "RXerr", "TXerr", "RXdp", "TXdp");
         printf("--------------------------------------------------------------------------------\n");
         
         // Reset rates and determine rates for each interface.
         for (int i = 0; i < curr_count; i++) {
             int found = 0;
             NetDevStats *prev_stat = NULL;
             for (int j = 0; j < prev_count; j++) {
                 if (strcmp(curr[i].name, prev[j].name) == 0) {
                     found = 1;
                     prev_stat = &prev[j];
                     break;
                 }
             }
             if (!found) {
                rx_rates[i] = 0;
                tx_rates[i] = 0;
                printf("%-6s %8s %8s %10lu %10lu %8lu %8lu %6lu %6lu %6lu %6lu\n",
                    curr[i].name, "N/A", "N/A", curr[i].rx_bytes, curr[i].tx_bytes,
                    curr[i].rx_packets, curr[i].tx_packets,
                    curr[i].rx_errs, curr[i].tx_errs, curr[i].rx_drop, curr[i].tx_drop);
             } else {
                rx_rates[i] = (curr[i].rx_bytes - prev_stat->rx_bytes) / interval;
                tx_rates[i] = (curr[i].tx_bytes - prev_stat->tx_bytes) / interval;
                printf("%-6s %8lu %8lu %10lu %10lu %8lu %8lu %6lu %6lu %6lu %6lu\n",
                    curr[i].name, rx_rates[i], tx_rates[i],
                    curr[i].rx_bytes, curr[i].tx_bytes,
                    curr[i].rx_packets, curr[i].tx_packets,
                    curr[i].rx_errs, curr[i].tx_errs,
                    curr[i].rx_drop, curr[i].tx_drop);
             }
         }
         
         // Compute maximum rates for scaling histograms.
         unsigned long max_rx = 0, max_tx = 0;
         for (int i = 0; i < curr_count; i++) {
             if (rx_rates[i] > max_rx)
                 max_rx = rx_rates[i];
             if (tx_rates[i] > max_tx)
                 max_tx = tx_rates[i];
         }
         // Prevent division by zero.
         if (max_rx == 0) max_rx = 1;
         if (max_tx == 0) max_tx = 1;
         
         // Print histogram for RX throughput.
         printf("\nHistogram for RX Throughput (bytes/sec):\n");
         for (int i = 0; i < curr_count; i++) {
             int bar_len = (int)((rx_rates[i] * MAX_BAR_LEN) / max_rx);
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < MAX_BAR_LEN; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", rx_rates[i]);
         }
         
         // Print histogram for TX throughput.
         printf("\nHistogram for TX Throughput (bytes/sec):\n");
         for (int i = 0; i < curr_count; i++) {
             int bar_len = (int)((tx_rates[i] * MAX_BAR_LEN) / max_tx);
             printf("%-6s [", curr[i].name);
             for (int j = 0; j < bar_len; j++) {
                 putchar('#');
             }
             for (int j = bar_len; j < MAX_BAR_LEN; j++) {
                 putchar(' ');
             }
             printf("] %8lu B/s\n", tx_rates[i]);
         }
         
         // Display instructions for interpreting the metrics.
         printf("\nInstructions:\n");
         printf("  IFACE  : Interface name.\n");
         printf("  RX/s   : Bytes received per second.\n");
         printf("  TX/s   : Bytes transmitted per second.\n");
         printf("  RXTot  : Total bytes received since boot or reset.\n");
         printf("  TXTOT  : Total bytes transmitted since boot or reset.\n");
         printf("  RXpkts : Total received packets.\n");
         printf("  TXpkts : Total transmitted packets.\n");
         printf("  RXerr  : Number of receive errors.\n");
         printf("  TXerr  : Number of transmit errors.\n");
         printf("  RXdp   : Number of dropped received packets.\n");
         printf("  TXdp   : Number of dropped transmitted packets.\n");
         printf("  Histogram bars represent relative throughput (compared to the maximum among all interfaces).\n");
         printf("--------------------------------------------------------------------------------\n");
         
         // Update previous statistics for the next interval.
         prev_count = curr_count;
         for (int i = 0; i < curr_count; i++) {
              prev[i] = curr[i];
         }
    }
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
                system("nmcli device wifi list");
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
                    printf("Connection attempt failed. Consider running diagnostics (option 4) for more details.\n");
                }
                break;
            }
            case 3:
                // Disconnect from the current wireless network (assumes interface is wlan0).
                printf("Disconnecting from current wireless network...\n");
                system("nmcli device disconnect wlan0");
                break;
            case 4:
                // Run diagnostics.
                run_diagnostics();
                break;
            case 5:
                // Enter monitoring mode.
                monitor_mode();
                break;
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
