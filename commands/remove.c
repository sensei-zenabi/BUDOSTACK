#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// Recursively remove a directory and its contents.
// Returns 0 on success, non-zero on error.
int remove_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Error opening directory '%s': %s\n", path, strerror(errno));
        return 1;
    }
    struct dirent *entry;
    int ret = 0;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries.
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) != 0) {
            fprintf(stderr, "Error accessing '%s': %s\n", fullpath, strerror(errno));
            ret = 1;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            // Recursively remove subdirectory.
            if (remove_directory(fullpath) != 0) {
                ret = 1;
            }
        } else {
            // Remove file.
            if (remove(fullpath) != 0) {
                fprintf(stderr, "Error removing file '%s': %s\n", fullpath, strerror(errno));
                ret = 1;
            }
        }
    }
    closedir(dir);
    // Finally remove the directory itself.
    if (rmdir(path) != 0) {
        fprintf(stderr, "Error removing directory '%s': %s\n", path, strerror(errno));
        ret = 1;
    }
    return ret;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: remove <path>\n");
        return EXIT_FAILURE;
    }
    struct stat st;
    if (stat(argv[1], &st) != 0) {
        fprintf(stderr, "Error accessing '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    if (S_ISDIR(st.st_mode)) {
        // Remove directory and its contents.
        if (remove_directory(argv[1]) != 0) {
            return EXIT_FAILURE;
        }
    } else {
        // Remove single file.
        if (remove(argv[1]) != 0) {
            perror("Error removing file");
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
