/*
 * list.c
 *
 * This program lists the contents of directories and prints details for files.
 * It now supports internal wildcard expansion so that if a user types "list *"
 * (or any pattern containing wildcards), it behaves similarly to "ls *" in Linux.
 *
 * Enhancements:
 * - If a user types "list a", it will display all files starting with "a" from the current
 *   directory and all subfolders.
 * - Similarly, "list ab" displays all files starting with "ab" recursively.
 * - If a user types "list *ab*" it displays all files having "ab" in their filename recursively.
 * - The new recursive search functionality does not break existing functionality:
 *   if a literal file or directory exists and the argument does not contain a wildcard,
 *   it is listed normally.
 * - Added attribute "list -help" that prints this help message detailing all capabilities.
 *
 * Design principles:
 * - Use plain C (C11 standard) and only standard libraries.
 * - Preserve existing functionality and extend behavior for search patterns.
 * - Use recursion with opendir(), readdir(), and stat() for traversing subdirectories.
 * - Use fnmatch() for matching filenames against wildcard patterns.
 * - Maintain configurable exclusion list for file extensions, applied to non-directories unless "-a" is set.
 * - Include inline comments to explain design decisions.
 *
 * Compile with: gcc -std=c11 -o list list.c
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
#include <fnmatch.h>

// Global base path used in the comparator and filter function
static const char *base_path;

// Global flag to indicate if all files should be shown (if "-a" is provided)
static int show_all = 0;

// Configurable list of file extensions to be hidden unless "-a" is specified
static const char *excluded_extensions[] = {
    ".c",
    ".h",
    ".o",
    ".gitignore",
    ".md",
    NULL
};

/* Convert file mode to a permission string (similar to ls -l output) */
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
 * This function filters out files with certain excluded extensions unless the
 * global flag show_all is set. Directories (and the entries "." and "..") are always included.
 */
int filter(const struct dirent *entry) {
    /* Always include "." and ".." */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        return 1;

    /* Check if entry is a directory using d_type, or fall back to stat if necessary */
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

    /* If the "-a" flag is set, show all files */
    if (show_all)
        return 1;

    /* Otherwise, filter out files with excluded extensions */
    for (int i = 0; excluded_extensions[i] != NULL; i++) {
        size_t ext_len = strlen(excluded_extensions[i]);
        size_t name_len = strlen(entry->d_name);
        if (name_len >= ext_len && strcmp(entry->d_name + name_len - ext_len, excluded_extensions[i]) == 0)
            return 0; // Exclude this file
    }
    return 1;
}

/*
 * Custom comparator for scandir.
 *
 * Directories are sorted before files, and entries of the same type are sorted alphabetically.
 */
int cmp_entries(const struct dirent **a, const struct dirent **b) {
    int is_dir_a = 0, is_dir_b = 0;

#ifdef DT_DIR
    if ((*a)->d_type != DT_UNKNOWN) {
        is_dir_a = ((*a)->d_type == DT_DIR);
    } else {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, (*a)->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0)
            is_dir_a = S_ISDIR(st.st_mode);
    }
    if ((*b)->d_type != DT_UNKNOWN) {
        is_dir_b = ((*b)->d_type == DT_DIR);
    } else {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, (*b)->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0)
            is_dir_b = S_ISDIR(st.st_mode);
    }
#endif

    /* Directories come before files */
    if (is_dir_a && !is_dir_b)
        return -1;
    if (!is_dir_a && is_dir_b)
        return 1;

    /* If both are the same type, compare alphabetically */
    return strcmp((*a)->d_name, (*b)->d_name);
}

/*
 * Helper function to print file information for a given file path.
 *
 * This function prints the filename, permissions, size, and last modified time.
 */
void print_file_info(const char *filepath, const char *display_name) {
    struct stat st;
    if (stat(filepath, &st) == -1) {
        fprintf(stderr, "list: cannot access '%s': %s\n", filepath, strerror(errno));
        return;
    }
    char perms[11];
    mode_to_string(st.st_mode, perms);
    char timebuf[20];
    struct tm *tm_info = localtime(&st.st_mtime);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);

    /* If the entry is a directory and not "." or "..", append '/' */
    if (S_ISDIR(st.st_mode)) {
        if (strcmp(display_name, ".") != 0 && strcmp(display_name, "..") != 0) {
            char nameWithSlash[1024];
            snprintf(nameWithSlash, sizeof(nameWithSlash), "%s/", display_name);
            printf("%-30s %-11s %-10ld %-20s\n", nameWithSlash, perms, (long)st.st_size, timebuf);
            return;
        }
    }
    printf("%-30s %-11s %-10ld %-20s\n", display_name, perms, (long)st.st_size, timebuf);
}

/*
 * Function to list the contents of a directory specified by dir_path.
 *
 * It uses scandir() with the custom filter and comparator to list entries.
 */
void list_directory(const char *dir_path) {
    base_path = dir_path;  // Set global base path for filter and comparator

    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, filter, cmp_entries);
    if (n < 0) {
        fprintf(stderr, "list: cannot access directory '%s': %s\n", dir_path, strerror(errno));
        return;
    }

    /* Print header for directory listing */
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, namelist[i]->d_name);
        print_file_info(fullpath, namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);
}

/*
 * Recursive function to search directories for files matching a pattern.
 * Uses fnmatch() to compare filenames against the provided pattern.
 * Applies exclusion rules for file extensions unless "-a" is set.
 */
void recursive_search(const char *dir_path, const char *pattern) {
    DIR *dp = opendir(dir_path);
    if (!dp) {
        fprintf(stderr, "list: cannot access directory '%s': %s\n", dir_path, strerror(errno));
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) == -1)
            continue;
        // Check if the entry matches the pattern (always using fnmatch)
        if (fnmatch(pattern, entry->d_name, 0) == 0) {
            // Apply exclusion for files if not showing all
            if (!show_all && !S_ISDIR(st.st_mode)) {
                int excluded = 0;
                for (int i = 0; excluded_extensions[i] != NULL; i++) {
                    size_t ext_len = strlen(excluded_extensions[i]);
                    size_t name_len = strlen(entry->d_name);
                    if (name_len >= ext_len && strcmp(entry->d_name + name_len - ext_len, excluded_extensions[i]) == 0) {
                        excluded = 1;
                        break;
                    }
                }
                if (!excluded) {
                    print_file_info(fullpath, fullpath);
                }
            } else {
                print_file_info(fullpath, fullpath);
            }
        }
        // Recursively search inside directories.
        if (S_ISDIR(st.st_mode)) {
            recursive_search(fullpath, pattern);
        }
    }
    closedir(dp);
}

/*
 * Wrapper function to perform recursive search from the current directory.
 * Prints a header and then calls recursive_search.
 */
void list_recursive_search(const char *pattern) {
    printf("Recursive search for files matching pattern '%s':\n", pattern);
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");
    recursive_search(".", pattern);
}

/*
 * Function to display help message for the list command.
 */
void print_help() {
    printf("Usage: list [options] [file/directory or search pattern]\n");
    printf("Options:\n");
    printf("  -a      Show all files (override exclusion of certain file extensions)\n");
    printf("  -help   Display this help message\n");
    printf("\nCapabilities:\n");
    printf("  - If no arguments are provided, the current directory is listed.\n");
    printf("  - If a valid file or directory is provided (and does not contain wildcard characters),\n");
    printf("    it is listed with details.\n");
    printf("  - Wildcard patterns (e.g., \"*ab*\") are supported and will recursively search\n");
    printf("    the current directory and all subfolders for matching files.\n");
    printf("  - If a non-existent filename is provided (e.g., \"a\" or \"ab\"), it is treated as\n");
    printf("    a search pattern. For example, \"list a\" will display all files starting with 'a'\n");
    printf("    from the current directory and all subfolders.\n");
    printf("\nExisting functionality is preserved for backward compatibility.\n");
}

/*
 * Main function with extended capabilities:
 * - Processes command-line arguments.
 * - Supports recursive search for patterns that contain wildcard characters
 *   or do not correspond to existing files or directories.
 * - Maintains existing functionality for literal file/directory listing.
 */
int main(int argc, char *argv[]) {
    // If no arguments are provided, list the current directory.
    if (argc == 1) {
        list_directory(".");
        return EXIT_SUCCESS;
    }

    // Arrays to store literal file paths, directory paths, and search patterns.
    char **file_paths = malloc(sizeof(char*) * argc);
    char **dir_paths = malloc(sizeof(char*) * argc);
    char **search_patterns = malloc(sizeof(char*) * argc);
    if (!file_paths || !dir_paths || !search_patterns) {
        fprintf(stderr, "list: memory allocation failed\n");
        return EXIT_FAILURE;
    }
    int file_count = 0, dir_count = 0, search_count = 0;

    // Process each argument.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0) {
            print_help();
            free(file_paths);
            free(dir_paths);
            free(search_patterns);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
            continue;
        }
        /* If the argument contains a wildcard, treat it as a search pattern regardless
         * of whether a file/directory with that name exists.
         */
        if (strchr(argv[i], '*') || strchr(argv[i], '?') || strchr(argv[i], '[')) {
            search_patterns[search_count++] = strdup(argv[i]);
            continue;
        }
        struct stat st;
        // Check if the argument exists as a file or directory.
        if (stat(argv[i], &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                dir_paths[dir_count++] = strdup(argv[i]);
            } else {
                file_paths[file_count++] = strdup(argv[i]);
            }
        } else {
            // If the file/directory does not exist, treat it as a search pattern.
            search_patterns[search_count++] = strdup(argv[i]);
        }
    }

    // List literal files, if any.
    if (file_count > 0) {
        printf("Files:\n");
        printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
        printf("--------------------------------------------------------------------------------\n");
        for (int i = 0; i < file_count; i++) {
            print_file_info(file_paths[i], file_paths[i]);
            free(file_paths[i]);
        }
    }
    free(file_paths);

    // List literal directories, if any.
    for (int i = 0; i < dir_count; i++) {
        if (dir_count > 1 || file_count > 0) {
            printf("\n%s:\n", dir_paths[i]);
        }
        list_directory(dir_paths[i]);
        free(dir_paths[i]);
    }
    free(dir_paths);

    // Process search patterns for recursive search.
    for (int i = 0; i < search_count; i++) {
        char pattern[1024];
        // If the search pattern does not contain wildcard characters, treat it as a prefix search.
        if (!strchr(search_patterns[i], '*') && !strchr(search_patterns[i], '?') && !strchr(search_patterns[i], '[')) {
            snprintf(pattern, sizeof(pattern), "%s*", search_patterns[i]);
        } else {
            snprintf(pattern, sizeof(pattern), "%s", search_patterns[i]);
        }
        list_recursive_search(pattern);
        free(search_patterns[i]);
    }
    free(search_patterns);

    return EXIT_SUCCESS;
}
