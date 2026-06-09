#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

// Function to get battery charge (if available)
// Returns battery capacity (0–100) if found, or -1 if no battery is found.
static int get_battery_charge(void) {
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

struct CpuTimes {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
};

struct CpuMeasurement {
    double mean;
    double min;
    double max;
    int samples;
};

static int read_cpu_stats(struct CpuTimes *stats) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        return -1;
    }

    int ret = fscanf(fp, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                     &stats->user, &stats->nice, &stats->system, &stats->idle,
                     &stats->iowait, &stats->irq, &stats->softirq, &stats->steal);
    fclose(fp);
    return (ret == 8) ? 0 : -1;
}

static void sleep_interval(long nanoseconds) {
    struct timespec remaining;
    struct timespec request;

    request.tv_sec = nanoseconds / 1000000000L;
    request.tv_nsec = nanoseconds % 1000000000L;

    while (nanosleep(&request, &remaining) == -1) {
        if (errno != EINTR) {
            return;
        }
        request = remaining;
    }
}

static int cpu_usage_between(const struct CpuTimes *before,
                             const struct CpuTimes *after,
                             double *usage) {
    unsigned long long total_before = before->user + before->nice + before->system +
                                      before->idle + before->iowait + before->irq +
                                      before->softirq + before->steal;
    unsigned long long total_after = after->user + after->nice + after->system +
                                     after->idle + after->iowait + after->irq +
                                     after->softirq + after->steal;
    unsigned long long idle_before = before->idle + before->iowait;
    unsigned long long idle_after = after->idle + after->iowait;
    unsigned long long delta_total = total_after - total_before;
    unsigned long long delta_idle = idle_after - idle_before;

    if (delta_total == 0 || delta_total < delta_idle) {
        return -1;
    }

    *usage = (double)(delta_total - delta_idle) * 100.0 / (double)delta_total;
    return 0;
}

static int measure_cpu_usage(struct CpuMeasurement *measurement) {
    enum {
        sample_count = 10
    };
    const long sample_interval_ns = 500000000L;
    struct CpuTimes before;
    double sum = 0.0;

    measurement->mean = 0.0;
    measurement->min = 0.0;
    measurement->max = 0.0;
    measurement->samples = 0;

    if (read_cpu_stats(&before) != 0) {
        return -1;
    }

    printf("Measuring CPU utilization for 5 seconds...\n");
    for (int sample = 1; sample <= sample_count; sample++) {
        struct CpuTimes after;
        double usage;

        sleep_interval(sample_interval_ns);
        if (read_cpu_stats(&after) != 0) {
            return -1;
        }
        if (cpu_usage_between(&before, &after, &usage) == 0) {
            if (measurement->samples == 0 || usage < measurement->min) {
                measurement->min = usage;
            }
            if (measurement->samples == 0 || usage > measurement->max) {
                measurement->max = usage;
            }
            sum += usage;
            measurement->samples++;
        }
        before = after;
        printf("Measurement progress: %d%%\n", sample * 10);
        fflush(stdout);
    }

    if (measurement->samples == 0) {
        return -1;
    }

    measurement->mean = sum / (double)measurement->samples;
    return 0;
}

int main(void) {
    time_t start_time = time(NULL);
    time_t now = start_time;
    struct tm *local = localtime(&now);
    char time_str[64];
    if (local && strftime(time_str, sizeof(time_str), "%H:%M:%S %d-%B-%Y", local)) {
        printf("Time: %s\n", time_str);
    }

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

    struct CpuMeasurement cpu_measurement;
    if (measure_cpu_usage(&cpu_measurement) == 0) {
        printf("CPU Utilization (5 sec): mean %.1f%% / min %.1f%% / max %.1f%%\n",
               cpu_measurement.mean, cpu_measurement.min, cpu_measurement.max);
    } else {
        printf("CPU Utilization (5 sec): N/A\n");
    }

    now = time(NULL);
    double elapsed = difftime(now, start_time);
    int hrs = (int)elapsed / 3600;
    int mins = ((int)elapsed % 3600) / 60;
    int secs = (int)elapsed % 60;
    printf("Runtime: %02d:%02d:%02d\n", hrs, mins, secs);

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

    return EXIT_SUCCESS;
}
