#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/statvfs.h>

// This program demonstrates a simple POSIX Linux application that:
// 1. Retrieves and formats the current system time.
// 2. Calculates free disk space on the root filesystem using statvfs().
// 3. Reads and displays the CPU temperature.
// The design emphasizes simplicity and clarity using only standard C libraries.

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
    // Print free disk space with one decimal place.
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

    return EXIT_SUCCESS;
}
