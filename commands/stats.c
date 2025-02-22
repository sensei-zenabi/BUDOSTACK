#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

// Function to get battery charge (if available)
// Returns battery capacity (0–100) if found, or -1 if no battery is found.
int get_battery_charge(void) {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    char type_path[256];
    char capacity_path[256];
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

int main(void) {
    // *** Time Retrieval and Formatting ***
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        perror("time");
        return EXIT_FAILURE;
    }
    struct tm *local = localtime(&now);
    if (local == NULL) {
        perror("localtime");
        return EXIT_FAILURE;
    }
    char time_str[64];
    if (strftime(time_str, sizeof(time_str), "Time: %H:%M:%S %d-%B-%Y", local) == 0) {
        fprintf(stderr, "strftime returned 0");
        return EXIT_FAILURE;
    }
    printf("%s\n", time_str);

    // *** Free Disk Space Calculation ***
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        perror("statvfs failed");
        return EXIT_FAILURE;
    }
    unsigned long free_bytes = stat.f_bfree * stat.f_frsize;
    double free_gb = free_bytes / (1024.0 * 1024.0 * 1024.0);
    printf("Free Disk Space: %.1fGB\n", free_gb);

    // *** CPU Temperature Reading ***
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fp == NULL) {
        perror("fopen failed for CPU temperature");
        return EXIT_FAILURE;
    }
    int temp_millideg;
    if (fscanf(fp, "%d", &temp_millideg) != 1) {
        perror("fscanf failed for CPU temperature");
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);
    double cpu_temp = temp_millideg / 1000.0;
    printf("CPU Temp: %.0f°C\n", cpu_temp);

    // *** Average CPU Utilization ***
    {
        FILE *fp_stat;
        unsigned long long user1, nice1, system1, idle1, iowait1, irq1, softirq1, steal1;
        unsigned long long total1, idle_all1;
        unsigned long long user2, nice2, system2, idle2, iowait2, irq2, softirq2, steal2;
        unsigned long long total2, idle_all2;

        fp_stat = fopen("/proc/stat", "r");
        if (fp_stat) {
            if (fscanf(fp_stat, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user1, &nice1, &system1, &idle1, &iowait1, &irq1, &softirq1, &steal1) != 8) {
                fprintf(stderr, "Failed to parse /proc/stat\n");
                fclose(fp_stat);
                return EXIT_FAILURE;
            }
            fclose(fp_stat);
            total1 = user1 + nice1 + system1 + idle1 + iowait1 + irq1 + softirq1 + steal1;
            idle_all1 = idle1 + iowait1;

            sleep(1); // wait 1 second for delta

            fp_stat = fopen("/proc/stat", "r");
            if (fp_stat) {
                if (fscanf(fp_stat, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user2, &nice2, &system2, &idle2, &iowait2, &irq2, &softirq2, &steal2) != 8) {
                    fprintf(stderr, "Failed to parse /proc/stat (second read)\n");
                    fclose(fp_stat);
                    return EXIT_FAILURE;
                }
                fclose(fp_stat);
                total2 = user2 + nice2 + system2 + idle2 + iowait2 + irq2 + softirq2 + steal2;
                idle_all2 = idle2 + iowait2;

                unsigned long long delta_total = total2 - total1;
                unsigned long long delta_idle = idle_all2 - idle_all1;
                double cpu_usage = 0.0;
                if (delta_total > 0) {
                    cpu_usage = (double)(delta_total - delta_idle) * 100.0 / delta_total;
                }
                printf("CPU Average Utilization: %.1f%%\n", cpu_usage);
            } else {
                perror("fopen failed for /proc/stat (second read)");
            }
        } else {
            perror("fopen failed for /proc/stat (first read)");
        }
    }

    // *** System Uptime ***
    fp = fopen("/proc/uptime", "r");
    if (fp != NULL) {
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
        } else {
            fprintf(stderr, "Error reading uptime\n");
        }
        fclose(fp);
    } else {
        perror("fopen failed for /proc/uptime");
    }

    // *** Memory Usage ***
    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
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
        } else {
            fprintf(stderr, "Failed to read MemTotal from /proc/meminfo\n");
        }
    } else {
        perror("fopen failed for /proc/meminfo");
    }

    // *** Battery Charge Level ***
    int battery = get_battery_charge();
    if (battery >= 0) {
        printf("Battery Charge: %d%%\n", battery);
    } else {
        printf("Battery Charge: N/A\n");
    }

    return EXIT_SUCCESS;
}
