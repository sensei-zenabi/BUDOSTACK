#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

struct CommandSpec {
    const char *title;
    const char *tool;
    const char *args;
    const char *description;
};

static void print_separator(void) {
    puts("============================================================");
}

static void print_section_header(const char *title) {
    putchar('\n');
    print_separator();
    printf("%s\n", title);
    print_separator();
}

static void trim_trailing_whitespace(char *text) {
    if (text == NULL) {
        return;
    }
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]) != 0) {
        text[length - 1] = '\0';
        length--;
    }
}

static bool read_binary_file(const char *path, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t total = fread(buffer, 1, buffer_size - 1, file);
    fclose(file);
    if (total == 0) {
        buffer[0] = '\0';
        return false;
    }
    buffer[total] = '\0';
    for (size_t i = 0; i < total; i++) {
        if (buffer[i] == '\0' || buffer[i] == '\n') {
            buffer[i] = ' ';
        }
    }
    trim_trailing_whitespace(buffer);
    return true;
}

static bool read_key_value(const char *path, const char *key, char *value, size_t value_size) {
    if (path == NULL || key == NULL || value == NULL || value_size == 0) {
        return false;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    size_t key_length = strlen(key);
    char line[512];
    bool found = false;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, key, key_length) != 0) {
            continue;
        }
        const char *separator = strchr(line, ':');
        if (separator == NULL) {
            continue;
        }
        separator++;
        while (*separator == ' ' || *separator == '\t') {
            separator++;
        }
        strncpy(value, separator, value_size - 1);
        value[value_size - 1] = '\0';
        trim_trailing_whitespace(value);
        found = true;
        break;
    }

    fclose(file);
    return found;
}

static bool command_exists(const char *name) {
    if (name == NULL || *name == '\0') {
        return false;
    }
    if (strchr(name, '/') != NULL) {
        return access(name, X_OK) == 0;
    }
    const char *path = getenv("PATH");
    if (path == NULL || *path == '\0') {
        return false;
    }

    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return false;
    }

    bool found = false;
    char *saveptr = NULL;
    for (char *token = strtok_r(path_copy, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)) {
        size_t candidate_length = strlen(token) + strlen(name) + 2;
        char *candidate = malloc(candidate_length);
        if (candidate == NULL) {
            continue;
        }
        int written = snprintf(candidate, candidate_length, "%s/%s", token, name);
        if (written >= 0 && (size_t)written < candidate_length && access(candidate, X_OK) == 0) {
            found = true;
            free(candidate);
            break;
        }
        free(candidate);
    }
    free(path_copy);
    return found;
}

static void stream_command(const struct CommandSpec *spec) {
    if (spec == NULL) {
        return;
    }

    print_section_header(spec->title);
    if (spec->description != NULL && *spec->description != '\0') {
        printf("%s\n\n", spec->description);
    }

    if (!command_exists(spec->tool)) {
        printf("[skipped] '%s' is not available in PATH.\n", spec->tool);
        return;
    }

    char command_buffer[512];
    if (spec->args != NULL && *spec->args != '\0') {
        int written = snprintf(command_buffer, sizeof(command_buffer), "%s %s", spec->tool, spec->args);
        if (written < 0 || (size_t)written >= sizeof(command_buffer)) {
            printf("[error] command string for %s is too long.\n", spec->tool);
            return;
        }
    } else {
        int written = snprintf(command_buffer, sizeof(command_buffer), "%s", spec->tool);
        if (written < 0 || (size_t)written >= sizeof(command_buffer)) {
            printf("[error] command string for %s is too long.\n", spec->tool);
            return;
        }
    }

    FILE *pipe = popen(command_buffer, "r");
    if (pipe == NULL) {
        printf("[error] unable to execute '%s': %s\n", command_buffer, strerror(errno));
        return;
    }

    char buffer[512];
    bool has_output = false;
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        fputs(buffer, stdout);
        has_output = true;
    }

    int status = pclose(pipe);
    if (!has_output) {
        puts("[info] command produced no output.");
    }
    if (status == -1) {
        printf("[warning] unable to obtain exit status for '%s': %s\n", command_buffer, strerror(errno));
    } else if (WIFEXITED(status) != 0) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            printf("[warning] '%s' exited with code %d.\n", command_buffer, exit_code);
        }
    } else if (WIFSIGNALED(status) != 0) {
        printf("[warning] '%s' terminated by signal %d.\n", command_buffer, WTERMSIG(status));
    }
}

static void print_system_overview(void) {
    print_section_header("System Overview");

    struct utsname info;
    if (uname(&info) == 0) {
        printf("Kernel        : %s %s\n", info.sysname, info.release);
        printf("Architecture  : %s\n", info.machine);
        printf("Hostname      : %s\n", info.nodename);
    } else {
        printf("Kernel        : (uname failed: %s)\n", strerror(errno));
    }

    char value[256];
    if (read_binary_file("/sys/devices/virtual/dmi/id/sys_vendor", value, sizeof(value))) {
        printf("Vendor        : %s\n", value);
    }
    if (read_binary_file("/sys/devices/virtual/dmi/id/product_name", value, sizeof(value))) {
        printf("Product       : %s\n", value);
    }
    if (read_binary_file("/sys/devices/virtual/dmi/id/board_name", value, sizeof(value))) {
        printf("Board         : %s\n", value);
    }
    if (read_binary_file("/sys/devices/virtual/dmi/id/bios_version", value, sizeof(value))) {
        printf("Firmware      : %s\n", value);
    }
    if (read_binary_file("/sys/firmware/devicetree/base/model", value, sizeof(value)) ||
        read_binary_file("/proc/device-tree/model", value, sizeof(value))) {
        printf("DT Model      : %s\n", value);
    }

    struct sysinfo sys;
    if (sysinfo(&sys) == 0) {
        long days = (long)(sys.uptime / 86400);
        long hours = (long)((sys.uptime % 86400) / 3600);
        long minutes = (long)((sys.uptime % 3600) / 60);
        printf("Uptime        : %ldd %ldh %ldm\n", days, hours, minutes);
    }
}

static void print_cpu_summary(void) {
    print_section_header("CPU Summary");

    char value[1024];
    if (read_key_value("/proc/cpuinfo", "model name", value, sizeof(value)) ||
        read_key_value("/proc/cpuinfo", "Hardware", value, sizeof(value))) {
        printf("Model         : %s\n", value);
    }

    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count > 0) {
        printf("Logical CPUs  : %ld\n", cpu_count);
    }

    if (read_key_value("/proc/cpuinfo", "cpu MHz", value, sizeof(value))) {
        printf("CPU MHz       : %s\n", value);
    } else if (read_key_value("/proc/cpuinfo", "BogoMIPS", value, sizeof(value))) {
        printf("BogoMIPS      : %s\n", value);
    }

    if (read_key_value("/proc/cpuinfo", "Features", value, sizeof(value)) ||
        read_key_value("/proc/cpuinfo", "flags", value, sizeof(value))) {
        bool virtualization = strstr(value, "hypervisor") != NULL;
        bool neon = strstr(value, "neon") != NULL || strstr(value, "asimd") != NULL;
        bool fpu = strstr(value, "fpu") != NULL;
        printf("Virtualization: %s\n", virtualization ? "detected" : "not detected");
        printf("Vector/FPU    : %s%s\n", neon ? "NEON " : "", fpu ? "FPU" : (neon ? "" : "not detected"));
        printf("Feature Flags : %s\n", value);
    }
}

static void print_memory_summary(void) {
    print_section_header("Memory Summary");

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        double total_gib = (double)info.totalram * (double)info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double free_gib = (double)info.freeram * (double)info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double swap_total_gib = (double)info.totalswap * (double)info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double swap_free_gib = (double)info.freeswap * (double)info.mem_unit / (1024.0 * 1024.0 * 1024.0);
        printf("Total RAM     : %.2f GiB\n", total_gib);
        printf("Free RAM      : %.2f GiB\n", free_gib);
        printf("Total Swap    : %.2f GiB\n", swap_total_gib);
        printf("Free Swap     : %.2f GiB\n", swap_free_gib);
    }

    char value[256];
    if (read_key_value("/proc/meminfo", "MemAvailable", value, sizeof(value))) {
        printf("MemAvailable  : %s\n", value);
    }
    if (read_key_value("/proc/meminfo", "HugePages_Total", value, sizeof(value))) {
        printf("HugePages     : %s\n", value);
    }
}

static void print_device_tree_overview(void) {
    print_section_header("Device Tree Overview");

    DIR *dir = opendir("/proc/device-tree");
    if (dir == NULL) {
        puts("/proc/device-tree is not available on this system.");
        return;
    }

    char value[256];
    if (read_binary_file("/proc/device-tree/model", value, sizeof(value))) {
        printf("Model         : %s\n", value);
    }
    if (read_binary_file("/proc/device-tree/compatible", value, sizeof(value))) {
        printf("Compatible    : %s\n", value);
    }

    puts("Top-level nodes:");
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        printf("  - %s\n", entry->d_name);
        count++;
    }
    if (count == 0) {
        puts("  (no entries found)");
    }
    closedir(dir);
}

static void print_filesystem_view(void) {
    print_section_header("Root Filesystem Insight");

    const char *paths[] = {
        "/etc/os-release",
        "/etc/fstab",
        "/proc/cmdline"
    };
    char buffer[512];
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *file = fopen(paths[i], "r");
        if (file == NULL) {
            continue;
        }
        printf("--- %s ---\n", paths[i]);
        while (fgets(buffer, sizeof(buffer), file) != NULL) {
            fputs(buffer, stdout);
        }
        puts("");
        fclose(file);
    }
}

int main(void) {
    puts("Hardware Capability Explorer");
    puts("Gathering host hardware information relevant to embedded development...\n");

    print_system_overview();
    print_cpu_summary();
    print_memory_summary();
    print_device_tree_overview();
    print_filesystem_view();

    const struct CommandSpec commands[] = {
        {
            .title = "Processor Topology (lscpu)",
            .tool = "lscpu",
            .args = "",
            .description = "Detailed CPU layout, caches, and ISA extensions."
        },
        {
            .title = "Block Devices (lsblk)",
            .tool = "lsblk",
            .args = "-o NAME,SIZE,TYPE,MOUNTPOINT,MODEL",
            .description = "Storage topology with sizes and mount points."
        },
        {
            .title = "PCI Devices (lspci)",
            .tool = "lspci",
            .args = "-nn",
            .description = "PCIe peripherals with vendor and device identifiers."
        },
        {
            .title = "USB Topology (lsusb)",
            .tool = "lsusb",
            .args = "-t",
            .description = "USB bus tree with driver information."
        },
        {
            .title = "Network Interfaces (ip)",
            .tool = "ip",
            .args = "-br address",
            .description = "Network interface summary including IPv4/IPv6 assignments."
        },
        {
            .title = "Wireless Capabilities (iwconfig)",
            .tool = "iwconfig",
            .args = "",
            .description = "Wireless PHY status and supported modes."
        },
        {
            .title = "Sensors (sensors)",
            .tool = "sensors",
            .args = "",
            .description = "Thermal and power telemetry (lm-sensors)."
        },
        {
            .title = "Loaded Kernel Modules (lsmod)",
            .tool = "lsmod",
            .args = "",
            .description = "Kernel modules can indicate enabled hardware drivers."
        }
    };

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        stream_command(&commands[i]);
    }

    return 0;
}
