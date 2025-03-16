/* ChatGPT: DO NOT MODIFY OR REMOVE THIS HEADER BUT IMPLEMENT IT FULLY!
   Design principle: We maintain all original functionality while extending the 
   “search network” command to include an ASCII visualization. This implementation 
   uses popen() to capture the output of arp-scan, filters device lines, and then 
   prints a simple diagram. All changes adhere to plain C, C11, and use only 
   standard cross-platform libraries.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>  // For isdigit()

#define TEMP_HWFILE "/tmp/hwinfo.txt"
#define LOG_HW_FILE "logs/hwtree.txt"
#define LOG_LINUX_FILE "logs/linux.txt"
#define TRUNCATED_DT_LINES 1024  // Configurable: number of lines for truncated device tree dump

// Maximum number of devices we expect (adjust as needed).
#define MAX_DEVICES 100
#define MAX_LINE_LEN 512

// Declaration of prettyprint, assumed to be provided externally.
extern void prettyprint(const char *message, unsigned int delay_ms);

/*
 * Function: print_network_ascii
 * -----------------------------
 *  Prints a simple ASCII network topology diagram.
 *
 *  devices: an array of strings containing the lines (devices) from arp-scan.
 *  count: the number of devices discovered.
 */
void print_network_ascii(char devices[][MAX_LINE_LEN], int count) {
    printf("\nASCII Network Topology Diagram:\n");
    printf("          [Router/Switch]\n");
    printf("                |\n");
    // For each device, print a branch. If there are multiple devices, 
    // we simply list them one per line with a connecting line.
    for (int i = 0; i < count; i++) {
        // We use a simple format: the first part of the device line is shown.
        // You can modify this to extract only the IP or name if desired.
        printf("                +-- [%s]\n", devices[i]);
    }
    printf("\n");
}

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
            printf("      Actively scans the local network using arp-scan (requires root privileges) and shows an ASCII visualization of the network topology.\n\n");
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
            // Instead of simply using system(), we now capture the output via popen().
            FILE *fp = popen("arp-scan -l", "r");
            if (fp == NULL) {
                printf("Error: Failed to run arp-scan.\n");
            } else {
                char line[MAX_LINE_LEN];
                // Array to store device lines.
                char devices[MAX_DEVICES][MAX_LINE_LEN];
                int device_count = 0;

                // Read each line from the arp-scan output.
                while (fgets(line, sizeof(line), fp) != NULL) {
                    // Print the line as is to preserve original functionality.
                    printf("%s", line);
                    // Check if the line likely represents a device:
                    // (if it starts with a digit, we assume it’s a device entry).
                    if (isdigit((unsigned char)line[0]) && device_count < MAX_DEVICES) {
                        // Remove any trailing newline.
                        size_t linelen = strlen(line);
                        if (linelen > 0 && line[linelen - 1] == '\n') {
                            line[linelen - 1] = '\0';
                        }
                        // Copy the line into our devices array.
                        strncpy(devices[device_count], line, MAX_LINE_LEN - 1);
                        devices[device_count][MAX_LINE_LEN - 1] = '\0';
                        device_count++;
                    }
                }
                pclose(fp);

                // Print the ASCII visualization based on the captured device list.
                print_network_ascii(devices, device_count);
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
- POSIX popen() documentation (used here for capturing command output).
- Original source code design for assist.c.
- Various online resources regarding ASCII art representations in plain C.
*/
