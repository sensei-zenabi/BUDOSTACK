#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/*
Design principles:
- Use only standard C11 and its libraries.
- Keep all code in a single file (no header files).
- Implement custom utility functions if non-standard functions (like strdup) are required.
- Use opendir() and readdir() to scan /dev for block devices.
- Use stat() to check if an entry is a block device.
- Filter device names using common Linux naming conventions.
- Provide a user-friendly numbered list for drive selection.
- Simulate "going" to the drive by launching a new shell.
*/

/* Custom implementation of strdup for C11 compliance */
char *my_strdup(const char *s) {
    size_t len = strlen(s);
    char *dup = malloc(len + 1); // Allocate memory for the duplicate string.
    if (dup != NULL) {
        strcpy(dup, s); // Copy string content.
    }
    return dup;
}

int main(void) {
    DIR *dev_dir = opendir("/dev");
    if (dev_dir == NULL) {
        perror("Error opening /dev");
        return 1;
    }
    
    char **devices = NULL;
    size_t count = 0;
    struct dirent *entry;
    
    // Iterate over /dev directory entries.
    while ((entry = readdir(dev_dir)) != NULL) {
        char path[256];
        snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
            // Heuristic filter: names starting with "sd", "hd", "sr" or containing "cd" or "dvd".
            if (strncmp(entry->d_name, "sd", 2) == 0 ||
                strncmp(entry->d_name, "hd", 2) == 0 ||
                strncmp(entry->d_name, "sr", 2) == 0 ||
                strstr(entry->d_name, "cd") != NULL ||
                strstr(entry->d_name, "dvd") != NULL) {
                    
                char *devname = my_strdup(entry->d_name);
                if (devname == NULL) {
                    perror("Memory allocation failed");
                    closedir(dev_dir);
                    return 1;
                }
                char **temp = realloc(devices, (count + 1) * sizeof(char *));
                if (temp == NULL) {
                    perror("Memory allocation failed");
                    free(devname);
                    closedir(dev_dir);
                    return 1;
                }
                devices = temp;
                devices[count++] = devname;
            }
        }
    }
    closedir(dev_dir);
    
    // Inform the user if no drives are found.
    if (count == 0) {
        printf("No drives found.\n");
        return 0;
    }
    
    // Display the list of drives.
    printf("Found drives:\n");
    for (size_t i = 0; i < count; i++) {
        printf("%zu: /dev/%s\n", i + 1, devices[i]);
    }
    
    // Prompt user to select a drive.
    printf("Enter the number of the drive to select: ");
    int selection = 0;
    if (scanf("%d", &selection) != 1 || selection < 1 || selection > (int)count) {
        printf("Invalid selection.\n");
        for (size_t i = 0; i < count; i++) {
            free(devices[i]);
        }
        free(devices);
        return 1;
    }
    
    // Display the selected drive.
    printf("You selected: /dev/%s\n", devices[selection - 1]);
    
    // Free allocated memory.
    for (size_t i = 0; i < count; i++) {
        free(devices[i]);
    }
    free(devices);
    
    // Launch a new shell to simulate "going" to the selected drive.
    printf("Launching a new shell...\n");
    system("bash");
    
    return 0;
}
