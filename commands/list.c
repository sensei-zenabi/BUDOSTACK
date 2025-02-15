/*
 * list.c
 *
 * This program lists the contents of a directory. It first lists directories
 * (appending a '/' after their names, except for "." and "..") and then files.
 * Both directories and files are sorted alphabetically. This is achieved by using
 * a custom comparator with scandir().
 *
 * Design principles:
 * - Use plain C (C11 standard) and only standard cross-platform libraries.
 * - Use a custom comparator to separate directories from files.
 * - Use d_type if available; otherwise, fall back to stat() to determine the file type.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static const char *base_path;  // Global base path used in the custom comparator

/* Convert file mode to a permission string (like ls -l) */
void mode_to_string(mode_t mode, char *str) {
    str[0] = S_ISDIR(mode) ? 'd' : (S_ISLNK(mode) ? 'l' : '-');
    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';
    str[10] = '\0';
}

/*
 * Custom comparator function for scandir.
 * Directories are sorted before files. If both entries are of the same type,
 * they are compared alphabetically.
 */
int cmp_entries(const struct dirent **a, const struct dirent **b) {
    int is_dir_a = 0, is_dir_b = 0;

    /* First, try using d_type if available */
    #ifdef DT_DIR
    if ((*a)->d_type != DT_UNKNOWN) {
        is_dir_a = ((*a)->d_type == DT_DIR);
    } else {
        /* Fall back to stat if d_type is unknown */
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, (*a)->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            is_dir_a = S_ISDIR(st.st_mode);
        }
    }
    if ((*b)->d_type != DT_UNKNOWN) {
        is_dir_b = ((*b)->d_type == DT_DIR);
    } else {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, (*b)->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            is_dir_b = S_ISDIR(st.st_mode);
        }
    }
    #endif

    /* Directories come before files */
    if (is_dir_a && !is_dir_b)
        return -1;
    if (!is_dir_a && is_dir_b)
        return 1;

    /* Both are the same type; compare alphabetically */
    return strcmp((*a)->d_name, (*b)->d_name);
}

int main(int argc, char *argv[]) {
    const char *dir_path = (argc < 2) ? "." : argv[1];
    base_path = dir_path;  // Set the global base path for the comparator

    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, NULL, cmp_entries);
    if (n < 0) {
        perror("scandir");
        return EXIT_FAILURE;
    }

    /* Print header */
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        struct dirent *entry = namelist[i];
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) == -1) {
            perror("stat");
            free(namelist[i]);
            continue;
        }

        char perms[11];
        mode_to_string(st.st_mode, perms);

        /* Format modification time */
        char timebuf[20];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);

        /* Append a '/' after directory names except for "." and ".." */
        if (S_ISDIR(st.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char nameWithSlash[1024];
                snprintf(nameWithSlash, sizeof(nameWithSlash), "%s/", entry->d_name);
                printf("%-30s %-11s %-10ld %-20s\n", nameWithSlash, perms, (long)st.st_size, timebuf);
            } else {
                printf("%-30s %-11s %-10ld %-20s\n", entry->d_name, perms, (long)st.st_size, timebuf);
            }
        } else {
            printf("%-30s %-11s %-10ld %-20s\n", entry->d_name, perms, (long)st.st_size, timebuf);
        }
        free(namelist[i]);
    }
    free(namelist);
    return EXIT_SUCCESS;
}
