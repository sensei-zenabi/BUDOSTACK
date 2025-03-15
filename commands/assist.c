/* ChatGPT: DO NOT MODIFY OR REMOVE THIS HEADER BUT IMPLEMENT IT FULLY!

Filename: assist.c

Description:

    Implement a loop, where user is able to discuss with this chatbot.
    Include a command parser that recognizes familiar commands and creates 
    a randomized default message for unrecognized commands.
    If the user types "exit", the chatbot program terminates.

Supported commands:

    help
        1. Displays a neatly formatted list of supported commands and their descriptions.

    search network
        1. Actively scans and displays all MAC, IP addresses, and device names 
           from devices in the same Wi-Fi network.
           (On Linux, uses arp-scan to query every host in the local subnet.)

    ping <IP-address>
        1. Pings the specified IP address 5 times and reports the results.

    search "string"
        1. Searches for the given string in files in the current folder and subfolders.
        2. If the file is binary, only the filename is displayed.

    search hardware
        1. Displays a comprehensive, developer-friendly overview of the system's hardware.
           This includes:
             - A hierarchical overview via "lshw -short".
             - Detailed hardware info (using lshw, lscpu, free, lspci, lsusb, sensors, etc.).
             - A logical tree view of the top-level device tree nodes.
             - A truncated dump of the device tree (first TRUNCATED_DT_LINES lines, default 1024).
             - The full device tree dump.
           The output is both displayed (paged via less) and exported to logs/hwtree.txt.

    linux
        1. Displays a complete list of useful Linux commands stored in logs/linux.txt,
           where the commands are sorted in functional categories.

Remarks:

    1. Functions declared as "extern" come from shared libraries and do not need to be implemented.
    2. All functionalities are implemented using standard POSIX calls and utilities.
    3. Do not delete or modify this header.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEMP_HWFILE "/tmp/hwinfo.txt"
#define LOG_HW_FILE "logs/hwtree.txt"
#define LOG_LINUX_FILE "logs/linux.txt"
#define TRUNCATED_DT_LINES 1024  // Configurable: number of lines for truncated device tree dump

// Declaration of prettyprint, assumed to be provided externally.
extern void prettyprint(const char *message, unsigned int delay_ms);

int main(void) {
    // Clear the console.
    system("clear");
    prettyprint("Hello User! How can I help you?\n", 25);

    // Seed the random number generator.
    srand((unsigned) time(NULL));

    char input[256];

    // Main command loop.
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        if (strcmp(input, "exit") == 0) {
            break;
        }
        else if (strcmp(input, "help") == 0) {
            printf("\nSupported commands:\n");
            printf("  help\n");
            printf("      Displays this help information and list of commands.\n\n");
            printf("  search network\n");
            printf("      Actively scans the local network using arp-scan (requires root privileges).\n\n");
            printf("  ping <IP-address>\n");
            printf("      Pings the specified IP address 5 times and reports the results.\n\n");
            printf("  search \"string\"\n");
            printf("      Searches for the given string in files in the current folder and subfolders.\n");
            printf("      If the file is binary, only the filename is displayed.\n\n");
            printf("  search hardware\n");
            printf("      Displays a comprehensive overview of the system's hardware.\n");
            printf("      This includes:\n");
            printf("        - A hierarchical overview (lshw -short).\n");
            printf("        - Detailed hardware info (lshw, lscpu, free, lspci, lsusb, sensors, etc.).\n");
            printf("        - Logical tree view of top-level device tree nodes.\n");
            printf("        - Truncated device tree dump (first %d lines).\n", TRUNCATED_DT_LINES);
            printf("        - Full device tree dump.\n");
            printf("      The output is displayed (paged via less) and saved to logs/hwtree.txt.\n\n");
            printf("  linux\n");
            printf("      Displays a complete list of useful Linux commands stored in logs/linux.txt.\n\n");
        }
        else if (strcmp(input, "search network") == 0) {
            printf("Performing active network scan using arp-scan...\n");
            int ret = system("arp-scan -l");
            if (ret != 0) {
                printf("Error: arp-scan failed. Ensure it is installed and you have sufficient privileges.\n");
            }
        }
        else if (strncmp(input, "ping ", 5) == 0) {
            char *ip = input + 5;
            if (ip[0] == '\0') {
                printf("Error: No IP address provided.\n");
            } else {
                printf("Pinging %s ...\n", ip);
                char command[512];
                snprintf(command, sizeof(command), "ping -c 5 %s", ip);
                int ret = system(command);
                if (ret != 0) {
                    printf("Error: Ping command failed or the IP address is unreachable.\n");
                }
            }
        }
        else if (strncmp(input, "search ", 7) == 0 && strchr(input, '\"') != NULL) {
            char *quote1 = strchr(input, '\"');
            char *quote2 = strchr(quote1 + 1, '\"');
            if (quote1 && quote2) {
                int str_len = (int)(quote2 - quote1 - 1);
                char search_term[256];
                if (str_len >= (int)sizeof(search_term)) {
                    str_len = sizeof(search_term) - 1;
                }
                strncpy(search_term, quote1 + 1, (size_t)str_len);
                search_term[str_len] = '\0';
                printf("Searching for \"%s\" in files...\n", search_term);
                char command[512];
                snprintf(command, sizeof(command), "grep -R -I \"%s\" .", search_term);
                int ret = system(command);
                if (ret != 0) {
                    printf("Error: File search command failed or returned no matches.\n");
                }
            } else {
                printf("Error: Search string must be enclosed in double quotes.\n");
            }
        }
        else if (strcmp(input, "search hardware") == 0) {
            printf("Gathering comprehensive hardware specs...\n");
            // Remove any previous temporary file.
            system("rm -f " TEMP_HWFILE);
            
            // Create logs directory if not exists.
            system("mkdir -p logs");

            // Create organized sections in the temporary file.
            system("echo \"=== Detailed Hardware Information ===\" > " TEMP_HWFILE);

            // Section: Hierarchical hardware overview.
            system("echo \"\n--- Hardware Overview (lshw -short) ---\" >> " TEMP_HWFILE);
            system("lshw -short 2>/dev/null >> " TEMP_HWFILE);

            // Section: Detailed hardware info.
            system("echo \"\n--- Detailed lshw Output ---\" >> " TEMP_HWFILE);
            system("lshw 2>/dev/null >> " TEMP_HWFILE);
            system("echo \"\n--- CPU Info (/proc/cpuinfo & lscpu) ---\" >> " TEMP_HWFILE);
            system("cat /proc/cpuinfo >> " TEMP_HWFILE);
            system("lscpu >> " TEMP_HWFILE);
            system("echo \"\n--- Memory Info (proc & free) ---\" >> " TEMP_HWFILE);
            system("cat /proc/meminfo >> " TEMP_HWFILE);
            system("free -h >> " TEMP_HWFILE);
            system("echo \"\n--- PCI Devices ---\" >> " TEMP_HWFILE);
            system("lspci -v >> " TEMP_HWFILE);
            system("echo \"\n--- USB Devices ---\" >> " TEMP_HWFILE);
            system("lsusb -v 2>/dev/null | head -n 50 >> " TEMP_HWFILE);
            system("echo \"\n--- Network Interfaces ---\" >> " TEMP_HWFILE);
            system("ip addr >> " TEMP_HWFILE);
            system("echo \"\n--- Sensors Info ---\" >> " TEMP_HWFILE);
            system("sensors 2>/dev/null >> " TEMP_HWFILE);
            system("echo \"\n--- Battery Info ---\" >> " TEMP_HWFILE);
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/status; "
                   "elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/status; "
                   "else echo \"No battery found\"; fi' >> " TEMP_HWFILE);
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/capacity; "
                   "elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/capacity; fi && echo \"%\"' >> " TEMP_HWFILE);
            system("echo \"\n--- Storage Devices (lsblk) ---\" >> " TEMP_HWFILE);
            system("lsblk >> " TEMP_HWFILE);
            system("echo \"\n--- Input Devices (/proc/bus/input/devices) ---\" >> " TEMP_HWFILE);
            system("cat /proc/bus/input/devices >> " TEMP_HWFILE);
            system("echo \"\n--- Audio Devices (aplay -l) ---\" >> " TEMP_HWFILE);
            system("aplay -l 2>/dev/null >> " TEMP_HWFILE);

            // Section: Logical tree view of top-level device tree nodes.
            system("echo \"\n--- Device Tree Overview (Logical Tree) ---\" >> " TEMP_HWFILE);
            system("find /proc/device-tree -maxdepth 2 | sort >> " TEMP_HWFILE);

            // Section: Truncated device tree dump.
            {
                char truncated_cmd[128];
                snprintf(truncated_cmd, sizeof(truncated_cmd), "dtc -I fs -O dts /proc/device-tree | head -n %d >> %s", TRUNCATED_DT_LINES, TEMP_HWFILE);
                system(truncated_cmd);
            }

            // Section: Full device tree dump.
            system("echo \"\n--- Full Device Tree Dump ---\" >> " TEMP_HWFILE);
            system("dtc -I fs -O dts /proc/device-tree >> " TEMP_HWFILE);

            // Export the complete output to logs/hwtree.txt.
            system("cp " TEMP_HWFILE " " LOG_HW_FILE);

            // Page the organized output.
            system("less " TEMP_HWFILE);
            system("rm " TEMP_HWFILE);
        }
        else if (strcmp(input, "linux") == 0) {
            printf("Displaying the complete Linux command list from logs/linux.txt...\n");
            int ret = system("less " LOG_LINUX_FILE);
            if (ret != 0) {
                printf("Error: Unable to display " LOG_LINUX_FILE "\n");
            }
        }
        else {
            const char *default_responses[] = {
                "I'm not sure how to respond to that.",
                "Could you please rephrase?",
                "I don't understand, can you try another command?",
                "Hmm, that's interesting. Tell me more!"
            };
            int num_responses = sizeof(default_responses) / sizeof(default_responses[0]);
            int random_index = rand() % num_responses;
            printf("%s\n", default_responses[random_index]);
        }
    }

    printf("Goodbye!\n");
    return 0;
}

/*
References:
- ISO C11 Standard Documentation for C Standard Library functions.
- Linux kernel documentation for /proc and /sys virtual files.
- Documentation for utilities: lshw, lscpu, free, lspci, lsusb, ip, sensors, upower, lsblk, and aplay.
- Device Tree Compiler (dtc) usage for dumping /proc/device-tree.
- POSIX standard functions and system interfaces.
- Various online resources regarding hardware overviews and logical device trees.
*/
