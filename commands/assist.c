/* ChatGPT: DO NOT MODIFY OR REMOVE THIS HEADER BUT IMPLEMENT IT FULLY!

Filename: discover.c

Description:

	Implement a loop, where user is able to discuss with this chatbot.
	Include a command parser that recognizes familiar commands and create 
	a randomized default message to commands that are not recognized.
	If the user types "exit", the chatbot program needs to terminate.

Supported commands:

	help
		1. Displays all the supported commands (from below)

	search network
		1. Actively scans and displays all the MAC, IP addresses and device names 
		   from devices in the same Wi-Fi network.
		   (On Linux, uses arp-scan to actively query every host in your subnet.)

	ping <IP-address>
		1. Pings the device 5 times and reports metrics from the results.

	search "string"
		1. Searches and displays all the files and their contents that contain the 
		   given string from the current folder and its subfolders.
		2. If the file is a binary file, only the filename is displayed.

	search hardware
		1. Searches and displays all connected hardware specs and devices from the device 
		   where the app is running (Linux only). The output is paged.
    
	search hardware -short
		1. Displays a concise summary of the connected hardware specs, including key details 
		   from Linux kernel virtual files and battery information.

 Remarks:

	1. Functions defined as "extern" come from shared libraries and do not need to be implemented.
	2. Implement all above descriptions and requirements.
	3. Provide the fully updated source code.
	4. Do not delete or modify this header.

*/

/*
Design Principles and Implementation Notes:
- The program uses a continuous command loop to interact with the user.
- Commands are parsed using only standard string functions (<string.h>) for cross-platform compatibility.
- Default responses are randomized by seeding the random number generator with the current time.
- For "search network", on Linux the program uses "arp-scan -l" to actively scan the local subnet.
  This ensures that all devices on the network respond, not only those present in the ARP cache.
- On Windows or non-Linux systems, "arp -a" is used.
- Other commands (ping, file search, hardware search) invoke system utilities, with conditional compilation where needed.
- The code is written in plain C using -std=c11 and only standard cross-platform libraries.
- The external function prettyprint is assumed to be provided via a shared library.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #define CLEAR_COMMAND "cls"
#else
    #define CLEAR_COMMAND "clear"
#endif

// Declaration of prettyprint, assumed to be provided externally.
extern void prettyprint(const char *message, unsigned int delay_ms);

int main(void) {
    // Clear the console.
    system(CLEAR_COMMAND);
    prettyprint("Hello User! How can I help you?\n", 25);

    // Seed random number generator.
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
            printf("Supported commands:\n");
            printf("help - Displays this help information and list of commands.\n");
#ifdef __linux__
            printf("search network - Actively scans the local network using arp-scan (requires arp-scan and root privileges).\n");
#else
            printf("search network - Displays network devices using 'arp -a'.\n");
#endif
			printf("  Note! Always ensure you have the proper authorization before scanning any network.\n");
            printf("ping <IP-address> - Pings the specified IP address 5 times and reports the results.\n");
            printf("search \"string\" - Searches for the given string in files in the current folder and subfolders.\n");
            printf("search hardware - Displays detailed hardware specs (Linux only, output is paged).\n");
            printf("search hardware -short - Displays concise hardware specs (Linux only).\n");
        }
        else if (strcmp(input, "search network") == 0) {
#ifdef __linux__
            printf("Performing active network scan using arp-scan...\n");
            // Actively scan the local network using arp-scan.
            int ret = system("arp-scan -l");
            if (ret != 0) {
                printf("Error: arp-scan failed. Ensure it is installed and you have sufficient privileges.\n");
            }
#else
            printf("Performing network search...\n");
            int ret = system("arp -a");
            if (ret != 0) {
                printf("Error: Network search command failed or is not supported on this system.\n");
            }
#endif
        }
        else if (strncmp(input, "ping ", 5) == 0) {
            char *ip = input + 5;
            if (ip[0] == '\0') {
                printf("Error: No IP address provided.\n");
            } else {
                printf("Pinging %s ...\n", ip);
                char command[512];
#ifdef _WIN32
                snprintf(command, sizeof(command), "ping -n 5 %s", ip);
#else
                snprintf(command, sizeof(command), "ping -c 5 %s", ip);
#endif
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
#ifdef _WIN32
                snprintf(command, sizeof(command), "findstr /S /I \"%s\" *", search_term);
#else
                snprintf(command, sizeof(command), "grep -R -I \"%s\" .", search_term);
#endif
                int ret = system(command);
                if (ret != 0) {
                    printf("Error: File search command failed or returned no matches.\n");
                }
            } else {
                printf("Error: Search string must be enclosed in double quotes.\n");
            }
        }
        else if (strcmp(input, "search hardware") == 0) {
#ifdef _WIN32
            printf("Hardware search is not supported on Windows in this version.\n");
#else
            printf("Gathering detailed hardware specs (with paging)...\n");
            system("rm -f /tmp/hwinfo.txt");
            system("echo \"=== lshw output ===\" >> /tmp/hwinfo.txt");
            system("lshw 2>/dev/null >> /tmp/hwinfo.txt");
            system("echo \"\n--- CPU Info (from /proc/cpuinfo) ---\" >> /tmp/hwinfo.txt");
            system("cat /proc/cpuinfo >> /tmp/hwinfo.txt");
            system("echo \"\n--- CPU Extended Info (lscpu) ---\" >> /tmp/hwinfo.txt");
            system("lscpu >> /tmp/hwinfo.txt");
            system("echo \"\n--- Memory Info (from /proc/meminfo) ---\" >> /tmp/hwinfo.txt");
            system("cat /proc/meminfo >> /tmp/hwinfo.txt");
            system("echo \"\n--- Memory Extended Info (free -h) ---\" >> /tmp/hwinfo.txt");
            system("free -h >> /tmp/hwinfo.txt");
            system("echo \"\n--- PCI Devices (basic) ---\" >> /tmp/hwinfo.txt");
            system("ls /sys/bus/pci/devices >> /tmp/hwinfo.txt");
            system("echo \"\n--- PCI Devices Extended Info (lspci -v) ---\" >> /tmp/hwinfo.txt");
            system("lspci -v >> /tmp/hwinfo.txt");
            system("echo \"\n--- USB Devices (basic) ---\" >> /tmp/hwinfo.txt");
            system("ls /sys/bus/usb/devices >> /tmp/hwinfo.txt");
            system("echo \"\n--- USB Devices Extended Info (lsusb -v) ---\" >> /tmp/hwinfo.txt");
            system("lsusb -v 2>/dev/null | head -n 50 >> /tmp/hwinfo.txt");
            system("echo \"\n--- Network Interfaces (from /proc/net/dev) ---\" >> /tmp/hwinfo.txt");
            system("cat /proc/net/dev >> /tmp/hwinfo.txt");
            system("echo \"\n--- Network Interfaces Extended Info (ip addr) ---\" >> /tmp/hwinfo.txt");
            system("ip addr >> /tmp/hwinfo.txt");
            system("echo \"\n--- Sensors Info (basic from hwmon) ---\" >> /tmp/hwinfo.txt");
            system("cat /sys/class/hwmon/hwmon*/temp* 2>/dev/null >> /tmp/hwinfo.txt");
            system("echo \"\n--- Sensors Extended Info (sensors) ---\" >> /tmp/hwinfo.txt");
            system("sensors 2>/dev/null >> /tmp/hwinfo.txt");
            system("echo \"\n--- Interrupts (from /proc/interrupts) ---\" >> /tmp/hwinfo.txt");
            system("cat /proc/interrupts >> /tmp/hwinfo.txt");
            system("echo \"\n--- I/O Ports (from /proc/ioports) ---\" >> /tmp/hwinfo.txt");
            system("cat /proc/ioports 2>/dev/null >> /tmp/hwinfo.txt");
            system("echo \"\n--- Battery Info (basic) ---\" >> /tmp/hwinfo.txt");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/status; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/status; else echo \"No battery found\"; fi' >> /tmp/hwinfo.txt");
            system("echo \"\n--- Battery Charge ---\" >> /tmp/hwinfo.txt");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/capacity; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/capacity; fi && echo \"%\"' >> /tmp/hwinfo.txt");
            system("echo \"\n--- Battery Extended Info (upower) ---\" >> /tmp/hwinfo.txt");
            system("upower -i $(upower -e | grep battery) 2>/dev/null >> /tmp/hwinfo.txt");
            system("less /tmp/hwinfo.txt");
            system("rm /tmp/hwinfo.txt");
#endif
        }
        else if (strcmp(input, "search hardware -short") == 0) {
#ifdef _WIN32
            printf("Hardware search is not supported on Windows in this version.\n");
#else
            printf("Searching concise hardware specs...\n");
            int ret = system("lshw -short 2>/dev/null");
            if (ret != 0) {
                printf("lshw not available. Displaying alternative concise hardware information...\n");
            }
            printf("\n--- CPU Info (concise) ---\n");
            system("lscpu | grep -E 'Architecture|Model name|CPU\\(s\\)|Thread|Core\\(s\\)'");
            printf("\n--- Memory Info (concise) ---\n");
            system("free -h");
            printf("\n--- PCI Devices (concise) ---\n");
            system("lspci | head -n 15");
            printf("\n--- USB Devices (concise) ---\n");
            system("lsusb | head -n 15");
            printf("\n--- Network Interfaces (concise) ---\n");
            system("ip -brief addr show");
            printf("\n--- Sensors (concise) ---\n");
            system("sensors | grep -E 'Core|Package'");
            printf("\n--- Battery Info (concise) ---\n");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/status; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/status; else echo \"No battery found\"; fi; echo -n \" Charge: \"; if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/capacity; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/capacity; fi; echo \"%\"'");
#endif
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
- Documentation for utilities: lshw, lscpu, free, lspci, lsusb, ip, sensors, and upower.
- Discussions and examples on using arp-scan, nmap, and other tools for LAN device discovery.
*/
