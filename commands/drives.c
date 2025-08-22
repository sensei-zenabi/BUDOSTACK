#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/*
Design principles:
- Use only standard C11 and its libraries.
- Keep all code in a single file (no header files).
- Implement custom utility functions if non-standard functions (like strdup) are required.
- Use opendir() and readdir() to scan /dev for block devices.
- Use stat() to check if an entry is a block device.
- Filter device names using common Linux naming conventions.
- Provide a user-friendly numbered list for drive selection.
- If a drive is mounted, display its mount point and provide basic navigation.
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

/* Find mount point of a device like "sda1" */
char *find_mount_point(const char *devname) {
    char devpath[256];
    snprintf(devpath, sizeof(devpath), "/dev/%s", devname);
    FILE *fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        return NULL;
    }
    char line[512];
    char device[256], mount[256];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%255s %255s", device, mount) == 2) {
            if (strcmp(device, devpath) == 0) {
                fclose(fp);
                return my_strdup(mount);
            }
        }
    }
    fclose(fp);
    return NULL;
}

/* List contents of the current directory */
void list_directory(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        printf("%s\n", e->d_name);
    }
    closedir(dir);
}

/* Simple navigation loop */
void navigate(void) {
    char input[256];
    char cwd[512];
    while (1) {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("[Current: %s]\n", cwd);
        }
        printf("Command (ls, cd <dir>, up, quit): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "quit") == 0) {
            break;
        } else if (strcmp(input, "ls") == 0) {
            list_directory(".");
        } else if (strncmp(input, "cd ", 3) == 0) {
            const char *dir = input + 3;
            if (chdir(dir) != 0) {
                perror("chdir");
            }
        } else if (strcmp(input, "up") == 0) {
            if (chdir("..") != 0) {
                perror("chdir");
            }
        } else {
            printf("Unknown command.\n");
        }
    }
}

int main(void) {
    DIR *dev_dir = opendir("/dev");
    if (dev_dir == NULL) {
        perror("Error opening /dev");
        return 1;
    }
    
    char **devices = NULL;
    char **mounts = NULL;
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

                char **temp2 = realloc(mounts, (count + 1) * sizeof(char *));
                if (temp2 == NULL) {
                    perror("Memory allocation failed");
                    free(devname);
                    closedir(dev_dir);
                    free(devices);
                    return 1;
                }
                mounts = temp2;

                devices[count] = devname;
                mounts[count] = find_mount_point(devname);
                count++;
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
        printf("%zu: /dev/%s", i + 1, devices[i]);
        if (mounts[i]) {
            printf(" (mounted at %s)", mounts[i]);
        } else {
            printf(" (not mounted)");
        }
        printf("\n");
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

    // Clear leftover input
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* discard */
    }

    // Display the selected drive.
    size_t idx = (size_t)(selection - 1);
    if (mounts[idx] == NULL) {
        printf("Selected device is not mounted.\n");
    } else {
        if (chdir(mounts[idx]) != 0) {
            perror("chdir");
        } else {
            navigate();
        }
    }

    // Free allocated memory.
    for (size_t i = 0; i < count; i++) {
        free(devices[i]);
        if (mounts[i]) {
            free(mounts[i]);
        }
    }
    free(devices);
    free(mounts);

    return 0;
}
