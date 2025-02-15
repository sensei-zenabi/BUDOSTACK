#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILES 1024

// Structure to store file details
typedef struct {
    char name[256];
    char extension[64];
    off_t size;
    time_t created;
    time_t modified;
    mode_t permissions;
} FileInfo;

// Function to extract file extension
const char *get_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    return (dot && dot != filename) ? dot + 1 : "";
}

// Comparison functions for sorting
int compare_name(const void *a, const void *b) {
    return strcmp(((FileInfo *)a)->name, ((FileInfo *)b)->name);
}

int compare_size(const void *a, const void *b) {
    return ((FileInfo *)a)->size - ((FileInfo *)b)->size;
}

int compare_created(const void *a, const void *b) {
    return ((FileInfo *)a)->created - ((FileInfo *)b)->created;
}

int compare_modified(const void *a, const void *b) {
    return ((FileInfo *)a)->modified - ((FileInfo *)b)->modified;
}

int compare_extension(const void *a, const void *b) {
    return strcmp(((FileInfo *)a)->extension, ((FileInfo *)b)->extension);
}

int main(int argc, char *argv[]) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    FileInfo files[MAX_FILES];
    int count = 0;
    int (*compare_func)(const void *, const void *) = compare_name;

    // Parse arguments safely
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) compare_func = compare_size;
        else if (strcmp(argv[i], "-c") == 0) compare_func = compare_created;
        else if (strcmp(argv[i], "-m") == 0) compare_func = compare_modified;
        else if (strcmp(argv[i], "-e") == 0) compare_func = compare_extension;
        else {
            fprintf(stderr, "Warning: Ignoring unknown argument '%s'\n", argv[i]);
        }
    }

    // Open current directory
    if ((dir = opendir(".")) == NULL) {
        perror("opendir");
        return EXIT_FAILURE;
    }

    // Read files and store info
    while ((entry = readdir(dir)) != NULL) {
        if (stat(entry->d_name, &file_stat) == -1) continue;
        strncpy(files[count].name, entry->d_name, 255);
        strncpy(files[count].extension, get_extension(entry->d_name), 63);
        files[count].size = file_stat.st_size;
        files[count].created = file_stat.st_ctime;
        files[count].modified = file_stat.st_mtime;
        files[count].permissions = file_stat.st_mode;
        count++;
    }
    closedir(dir);

    // Sort files
    qsort(files, count, sizeof(FileInfo), compare_func);

    // Print file info
    printf("%-30s %-10s %-10s %-20s %-20s %-10s\n", "Name", "Ext", "Size", "Created", "Modified", "Perms");
    printf("%s\n", "----------------------------------------------------------------------------------------------");
    for (int i = 0; i < count; i++) {
        char created_time[20], modified_time[20];
        strftime(created_time, sizeof(created_time), "%Y-%m-%d %H:%M:%S", localtime(&files[i].created));
        strftime(modified_time, sizeof(modified_time), "%Y-%m-%d %H:%M:%S", localtime(&files[i].modified));
        printf("%-30s %-10s %-10ld %-20s %-20s %o\n",
               files[i].name, files[i].extension, files[i].size, created_time, modified_time, files[i].permissions & 0777);
    }
    return EXIT_SUCCESS;
}
