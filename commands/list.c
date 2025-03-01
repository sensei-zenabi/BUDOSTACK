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
 * - Provide a configurable filter for file types (by extension) that are hidden unless "-a" is specified.
 *
 * ChatGPT Requirement #1:
 * Implement here a configurable list of file types that are not displayed unless the
 * list command is ran with argument list -a (meaning list all)
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

/* Global base path used in the comparator and filter function */
static const char *base_path;

/* Global flag to indicate if all files should be shown */
static int show_all = 0;

/* Configurable list of file extensions to be hidden unless "-a" is specified */
static const char *excluded_extensions[] = {
    ".c",
    ".h",
    ".o",
    ".gitignore",
    NULL
};

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
 * Filter function for scandir.
 *
 * Files whose names end with any extension in the excluded_extensions list are
 * filtered out unless the global flag show_all is set. Directories (and the entries
 * "." and "..") are always included.
 */
int filter(const struct dirent *entry) {
    /* Always include the current and parent directory entries */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        return 1;

    /* Use d_type if available to check if entry is a directory.
       If so, always include it. If d_type is unknown, fall back to stat(). */
    if (entry->d_type != DT_UNKNOWN) {
        if (entry->d_type == DT_DIR)
            return 1;
    } else {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                return 1;
        }
    }

    /* If the "-a" flag is provided, show all files */
    if (show_all)
        return 1;

    /* Otherwise, filter out files with excluded extensions */
    for (int i = 0; excluded_extensions[i] != NULL; i++) {
        size_t ext_len = strlen(excluded_extensions[i]);
        size_t name_len = strlen(entry->d_name);
        if (name_len >= ext_len && strcmp(entry->d_name + name_len - ext_len, excluded_extensions[i]) == 0)
            return 0; // Skip this file
    }
    return 1;
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
    /* Parse command-line arguments.
       If any argument equals "-a", set show_all flag.
       Any argument that is not "-a" is treated as the directory path.
       Default directory is "." if none provided.
    */
    const char *dir_path = ".";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else {
            dir_path = argv[i];
        }
    }
    base_path = dir_path;  // Set the global base path for comparator and filter

    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, filter, cmp_entries);
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
