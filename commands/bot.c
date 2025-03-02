/* ChatGPT: DO NOT MODIFY OR REMOVE THIS HEADER BUT IMPLEMENT IT FULLY!

Filename: bot.c

Description:

	Implement a loop, where user is able to discuss with this chatbot.
	Include a command parser that recognizes familiar commands and create 
	a randomized default message to commands that are not recognized.
	If the user types "exit", the chatbot program needs to terminate.

Supported commands:

	help
	1. Displays all the supported commands (from below)

	search network
	1.Search and display all the MAC, IP addresses and device names from devices in the same network.

	ping <IP-address>
	1. Ping the device 5 times and report metrics from the results.

	search "string"
	1. Search and display all the files and their contents that contain the string from current folder and its subfolders.
    2. If the file is a binary file, do not search it's content, only filename.

	search hardware
	1. Search and display all connected hardware specs and devices from the device where the app is running
	2. Display the list so, that if a device is a child device it is nested under the parent device in the list.
    
    search hardware -short
	1. Search and display connected hardware specs in a concise, short format,
       including some key details from the Linux kernel virtual files and battery information (status and charge).

 Remarks:

	1. Functions defined as "extern" come from shared libraries and do not need to be implemented.
	2. Implement all above descriptions and requirements.
	3. Provide the fully updated source code.
	4. Do not delete or modify this header.

*/

/*
Design Principles and Implementation Notes:
- This program uses a continuous command loop to interact with the user.
- Commands are parsed using only standard string functions (<string.h>), ensuring cross-platform compatibility.
- Default responses are randomized by seeding the random number generator with the current time.
- The "search network" and "search \"string\"" functionalities are implemented by invoking system utilities:
    - For network search, the program calls "arp -a" to list network devices.
    - For file search, it calls a recursive search command:
        - On Unix-like systems: "grep -R -I \"<string>\" ." (the -I flag ignores binary files)
        - On Windows: "findstr /S /I \"<string>\" *"
- The "ping <IP-address>" command uses the system ping utility:
    - On Unix-like systems, it calls "ping -c 5 <IP-address>".
    - On Windows, it calls "ping -n 5 <IP-address>".
- The "search hardware" command on Unix-like systems has been modified to output detailed hardware information from multiple Linux kernel virtual files,
  including extended details (via utilities like lscpu, free, lspci, lsusb, ip, sensors, upower) and battery information.
  The battery info is obtained by checking common battery paths (e.g. /sys/class/power_supply/BAT0 or BAT1) and using upower.
- The "search hardware -short" command provides a concise summary using "lshw -short" and additional brief outputs from key commands,
  including battery status and charge.
- Conditional compilation is used to support both Windows and Unix‑like systems for clearing the console and executing commands.
- The code is written in plain C with the -std=c11 flag and uses only standard cross-platform libraries.
- The external function prettyprint is declared as extern and assumed to be provided by a shared library.
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

// Declaration of prettyprint, which prints a message with a delay between characters.
// This function is assumed to be provided externally via a shared library.
extern void prettyprint(const char *message, unsigned int delay_ms);

int main(void) {
    // Clear the console screen using the appropriate command.
    system(CLEAR_COMMAND);
    prettyprint("Hello User! How can I help you?\n", 25);

    // Seed the random number generator for default responses.
    srand((unsigned) time(NULL));

    // Buffer to hold user input.
    char input[256];

    // Main command loop: read and process commands until "exit" is entered.
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;  // Exit if input reading fails.
        }
        // Remove the trailing newline character, if present.
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        // Process the "exit" command.
        if (strcmp(input, "exit") == 0) {
            break;
        }
        // Process the "help" command.
        else if (strcmp(input, "help") == 0) {
            printf("Supported commands:\n");
            printf("help - Displays all the supported commands\n");
            printf("search network - Displays all the MAC, IP addresses and device names from devices in the same network (if possible)\n");
            printf("ping <IP-address> - Ping the device 5 times and report metrics from the results\n");
            printf("search \"string\" - Searches all the files and their contents that contain the string from the current folder and its subfolders\n");
            printf("search hardware - Displays detailed hardware specs from the current machine\n");
            printf("                 (including extended info for CPU, memory, PCI/USB devices, network interfaces, sensors, interrupts, I/O ports and battery info)\n");
            printf("search hardware -short - Displays a concise, summary version of the hardware specs\n");
        }
        // Process the "search network" command.
        else if (strcmp(input, "search network") == 0) {
            printf("Performing network search...\n");
            int ret = system("arp -a");
            if (ret != 0) {
                printf("Error: Network search command failed or is not supported on this system.\n");
            }
        }
        // Process the "ping" command.
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
        // Process commands that start with "search " expecting a quoted string.
        else if (strncmp(input, "search ", 7) == 0 && strchr(input, '\"') != NULL) {
            char *quote1 = strchr(input, '\"');
            char *quote2 = strchr(quote1 + 1, '\"');
            if (quote1 != NULL && quote2 != NULL) {
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
        // Process the "search hardware" command (detailed view).
        else if (strcmp(input, "search hardware") == 0) {
#ifdef _WIN32
            printf("Hardware search is not supported on Windows in this version.\n");
#else
            printf("Searching detailed hardware specs...\n");

            // Run lshw for basic hardware details.
            int ret = system("lshw 2>/dev/null");
            if (ret != 0) {
                printf("lshw not available. Displaying alternative hardware information...\n");
            }
            
            // Extended CPU Information.
            printf("\n--- CPU Info (from /proc/cpuinfo) ---\n");
            system("cat /proc/cpuinfo");
            printf("\n--- CPU Extended Info (lscpu) ---\n");
            system("lscpu");

            // Extended Memory Information.
            printf("\n--- Memory Info (from /proc/meminfo) ---\n");
            system("cat /proc/meminfo");
            printf("\n--- Memory Extended Info (free -h) ---\n");
            system("free -h");

            // Extended PCI Devices Information.
            printf("\n--- PCI Devices (basic) ---\n");
            system("ls /sys/bus/pci/devices");
            printf("\n--- PCI Devices Extended Info (lspci -v) ---\n");
            system("lspci -v");

            // Extended USB Devices Information.
            printf("\n--- USB Devices (basic) ---\n");
            system("ls /sys/bus/usb/devices");
            printf("\n--- USB Devices Extended Info (lsusb -v) ---\n");
            system("lsusb -v 2>/dev/null | head -n 50");

            // Extended Network Interfaces Information.
            printf("\n--- Network Interfaces (from /proc/net/dev) ---\n");
            system("cat /proc/net/dev");
            printf("\n--- Network Interfaces Extended Info (ip addr) ---\n");
            system("ip addr");

            // Extended Sensors Information.
            printf("\n--- Sensors Info (basic from hwmon) ---\n");
            system("cat /sys/class/hwmon/hwmon*/temp* 2>/dev/null");
            printf("\n--- Sensors Extended Info (sensors) ---\n");
            system("sensors 2>/dev/null");

            // Extended Interrupts Information.
            printf("\n--- Interrupts (from /proc/interrupts) ---\n");
            system("cat /proc/interrupts");

            // Extended I/O Ports Information.
            printf("\n--- I/O Ports (from /proc/ioports) ---\n");
            system("cat /proc/ioports 2>/dev/null");

            // Extended Battery Information.
            printf("\n--- Battery Info (basic) ---\n");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/status; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/status; else echo \"No battery found\"; fi'");
            printf("\n--- Battery Charge ---\n");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then cat /sys/class/power_supply/BAT0/capacity; elif [ -d /sys/class/power_supply/BAT1 ]; then cat /sys/class/power_supply/BAT1/capacity; fi && echo \"%\"'");
            printf("\n--- Battery Extended Info (upower) ---\n");
            system("upower -i $(upower -e | grep battery) 2>/dev/null");
#endif
        }
        // Process the "search hardware -short" command (concise view).
        else if (strcmp(input, "search hardware -short") == 0) {
#ifdef _WIN32
            printf("Hardware search is not supported on Windows in this version.\n");
#else
            printf("Searching concise hardware specs...\n");

            // Attempt to run lshw in short mode for a brief overview.
            int ret = system("lshw -short 2>/dev/null");
            if (ret != 0) {
                printf("lshw not available. Displaying alternative concise hardware information...\n");
            }
            // Append additional concise details.
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

            // Concise Battery Information: check both BAT0 and BAT1.
            printf("\n--- Battery Info (concise) ---\n");
            system("sh -c 'if [ -d /sys/class/power_supply/BAT0 ]; then "
                   "cat /sys/class/power_supply/BAT0/status; "
                   "elif [ -d /sys/class/power_supply/BAT1 ]; then "
                   "cat /sys/class/power_supply/BAT1/status; "
                   "else echo \"No battery found\"; fi; "
                   "echo -n \" Charge: \"; "
                   "if [ -d /sys/class/power_supply/BAT0 ]; then "
                   "cat /sys/class/power_supply/BAT0/capacity; "
                   "elif [ -d /sys/class/power_supply/BAT1 ]; then "
                   "cat /sys/class/power_supply/BAT1/capacity; fi; echo \"%\"'");
#endif
        }
        // For any unrecognized command, output a random default response.
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

    // Display farewell message.
    printf("Goodbye!\n");
    return 0;
}

/*
References:
- ISO C11 Standard Documentation for the C Standard Library functions.
- Linux kernel documentation for /proc and /sys virtual files.
- Documentation for lshw, lscpu, free, lspci, lsusb, ip, sensors, and upower.
- Various Linux resources on accessing detailed hardware information including battery status and charge.
*/
