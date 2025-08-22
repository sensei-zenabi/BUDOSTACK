#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <termios.h>
#include <sys/select.h>

// Function to get battery charge (if available)
// Returns battery capacity (0–100) if found, or -1 if no battery is found.
int get_battery_charge(void) {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    char type_path[512];
    char capacity_path[512];
    char type_str[64];
    int battery = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", entry->d_name);
        FILE *fp = fopen(type_path, "r");
        if (!fp)
            continue;
        if (fgets(type_str, sizeof(type_str), fp) != NULL) {
            type_str[strcspn(type_str, "\n")] = '\0';
            if (strcmp(type_str, "Battery") == 0) {
                fclose(fp);
                snprintf(capacity_path, sizeof(capacity_path), "/sys/class/power_supply/%s/capacity", entry->d_name);
                fp = fopen(capacity_path, "r");
                if (fp) {
                    if (fscanf(fp, "%d", &battery) != 1)
                        battery = -1;
                    fclose(fp);
                    break; // Use the first battery found
                }
            } else {
                fclose(fp);
            }
        } else {
            fclose(fp);
        }
    }
    closedir(dir);
    return battery;
}

static struct termios orig_termios;

static void reset_terminal_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void set_conio_terminal_mode(void) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

static int read_cpu_stats(unsigned long long *user, unsigned long long *nice,
                          unsigned long long *system, unsigned long long *idle,
                          unsigned long long *iowait, unsigned long long *irq,
                          unsigned long long *softirq, unsigned long long *steal) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1;
    int ret = fscanf(fp, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                     user, nice, system, idle, iowait, irq, softirq, steal);
    fclose(fp);
    return (ret == 8) ? 0 : -1;
}

int main(void) {
    set_conio_terminal_mode();
    atexit(reset_terminal_mode);

    time_t start_time = time(NULL);

    unsigned long long prev_user = 0, prev_nice = 0, prev_system = 0,
                       prev_idle = 0, prev_iowait = 0, prev_irq = 0,
                       prev_softirq = 0, prev_steal = 0;
    int have_prev = 0;

    while (1) {
        printf("\033[H\033[J");

        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        char time_str[64];
        if (local && strftime(time_str, sizeof(time_str), "%H:%M:%S %d-%B-%Y", local)) {
            printf("Time: %s\n", time_str);
        }

        double elapsed = difftime(now, start_time);
        int hrs = (int)elapsed / 3600;
        int mins = ((int)elapsed % 3600) / 60;
        int secs = (int)elapsed % 60;
        printf("Runtime: %02d:%02d:%02d\n", hrs, mins, secs);

        struct statvfs stat;
        if (statvfs("/", &stat) == 0) {
            unsigned long free_bytes = stat.f_bfree * stat.f_frsize;
            double free_gb = free_bytes / (1024.0 * 1024.0 * 1024.0);
            printf("Free Disk Space: %.1fGB\n", free_gb);
        }

        FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
        if (fp) {
            int temp_millideg;
            if (fscanf(fp, "%d", &temp_millideg) == 1) {
                double cpu_temp = temp_millideg / 1000.0;
                printf("CPU Temp: %.0f°C\n", cpu_temp);
            }
            fclose(fp);
        }

        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (read_cpu_stats(&user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 0) {
            if (have_prev) {
                unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
                unsigned long long prev_total = prev_user + prev_nice + prev_system + prev_idle + prev_iowait + prev_irq + prev_softirq + prev_steal;
                unsigned long long delta_total = total - prev_total;
                unsigned long long delta_idle = (idle + iowait) - (prev_idle + prev_iowait);
                double cpu_usage = delta_total ? (double)(delta_total - delta_idle) * 100.0 / delta_total : 0.0;
                printf("CPU Average Utilization: %.1f%%\n", cpu_usage);
            } else {
                printf("CPU Average Utilization: N/A\n");
                have_prev = 1;
            }
            prev_user = user; prev_nice = nice; prev_system = system;
            prev_idle = idle; prev_iowait = iowait; prev_irq = irq;
            prev_softirq = softirq; prev_steal = steal;
        }

        fp = fopen("/proc/uptime", "r");
        if (fp) {
            double uptime_seconds;
            if (fscanf(fp, "%lf", &uptime_seconds) == 1) {
                int days = (int)uptime_seconds / (24 * 3600);
                int hours = ((int)uptime_seconds % (24 * 3600)) / 3600;
                int minutes = (((int)uptime_seconds % 3600)) / 60;
                int seconds = (int)uptime_seconds % 60;
                printf("Uptime: ");
                int printed = 0;
                if (days > 0) {
                    printf("%d %s", days, (days == 1 ? "day" : "days"));
                    printed = 1;
                }
                if (hours > 0) {
                    if (printed)
                        printf(" ");
                    printf("%d %s", hours, (hours == 1 ? "hour" : "hours"));
                    printed = 1;
                }
                if (minutes > 0) {
                    if (printed)
                        printf(" ");
                    printf("%d %s", minutes, (minutes == 1 ? "minute" : "minutes"));
                    printed = 1;
                }
                if (printed)
                    printf(" and ");
                printf("%d %s\n", seconds, (seconds == 1 ? "second" : "seconds"));
            }
            fclose(fp);
        }

        fp = fopen("/proc/meminfo", "r");
        if (fp) {
            char line[256];
            unsigned long memTotal = 0, memAvailable = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (sscanf(line, "MemTotal: %lu kB", &memTotal) == 1)
                    continue;
                if (sscanf(line, "MemAvailable: %lu kB", &memAvailable) == 1)
                    continue;
            }
            fclose(fp);
            if (memTotal > 0) {
                double totalMB = memTotal / 1024.0;
                double usedMB = (memTotal - memAvailable) / 1024.0;
                double usedPercent = ((memTotal - memAvailable) * 100.0) / memTotal;
                printf("Memory Usage: %.1fMB used / %.1fMB total (%.1f%%)\n", usedMB, totalMB, usedPercent);
            }
        }

        int battery = get_battery_charge();
        if (battery >= 0) {
            printf("Battery Charge: %d%%\n", battery);
        } else {
            printf("Battery Charge: N/A\n");
        }

        printf("Press 'q' to quit.\n");
        fflush(stdout);

        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
        if (rv > 0) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0 && (ch == 'q' || ch == 'Q'))
                break;
        }
    }

    return EXIT_SUCCESS;
}
