#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define STATS_MIN_COLUMNS 60
#define STATS_LOW_COLUMNS 80
#define STATS_LOW_ROWS 45
#define STATS_BAR_WIDTH 24
#define STATS_MAX_PROCESSES 512
#define STATS_CMD_WIDTH 27
#define STATS_REFRESH_SECONDS 1

struct cpu_times {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
};

struct process_info {
    int pid;
    char state;
    unsigned long vmsize_kb;
    unsigned long rss_kb;
    unsigned long long cpu_ticks;
    char command[STATS_CMD_WIDTH + 1];
};

struct system_snapshot {
    struct cpu_times cpu;
    double cpu_percent;
    unsigned long mem_total_kb;
    unsigned long mem_available_kb;
    unsigned long swap_total_kb;
    unsigned long swap_free_kb;
    unsigned long disk_total_kb;
    unsigned long disk_free_kb;
    double uptime_seconds;
    double loadavg[3];
    int battery_percent;
    int cpu_temp_millic;
    int process_count;
    struct process_info processes[STATS_MAX_PROCESSES];
};

static struct termios saved_termios;
static int terminal_restored = 1;

static void restore_terminal(void)
{
    if (!terminal_restored) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
        printf("\033[?25h\033[0m\033[2J\033[H");
        fflush(stdout);
        terminal_restored = 1;
    }
}

static int enter_terminal(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return -1;
    }
    raw = saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return -1;
    }
    terminal_restored = 0;
    atexit(restore_terminal);
    printf("\033[?25l\033[2J\033[H");
    fflush(stdout);
    return 0;
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    restore_terminal();
    _exit(EXIT_FAILURE);
}

static unsigned long long cpu_total(const struct cpu_times *cpu)
{
    return cpu->user + cpu->nice + cpu->system + cpu->idle + cpu->iowait +
           cpu->irq + cpu->softirq + cpu->steal;
}

static unsigned long long cpu_idle(const struct cpu_times *cpu)
{
    return cpu->idle + cpu->iowait;
}

static int read_cpu_times(struct cpu_times *cpu)
{
    FILE *fp = fopen("/proc/stat", "r");
    int read_count;

    if (!fp) {
        return -1;
    }
    read_count = fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &cpu->user, &cpu->nice, &cpu->system, &cpu->idle,
                        &cpu->iowait, &cpu->irq, &cpu->softirq, &cpu->steal);
    fclose(fp);
    return read_count == 8 ? 0 : -1;
}

static double cpu_percent_between(const struct cpu_times *before,
                                  const struct cpu_times *after)
{
    unsigned long long total_before = cpu_total(before);
    unsigned long long total_after = cpu_total(after);
    unsigned long long idle_before = cpu_idle(before);
    unsigned long long idle_after = cpu_idle(after);
    unsigned long long total_delta = total_after - total_before;
    unsigned long long idle_delta = idle_after - idle_before;

    if (total_delta == 0 || idle_delta > total_delta) {
        return 0.0;
    }
    return (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
}

static void read_meminfo(struct system_snapshot *snapshot)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    char line[256];

    if (!fp) {
        return;
    }
    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "MemTotal: %lu kB", &snapshot->mem_total_kb);
        sscanf(line, "MemAvailable: %lu kB", &snapshot->mem_available_kb);
        sscanf(line, "SwapTotal: %lu kB", &snapshot->swap_total_kb);
        sscanf(line, "SwapFree: %lu kB", &snapshot->swap_free_kb);
    }
    fclose(fp);
}

static void read_uptime(struct system_snapshot *snapshot)
{
    FILE *fp = fopen("/proc/uptime", "r");

    if (!fp) {
        return;
    }
    if (fscanf(fp, "%lf", &snapshot->uptime_seconds) != 1) {
        snapshot->uptime_seconds = 0.0;
    }
    fclose(fp);
}

static void read_loadavg(struct system_snapshot *snapshot)
{
    FILE *fp = fopen("/proc/loadavg", "r");

    if (!fp) {
        return;
    }
    if (fscanf(fp, "%lf %lf %lf", &snapshot->loadavg[0], &snapshot->loadavg[1],
               &snapshot->loadavg[2]) != 3) {
        snapshot->loadavg[0] = 0.0;
        snapshot->loadavg[1] = 0.0;
        snapshot->loadavg[2] = 0.0;
    }
    fclose(fp);
}

static void read_disk(struct system_snapshot *snapshot)
{
    struct statvfs stat;

    if (statvfs("/", &stat) == 0) {
        snapshot->disk_total_kb = (unsigned long)((stat.f_blocks * stat.f_frsize) / 1024UL);
        snapshot->disk_free_kb = (unsigned long)((stat.f_bavail * stat.f_frsize) / 1024UL);
    }
}

static int read_battery(void)
{
    DIR *dir = opendir("/sys/class/power_supply");
    struct dirent *entry;
    int battery = -1;

    if (!dir) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        char type[64];
        FILE *fp;

        if (entry->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", entry->d_name);
        fp = fopen(path, "r");
        if (!fp) {
            continue;
        }
        if (fgets(type, sizeof(type), fp) && strncmp(type, "Battery", 7) == 0) {
            fclose(fp);
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", entry->d_name);
            fp = fopen(path, "r");
            if (fp) {
                if (fscanf(fp, "%d", &battery) != 1) {
                    battery = -1;
                }
                fclose(fp);
            }
            break;
        }
        fclose(fp);
    }
    closedir(dir);
    return battery;
}

static int read_cpu_temp(void)
{
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    int temp = -1;

    if (fp) {
        if (fscanf(fp, "%d", &temp) != 1) {
            temp = -1;
        }
        fclose(fp);
    }
    return temp;
}

static int parse_process_stat(int pid, struct process_info *process)
{
    char path[64];
    char line[1024];
    char *open_paren;
    char *close_paren;
    char *cursor;
    FILE *fp;
    unsigned long utime = 0UL;
    unsigned long stime = 0UL;
    long rss_pages = 0L;
    int field = 4;

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    open_paren = strchr(line, '(');
    close_paren = strrchr(line, ')');
    if (!open_paren || !close_paren || close_paren <= open_paren) {
        return -1;
    }
    *close_paren = '\0';
    snprintf(process->command, sizeof(process->command), "%s", open_paren + 1);
    cursor = close_paren + 2;
    process->state = *cursor;
    cursor += 2;

    while (*cursor != '\0') {
        char *end;
        unsigned long long value = strtoull(cursor, &end, 10);

        if (end == cursor) {
            break;
        }
        if (field == 14) {
            utime = (unsigned long)value;
        } else if (field == 15) {
            stime = (unsigned long)value;
        } else if (field == 23) {
            process->vmsize_kb = (unsigned long)(value / 1024ULL);
        } else if (field == 24) {
            rss_pages = (long)value;
            break;
        }
        field++;
        cursor = end;
        while (*cursor == ' ') {
            cursor++;
        }
    }
    process->pid = pid;
    process->cpu_ticks = (unsigned long long)utime + (unsigned long long)stime;
    process->rss_kb = rss_pages > 0 ? (unsigned long)rss_pages * (unsigned long)(sysconf(_SC_PAGESIZE) / 1024L) : 0UL;
    return 0;
}

static int compare_processes(const void *left, const void *right)
{
    const struct process_info *a = (const struct process_info *)left;
    const struct process_info *b = (const struct process_info *)right;

    if (a->cpu_ticks < b->cpu_ticks) {
        return 1;
    }
    if (a->cpu_ticks > b->cpu_ticks) {
        return -1;
    }
    if (a->rss_kb < b->rss_kb) {
        return 1;
    }
    if (a->rss_kb > b->rss_kb) {
        return -1;
    }
    return a->pid - b->pid;
}

static void read_processes(struct system_snapshot *snapshot)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;

    if (!dir) {
        return;
    }
    while ((entry = readdir(dir)) != NULL && snapshot->process_count < STATS_MAX_PROCESSES) {
        int pid = 0;
        const char *name = entry->d_name;

        while (*name) {
            if (!isdigit((unsigned char)*name)) {
                pid = -1;
                break;
            }
            pid = (pid * 10) + (*name - '0');
            name++;
        }
        if (pid > 0 && parse_process_stat(pid, &snapshot->processes[snapshot->process_count]) == 0) {
            snapshot->process_count++;
        }
    }
    closedir(dir);
    qsort(snapshot->processes, (size_t)snapshot->process_count,
          sizeof(snapshot->processes[0]), compare_processes);
}

static void read_snapshot(struct system_snapshot *snapshot,
                          const struct cpu_times *previous_cpu)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->battery_percent = -1;
    snapshot->cpu_temp_millic = -1;
    if (read_cpu_times(&snapshot->cpu) == 0 && previous_cpu) {
        snapshot->cpu_percent = cpu_percent_between(previous_cpu, &snapshot->cpu);
    }
    read_meminfo(snapshot);
    read_uptime(snapshot);
    read_loadavg(snapshot);
    read_disk(snapshot);
    snapshot->battery_percent = read_battery();
    snapshot->cpu_temp_millic = read_cpu_temp();
    read_processes(snapshot);
}

static void format_duration(double seconds, char *buffer, size_t size)
{
    int total = (int)seconds;
    int days = total / 86400;
    int hours = (total % 86400) / 3600;
    int minutes = (total % 3600) / 60;

    if (days > 0) {
        snprintf(buffer, size, "%dd %02dh %02dm", days, hours, minutes);
    } else {
        snprintf(buffer, size, "%02dh %02dm", hours, minutes);
    }
}

static void print_rule(int columns, const char *left, const char *fill,
                       const char *right)
{
    int index;

    fputs(left, stdout);
    for (index = 0; index < columns - 2; index++) {
        fputs(fill, stdout);
    }
    fputs(right, stdout);
    putchar('\n');
}

static void print_panel_line(int columns, const char *text)
{
    int content_width = columns - 4;

    if (content_width < 1) {
        content_width = STATS_LOW_COLUMNS - 4;
    }
    printf("| %-*.*s |\n", content_width, content_width, text);
}

static void print_bar(const char *label, double percent, int width, int columns)
{
    int filled;
    int index;

    if (percent < 0.0) {
        percent = 0.0;
    } else if (percent > 100.0) {
        percent = 100.0;
    }
    filled = (int)((percent * (double)width) / 100.0 + 0.5);
    char line[256];
    char bar[(STATS_BAR_WIDTH * 4) + 1];
    size_t offset = 0U;

    for (index = 0; index < width; index++) {
        const char *glyph;

        if (index < filled) {
            glyph = index % 2 == 0 ? "#" : "=";
        } else if (index == filled) {
            glyph = ">";
        } else {
            glyph = ".";
        }
        if (offset + strlen(glyph) < sizeof(bar)) {
            offset += (size_t)snprintf(bar + offset, sizeof(bar) - offset, "%s", glyph);
        }
    }
    bar[offset] = '\0';
    snprintf(line, sizeof(line), "%-5s [%s] %5.1f%%", label, bar, percent);
    print_panel_line(columns, line);
}

static void draw_snapshot(const struct system_snapshot *snapshot, int columns)
{
    char time_buffer[64];
    char uptime_buffer[32];
    time_t now = time(NULL);
    struct tm local_time;
    double mem_percent = 0.0;
    double swap_percent = 0.0;
    double disk_percent = 0.0;
    int rows = STATS_LOW_ROWS;
    int process_rows;
    int index;

    if (columns < STATS_MIN_COLUMNS) {
        columns = STATS_LOW_COLUMNS;
    }
    localtime_r(&now, &local_time);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S  %d-%b-%Y", &local_time);
    format_duration(snapshot->uptime_seconds, uptime_buffer, sizeof(uptime_buffer));
    if (snapshot->mem_total_kb > 0UL) {
        mem_percent = (double)(snapshot->mem_total_kb - snapshot->mem_available_kb) * 100.0 /
                      (double)snapshot->mem_total_kb;
    }
    if (snapshot->swap_total_kb > 0UL) {
        swap_percent = (double)(snapshot->swap_total_kb - snapshot->swap_free_kb) * 100.0 /
                       (double)snapshot->swap_total_kb;
    }
    if (snapshot->disk_total_kb > 0UL) {
        disk_percent = (double)(snapshot->disk_total_kb - snapshot->disk_free_kb) * 100.0 /
                       (double)snapshot->disk_total_kb;
    }

    printf("\033[H");
    print_rule(columns, "+", "=", "+");
    {
        char line[256];

        snprintf(line, sizeof(line), "MU/TH/UR 6000  NOSTROMO BIO-FUNCTION SCAN  %s", time_buffer);
        print_panel_line(columns, line);
    }
    print_rule(columns, "+", "-", "+");
    print_bar("CPU", snapshot->cpu_percent, STATS_BAR_WIDTH, columns);
    print_bar("MEM", mem_percent, STATS_BAR_WIDTH, columns);
    print_bar("SWAP", swap_percent, STATS_BAR_WIDTH, columns);
    print_bar("DISK", disk_percent, STATS_BAR_WIDTH, columns);
    print_rule(columns, "+", "-", "+");
    {
        char line[256];

        snprintf(line, sizeof(line), "LOAD %4.2f %4.2f %4.2f | UPTIME %-12s | PROC %-4d",
                 snapshot->loadavg[0], snapshot->loadavg[1], snapshot->loadavg[2],
                 uptime_buffer, snapshot->process_count);
        print_panel_line(columns, line);
    }
    {
        char line[256];
        char temp_text[32];
        char battery_text[16];

        if (snapshot->cpu_temp_millic >= 0) {
            snprintf(temp_text, sizeof(temp_text), "%5.1f C",
                     (double)snapshot->cpu_temp_millic / 1000.0);
        } else {
            snprintf(temp_text, sizeof(temp_text), "N/A");
        }
        if (snapshot->battery_percent >= 0) {
            snprintf(battery_text, sizeof(battery_text), "%3d%%", snapshot->battery_percent);
        } else {
            snprintf(battery_text, sizeof(battery_text), "N/A");
        }
        snprintf(line, sizeof(line), "TEMP %s | BAT %s | MEM %lu/%lu MB | ROOT FREE %lu MB",
                 temp_text, battery_text,
                 (snapshot->mem_total_kb - snapshot->mem_available_kb) / 1024UL,
                 snapshot->mem_total_kb / 1024UL, snapshot->disk_free_kb / 1024UL);
        print_panel_line(columns, line);
    }
    print_rule(columns, "+", "-", "+");
    {
        char line[256];

        snprintf(line, sizeof(line), "%5s  %s %9s %8s %8s  %-27s",
                 "PID", "S", "CPU-TICKS", "RSS-MB", "VSZ-MB", "COMMAND");
        print_panel_line(columns, line);
    }
    print_rule(columns, "+", "-", "+");

    process_rows = rows - 16;
    for (index = 0; index < process_rows; index++) {
        if (index < snapshot->process_count) {
            const struct process_info *process = &snapshot->processes[index];

            char line[256];

            snprintf(line, sizeof(line), "%5d  %c %9llu %8lu %8lu  %-27.27s",
                     process->pid, process->state, process->cpu_ticks,
                     process->rss_kb / 1024UL, process->vmsize_kb / 1024UL,
                     process->command);
            print_panel_line(columns, line);
        } else {
            print_panel_line(columns, "");
        }
    }
    print_rule(columns, "+", "-", "+");
    print_panel_line(columns, "[Q] QUIT  [R] REDRAW  [1s] AUTO-REFRESH  CRT GRID: 1979/LOW-RES");
    print_rule(columns, "+", "=", "+");
    fflush(stdout);
}

static int terminal_columns(void)
{
    struct winsize size;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col > STATS_LOW_COLUMNS ? STATS_LOW_COLUMNS : size.ws_col;
    }
    return STATS_LOW_COLUMNS;
}

static int wait_for_key_or_timeout(void)
{
    fd_set read_fds;
    struct timeval timeout;
    int result;

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    timeout.tv_sec = STATS_REFRESH_SECONDS;
    timeout.tv_usec = 0;
    result = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
    if (result > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
        unsigned char key;

        if (read(STDIN_FILENO, &key, sizeof(key)) == 1) {
            return key;
        }
    }
    return 0;
}

int main(void)
{
    struct system_snapshot snapshot;
    struct cpu_times previous_cpu;
    int have_previous = 0;
    int running = 1;

    if (enter_terminal() != 0) {
        perror("stats: terminal setup");
        return EXIT_FAILURE;
    }
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (running) {
        int key;

        read_snapshot(&snapshot, have_previous ? &previous_cpu : NULL);
        previous_cpu = snapshot.cpu;
        have_previous = 1;
        draw_snapshot(&snapshot, terminal_columns());
        key = wait_for_key_or_timeout();
        if (key == 'q' || key == 'Q') {
            running = 0;
        } else if (key == 'r' || key == 'R') {
            printf("\033[2J");
        }
    }

    restore_terminal();
    return EXIT_SUCCESS;
}
