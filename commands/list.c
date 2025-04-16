#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fnmatch.h>

// Global base path used in filter and comparator
static const char *base_path;

// Global flag to indicate if all files should be shown (if "-a" is provided)
static int show_all = 0;

// Configurable list of file extensions to be hidden unless "-a" is specified
static const char *excluded_extensions[] = {
    ".c",
    ".h",
    ".o",
    ".gitignore",
    NULL
};

// Dynamic array to collect matching file paths in recursive search
static char **matches = NULL;
static size_t matches_count = 0;
static size_t matches_capacity = 0;

// Convert file mode to a permission string (similar to ls -l output)
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

// Filter function for scandir
int filter(const struct dirent *entry) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        return 1;

    // Determine if entry is a directory using stat for portability
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, entry->d_name);
    struct stat st;
    if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
        return 1;

    if (show_all)
        return 1;

    for (int i = 0; excluded_extensions[i] != NULL; i++) {
        size_t ext_len = strlen(excluded_extensions[i]);
        size_t name_len = strlen(entry->d_name);
        if (name_len >= ext_len && strcmp(entry->d_name + name_len - ext_len, excluded_extensions[i]) == 0)
            return 0;
    }
    return 1;
}

// Custom comparator for scandir entries
int cmp_entries(const struct dirent **a, const struct dirent **b) {
    // Always compare names alphabetically
    return strcmp((*a)->d_name, (*b)->d_name);
}

// Print file information for a given path
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

    if (S_ISDIR(st.st_mode) && strcmp(display_name, ".") != 0 && strcmp(display_name, "..") != 0) {
        char nameWithSlash[1024];
        snprintf(nameWithSlash, sizeof(nameWithSlash), "%s/", display_name);
        printf("%-30s %-11s %-10ld %-20s\n", nameWithSlash, perms, (long)st.st_size, timebuf);
        return;
    }
    printf("%-30s %-11s %-10ld %-20s\n", display_name, perms, (long)st.st_size, timebuf);
}

// List a single directory (non-recursive)
void list_directory(const char *dir_path) {
    base_path = dir_path;
    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, filter, cmp_entries);
    if (n < 0) {
        fprintf(stderr, "list: cannot access directory '%s': %s\n", dir_path, strerror(errno));
        return;
    }

    printf("\n");
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, namelist[i]->d_name);
        print_file_info(fullpath, namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);
    printf("\n");
}

// Add a matching path to the dynamic array
void add_match(const char *path) {
    if (matches_count == matches_capacity) {
        size_t new_cap = matches_capacity ? matches_capacity * 2 : 64;
        char **tmp = realloc(matches, new_cap * sizeof(char*));
        if (!tmp) {
            perror("list: memory allocation failed");
            exit(EXIT_FAILURE);
        }
        matches = tmp;
        matches_capacity = new_cap;
    }
    matches[matches_count++] = strdup(path);
}

// Recursively collect all entries matching the pattern
void recursive_collect(const char *dir_path, const char *pattern) {
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
        if (fnmatch(pattern, entry->d_name, 0) == 0) {
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
                if (!excluded)
                    add_match(fullpath);
            } else {
                add_match(fullpath);
            }
        }
        if (S_ISDIR(st.st_mode)) {
            recursive_collect(fullpath, pattern);
        }
    }
    closedir(dp);
}

// Compare two strings for qsort
int cmp_str(const void *a, const void *b) {
    const char *const *pa = a;
    const char *const *pb = b;
    return strcmp(*pa, *pb);
}

// List files matching pattern recursively in alphabetical order
void list_recursive_search(const char *pattern) {
    free(matches);
    matches = NULL;
    matches_count = 0;
    matches_capacity = 0;

    recursive_collect(".", pattern);

    qsort(matches, matches_count, sizeof(char*), cmp_str);

    printf("Recursive search for files matching pattern '%s':\n", pattern);
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < matches_count; i++) {
        print_file_info(matches[i], matches[i]);
        free(matches[i]);
    }
    free(matches);
    matches = NULL;
    matches_count = matches_capacity = 0;
    printf("\n");
}

// Help message
void print_help() {
    printf("Usage: list [options] [file/directory or search pattern]\n");
    printf("Options:\n");
    printf("  -a      Show all files (override exclusion of certain file extensions)\n");
    printf("  -help   Display this help message\n\n");
    printf("Capabilities:\n");
    printf("  - If no arguments are provided, the current directory is listed.\n");
    printf("  - If a valid file or directory is provided (and does not contain wildcard characters),\n");
    printf("    it is listed with details.\n");
    printf("  - Wildcard patterns (e.g., \"*ab*\") are supported and will recursively search\n");
    printf("    the current directory and all subfolders for matching files.\n");
    printf("  - If a non-existent filename is provided (e.g., \"a\" or \"ab\"), it is treated as\n");
    printf("    a search pattern.\n\n");
    printf("Existing functionality is preserved for backward compatibility.\n");
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        list_directory(".");
        return EXIT_SUCCESS;
    }

    char **file_paths = malloc(sizeof(char*) * argc);
    char **dir_paths = malloc(sizeof(char*) * argc);
    char **search_patterns = malloc(sizeof(char*) * argc);
    if (!file_paths || !dir_paths || !search_patterns) {
        fprintf(stderr, "list: memory allocation failed\n");
        return EXIT_FAILURE;
    }
    int file_count = 0, dir_count = 0, search_count = 0;

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
        if (strchr(argv[i], '*') || strchr(argv[i], '?') || strchr(argv[i], '[')) {
            search_patterns[search_count++] = strdup(argv[i]);
            continue;
        }
        struct stat st;
        if (stat(argv[i], &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                dir_paths[dir_count++] = strdup(argv[i]);
            } else {
                file_paths[file_count++] = strdup(argv[i]);
            }
        } else {
            search_patterns[search_count++] = strdup(argv[i]);
        }
    }

    if (file_count == 0 && dir_count == 0 && search_count == 0) {
        list_directory(".");
        free(file_paths);
        free(dir_paths);
        free(search_patterns);
        return EXIT_SUCCESS;
    }

    if (file_count > 0) {
        printf("Files:\n");
        printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
        printf("--------------------------------------------------------------------------------\n");
        for (int i = 0; i < file_count; i++) {
            print_file_info(file_paths[i], file_paths[i]);
            free(file_paths[i]);
        }
        printf("\n");
    }
    free(file_paths);

    for (int i = 0; i < dir_count; i++) {
        if (dir_count > 1 || file_count > 0) {
            printf("\n%s:\n", dir_paths[i]);
        }
        list_directory(dir_paths[i]);
        free(dir_paths[i]);
    }
    free(dir_paths);

    for (int i = 0; i < search_count; i++) {
        char pattern[1024];
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
