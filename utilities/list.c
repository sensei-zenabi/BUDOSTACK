#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <fnmatch.h>

#define NAME_DISPLAY_WIDTH 39

static void print_separator(void) {
    const int total_width = NAME_DISPLAY_WIDTH + 44;
    for (int i = 0; i < total_width; i++) {
        putchar('-');
    }
    putchar('\n');
}

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

static void format_display_name(const char *input, char *output, size_t width) {
    if (width == 0) {
        if (output != NULL) {
            output[0] = '\0';
        }
        return;
    }

    size_t len = strlen(input);
    if (len <= width) {
        snprintf(output, width + 1, "%s", input);
        return;
    }

    if (width <= 3) {
        memset(output, '.', width);
        output[width] = '\0';
        return;
    }

    size_t copy_len = width - 3;
    memcpy(output, input, copy_len);
    memcpy(output + copy_len, "...", 3);
    output[width] = '\0';
}

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

static int entry_is_directory(const struct dirent *entry) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", base_path, entry->d_name);
    struct stat st;
    if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 1;
    }
    return 0;
}

// Filter function for scandir
int filter(const struct dirent *entry) {
    int is_dir = entry_is_directory(entry);

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        return show_all;

    if (!show_all && entry->d_name[0] == '.')
        return 0;

    // Determine if entry is a directory using stat for portability
    if (is_dir)
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
    int a_is_dir = entry_is_directory(*a);
    int b_is_dir = entry_is_directory(*b);

    if (a_is_dir != b_is_dir) {
        return b_is_dir - a_is_dir;
    }

    int case_insensitive = strcasecmp((*a)->d_name, (*b)->d_name);
    if (case_insensitive != 0) {
        return case_insensitive;
    }

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

    char formatted_name_buffer[1024];
    if (S_ISDIR(st.st_mode) && strcmp(display_name, ".") != 0 && strcmp(display_name, "..") != 0) {
        snprintf(formatted_name_buffer, sizeof(formatted_name_buffer), "-%s/", display_name);
    } else {
        snprintf(formatted_name_buffer, sizeof(formatted_name_buffer), "%s", display_name);
    }

    char truncated_name[NAME_DISPLAY_WIDTH + 1];
    format_display_name(formatted_name_buffer, truncated_name, NAME_DISPLAY_WIDTH);

    printf("%-*s %-11s %-10ld %-20s\n", NAME_DISPLAY_WIDTH, truncated_name, perms, (long)st.st_size, timebuf);
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
    printf("%-*s %-11s %-10s %-20s\n", NAME_DISPLAY_WIDTH, "Filename", "Permissions", "Size", "Last Modified");
    print_separator();

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

        if (!show_all && entry->d_name[0] == '.')
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
    int case_insensitive = strcasecmp(*pa, *pb);
    if (case_insensitive != 0) {
        return case_insensitive;
    }
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
    printf("%-*s %-11s %-10s %-20s\n", NAME_DISPLAY_WIDTH, "Filename", "Permissions", "Size", "Last Modified");
    print_separator();

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
// Help message
void print_help() {
    printf("Usage examples for the 'list' command:\n");
    printf("  list                 List contents of the current directory\n");
    printf("  list -a              List all files, including excluded extensions\n");
    printf("  list <file>          Show details for a specific file\n");
    printf("  list <directory>     List contents of a specific directory\n");
    printf("  list <pattern>*      Recursively list files matching a wildcard pattern\n");
    printf("  list -a <pattern>*   Recursive wildcard search including all files\n");
    printf("  list <file1> <file2> Show details for multiple files\n");
    printf("\n");
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
        printf("%-*s %-11s %-10s %-20s\n", NAME_DISPLAY_WIDTH, "Filename", "Permissions", "Size", "Last Modified");
        print_separator();
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
