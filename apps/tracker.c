#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct metric_stats {
    double min;
    double max;
    double sum;
    double sum_sq;
    size_t count;
};

struct wifi_sample {
    double link_quality;
    double signal_dbm;
    double noise_dbm;
};

struct scan_stats {
    size_t network_count;
    size_t hidden_count;
    size_t band24_count;
    size_t band5_count;
    size_t band6_count;
    size_t signal_count;
    double sum_signal;
    double strongest_signal;
    char strongest_ssid[128];
};

static void stats_init(struct metric_stats *stats)
{
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->sum = 0.0;
    stats->sum_sq = 0.0;
    stats->count = 0;
}

static void stats_add(struct metric_stats *stats, double value)
{
    if (!isfinite(value)) {
        return;
    }
    if (value < stats->min) {
        stats->min = value;
    }
    if (value > stats->max) {
        stats->max = value;
    }
    stats->sum += value;
    stats->sum_sq += value * value;
    stats->count++;
}

static double stats_average(const struct metric_stats *stats)
{
    if (stats->count == 0) {
        return NAN;
    }
    return stats->sum / (double)stats->count;
}

static double stats_stddev(const struct metric_stats *stats)
{
    if (stats->count < 2) {
        return NAN;
    }
    double mean = stats->sum / (double)stats->count;
    double variance = (stats->sum_sq / (double)stats->count) - (mean * mean);
    if (variance < 0.0) {
        variance = 0.0;
    }
    return sqrt(variance);
}

static void print_metric_summary(const char *label, const struct metric_stats *stats, const char *unit)
{
    if (stats->count == 0) {
        printf(" %s : No data\n", label);
        return;
    }

    double avg = stats_average(stats);
    double stddev = stats_stddev(stats);
    printf(" %s : avg=%.2f%s  min=%.2f%s  max=%.2f%s  stddev=",
           label,
           avg,
           unit ? unit : "",
           stats->min,
           unit ? unit : "",
           stats->max,
           unit ? unit : "");

    if (isnan(stddev)) {
        printf("N/A\n");
    } else {
        printf("%.2f%s\n", stddev, unit ? unit : "");
    }
}

static const char *detect_default_interface(void)
{
    static char iface[64];
    FILE *fp = fopen("/proc/net/wireless", "r");
    if (!fp) {
        return NULL;
    }

    char line[256];
    /* Skip headers */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return NULL;
    }
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char name[64];
        if (sscanf(line, " %63[^:]:", name) == 1) {
            strncpy(iface, name, sizeof(iface) - 1);
            iface[sizeof(iface) - 1] = '\0';
            fclose(fp);
            return iface;
        }
    }

    fclose(fp);
    return NULL;
}

static int read_wireless_sample(const char *iface, struct wifi_sample *sample)
{
    FILE *fp = fopen("/proc/net/wireless", "r");
    if (!fp) {
        return -1;
    }

    char line[256];
    /* Skip headers */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    int result = -1;
    while (fgets(line, sizeof(line), fp)) {
        char name[64];
        unsigned int status;
        double link, level, noise;
        int parsed = sscanf(line, " %63[^:]: %x %lf %lf %lf", name, &status, &link, &level, &noise);
        if (parsed == 5 && strcmp(name, iface) == 0) {
            sample->link_quality = link;
            sample->signal_dbm = level;
            sample->noise_dbm = noise;
            result = 0;
            break;
        }
    }

    fclose(fp);
    return result;
}

static void print_timestamp(char *buffer, size_t size)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        time_t fallback = time(NULL);
        struct tm tm_fallback;
        if (localtime_r(&fallback, &tm_fallback)) {
            strftime(buffer, size, "%H:%M:%S", &tm_fallback);
            return;
        }
        snprintf(buffer, size, "--:--:--");
        return;
    }
    struct tm tm;
    if (!localtime_r(&ts.tv_sec, &tm)) {
        snprintf(buffer, size, "--:--:--");
        return;
    }
    strftime(buffer, size, "%H:%M:%S", &tm);
}

static void init_scan_stats(struct scan_stats *stats)
{
    stats->network_count = 0;
    stats->hidden_count = 0;
    stats->band24_count = 0;
    stats->band5_count = 0;
    stats->band6_count = 0;
    stats->signal_count = 0;
    stats->sum_signal = 0.0;
    stats->strongest_signal = -INFINITY;
    stats->strongest_ssid[0] = '\0';
}

struct network_temp {
    bool in_use;
    bool has_signal;
    bool has_freq;
    double signal;
    double frequency;
    char ssid[128];
};

static void finalize_network(struct scan_stats *stats, struct network_temp *tmp)
{
    if (!tmp->in_use) {
        return;
    }

    stats->network_count++;
    if (tmp->ssid[0] == '\0') {
        stats->hidden_count++;
    }

    if (tmp->has_signal) {
        stats->sum_signal += tmp->signal;
        stats->signal_count++;
        if (tmp->signal > stats->strongest_signal) {
            stats->strongest_signal = tmp->signal;
            if (tmp->ssid[0] != '\0') {
                strncpy(stats->strongest_ssid, tmp->ssid, sizeof(stats->strongest_ssid) - 1);
                stats->strongest_ssid[sizeof(stats->strongest_ssid) - 1] = '\0';
            } else {
                strncpy(stats->strongest_ssid, "<hidden>", sizeof(stats->strongest_ssid) - 1);
                stats->strongest_ssid[sizeof(stats->strongest_ssid) - 1] = '\0';
            }
        }
    }

    if (tmp->has_freq) {
        if (tmp->frequency >= 5925.0) {
            stats->band6_count++;
        } else if (tmp->frequency >= 4900.0) {
            stats->band5_count++;
        } else if (tmp->frequency >= 2400.0) {
            stats->band24_count++;
        }
    }

    tmp->in_use = false;
    tmp->has_signal = false;
    tmp->has_freq = false;
    tmp->signal = 0.0;
    tmp->frequency = 0.0;
    tmp->ssid[0] = '\0';
}

static void trim_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void parse_scan_line(struct scan_stats *stats, struct network_temp *tmp, const char *line)
{
    if (strncmp(line, "BSS ", 4) == 0 || (strstr(line, "Cell ") && strstr(line, "Address:"))) {
        finalize_network(stats, tmp);
        tmp->in_use = true;
        tmp->ssid[0] = '\0';
        tmp->has_signal = false;
        tmp->has_freq = false;
        return;
    }

    const char *signal_prefix1 = "\tsignal:";
    const char *signal_prefix2 = "\t\tsignal:";
    const char *signal_prefix3 = "                    Signal level=";
    const char *freq_prefix1 = "\tfreq:";
    const char *freq_prefix2 = "\t\tfreq:";
    const char *freq_prefix3 = "                    Frequency:";
    const char *ssid_prefix1 = "\tSSID:";
    const char *ssid_prefix2 = "\t\tSSID:";
    const char *ssid_prefix3 = "                    ESSID:";

    if (strncmp(line, signal_prefix1, strlen(signal_prefix1)) == 0 ||
        strncmp(line, signal_prefix2, strlen(signal_prefix2)) == 0) {
        const char *value = strchr(line, ':');
        if (value) {
            value++;
            tmp->signal = strtod(value, NULL);
            tmp->has_signal = true;
            tmp->in_use = true;
        }
        return;
    }

    if (strncmp(line, signal_prefix3, strlen(signal_prefix3)) == 0) {
        const char *value = strstr(line, "Signal level=");
        if (value) {
            value += strlen("Signal level=");
            tmp->signal = strtod(value, NULL);
            tmp->has_signal = true;
            tmp->in_use = true;
        }
        return;
    }

    if (strncmp(line, freq_prefix1, strlen(freq_prefix1)) == 0 ||
        strncmp(line, freq_prefix2, strlen(freq_prefix2)) == 0) {
        const char *value = strchr(line, ':');
        if (value) {
            value++;
            tmp->frequency = strtod(value, NULL);
            tmp->has_freq = true;
            tmp->in_use = true;
        }
        return;
    }

    if (strncmp(line, freq_prefix3, strlen(freq_prefix3)) == 0) {
        const char *value = strstr(line, "Frequency:");
        if (value) {
            value += strlen("Frequency:");
            tmp->frequency = strtod(value, NULL) * 1000.0; /* Convert GHz -> MHz */
            tmp->has_freq = true;
            tmp->in_use = true;
        }
        return;
    }

    if (strncmp(line, ssid_prefix1, strlen(ssid_prefix1)) == 0 ||
        strncmp(line, ssid_prefix2, strlen(ssid_prefix2)) == 0) {
        const char *value = strchr(line, ':');
        if (value) {
            value++;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            strncpy(tmp->ssid, value, sizeof(tmp->ssid) - 1);
            tmp->ssid[sizeof(tmp->ssid) - 1] = '\0';
            trim_newline(tmp->ssid);
            tmp->in_use = true;
        }
        return;
    }

    if (strncmp(line, ssid_prefix3, strlen(ssid_prefix3)) == 0) {
        const char *start = strchr(line, '"');
        if (start) {
            start++;
            const char *end = strchr(start, '"');
            size_t len = end ? (size_t)(end - start) : strlen(start);
            if (len >= sizeof(tmp->ssid)) {
                len = sizeof(tmp->ssid) - 1;
            }
            memcpy(tmp->ssid, start, len);
            tmp->ssid[len] = '\0';
            tmp->in_use = true;
        }
        return;
    }
}

static int run_scan_command(const char *iface, const char *cmd, struct scan_stats *stats)
{
    char full_cmd[256];
    snprintf(full_cmd, sizeof(full_cmd), cmd, iface);
    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        return -1;
    }

    init_scan_stats(stats);
    struct network_temp tmp = { 0 };
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        parse_scan_line(stats, &tmp, line);
    }
    finalize_network(stats, &tmp);

    int status = pclose(fp);
    if (status == -1) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (stats->network_count == 0) {
            return -1;
        }
    }
    return 0;
}

static int perform_environment_scan(const char *iface, struct scan_stats *stats)
{
    const char *commands[] = {
        "iw dev %s scan",
        "iwlist %s scan",
        NULL
    };

    for (size_t i = 0; commands[i]; i++) {
        if (run_scan_command(iface, commands[i], stats) == 0 && stats->network_count > 0) {
            return 0;
        }
    }

    return -1;
}

static void print_scan_report(const struct scan_stats *stats)
{
    if (stats->network_count == 0) {
        printf("No networks discovered during scan.\n");
        return;
    }

    printf("Networks discovered: %zu\n", stats->network_count);
    printf("Hidden networks: %zu\n", stats->hidden_count);

    printf("Band usage: 2.4GHz=%zu  5GHz=%zu  6GHz=%zu\n",
           stats->band24_count, stats->band5_count, stats->band6_count);

    if (stats->signal_count > 0) {
        double avg_signal = stats->sum_signal / (double)stats->signal_count;
        printf("Average signal level: %.1f dBm\n", avg_signal);
    } else {
        printf("Average signal level: N/A\n");
    }

    if (stats->strongest_signal > -INFINITY) {
        printf("Strongest signal: %.1f dBm (%s)\n",
               stats->strongest_signal,
               stats->strongest_ssid[0] ? stats->strongest_ssid : "<hidden>");
    }
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [-i interface] [-c samples] [-p interval] [-s]\n", prog);
    printf("  -i interface  Monitor the specified wireless interface.\n");
    printf("  -c samples    Number of samples to record (default: 30).\n");
    printf("  -p interval   Pause in seconds between samples (default: 1).\n");
    printf("  -s            Perform a network environment scan after sampling.\n");
}

static void gather_link_information(const char *iface)
{
    const char *commands[] = {
        "iw dev %s link",
        "iwconfig %s",
        "nmcli -f IN-USE,SSID,BSSID,FREQ,SIGNAL dev wifi list | grep %s",
        NULL
    };

    for (size_t i = 0; commands[i]; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), commands[i], iface);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            continue;
        }

        char line[256];
        bool printed_header = false;
        bool has_output = false;
        while (fgets(line, sizeof(line), fp)) {
            if (!printed_header) {
                printf("Current link information (%s):\n", iface);
                printed_header = true;
            }
            fputs(line, stdout);
            has_output = true;
        }
        int status = pclose(fp);
        if (has_output && status != -1) {
            printf("\n");
            return;
        }
    }
}

int main(int argc, char **argv)
{
    const char *iface = NULL;
    long sample_count = 30;
    double interval = 1.0;
    bool do_scan = false;

    int opt;
    while ((opt = getopt(argc, argv, "i:c:p:sh")) != -1) {
        switch (opt) {
        case 'i':
            iface = optarg;
            break;
        case 'c': {
            char *end = NULL;
            errno = 0;
            long value = strtol(optarg, &end, 10);
            if (errno != 0 || !end || *end != '\0' || value <= 0) {
                fprintf(stderr, "Invalid sample count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            sample_count = value;
            break;
        }
        case 'p': {
            char *end = NULL;
            errno = 0;
            double value = strtod(optarg, &end);
            if (errno != 0 || !end || *end != '\0' || !isfinite(value) || value < 0.0) {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return EXIT_FAILURE;
            }
            interval = value;
            break;
        }
        case 's':
            do_scan = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    const char *detected = NULL;
    if (!iface) {
        detected = detect_default_interface();
        if (!detected) {
            fprintf(stderr, "Unable to detect a wireless interface. Use -i to specify one.\n");
            return EXIT_FAILURE;
        }
        iface = detected;
    }

    printf("Monitoring wireless interface: %s\n", iface);
    gather_link_information(iface);

    struct metric_stats link_stats;
    struct metric_stats signal_stats;
    struct metric_stats noise_stats;
    stats_init(&link_stats);
    stats_init(&signal_stats);
    stats_init(&noise_stats);

    struct wifi_sample sample;
    double secs_d = floor(interval);
    time_t secs = (time_t)secs_d;
    double fractional = interval - secs_d;
    if (fractional < 0.0) {
        fractional = 0.0;
    }
    long nsec = (long)(fractional * 1e9);
    if (nsec >= 1000000000L) {
        secs++;
        nsec -= 1000000000L;
    }
    if (nsec < 0) {
        nsec = 0;
    }
    struct timespec sleep_interval = {
        .tv_sec = secs,
        .tv_nsec = nsec
    };

    for (long i = 0; i < sample_count; i++) {
        if (read_wireless_sample(iface, &sample) == 0) {
            char timestamp[32];
            print_timestamp(timestamp, sizeof(timestamp));
            printf("[%s] Link=%5.1f/70  Signal=%6.1f dBm  Noise=%6.1f dBm\n",
                   timestamp,
                   sample.link_quality,
                   sample.signal_dbm,
                   sample.noise_dbm);

            stats_add(&link_stats, sample.link_quality);
            stats_add(&signal_stats, sample.signal_dbm);
            stats_add(&noise_stats, sample.noise_dbm);
        } else {
            fprintf(stderr, "Failed to read wireless statistics for %s\n", iface);
        }

        if (i + 1 < sample_count && interval > 0.0) {
            nanosleep(&sleep_interval, NULL);
        }
    }

    printf("\nSample summary for %s:\n", iface);
    print_metric_summary("Link quality", &link_stats, "");
    print_metric_summary("Signal level", &signal_stats, " dBm");
    print_metric_summary("Noise level", &noise_stats, " dBm");

    if (do_scan) {
        printf("\nPerforming environment scan...\n");
        struct scan_stats stats;
        if (perform_environment_scan(iface, &stats) == 0) {
            print_scan_report(&stats);
        } else {
            printf("Unable to perform wireless scan. Ensure required tools (iw or iwlist) are available.\n");
        }
    }

    return EXIT_SUCCESS;
}

