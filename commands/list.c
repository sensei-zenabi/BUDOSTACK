/*
 * list.c
 *
 * This program lists the contents of directories and prints details for files.
 * It now supports internal wildcard expansion so that if a user types "list *"
 * (or any pattern containing wildcards), it behaves similarly to "ls *" in Linux.
 *
 * Design principles:
 * - Use plain C (C11 standard) and only standard (POSIX) libraries.
 * - Use POSIX glob() for internal wildcard expansion if an argument contains
 *   wildcard characters ('*', '?', '[').
 * - For directories, list contents using scandir() with a custom filter and comparator.
 * - For files, print file details (permissions, size, last modified).
 * - Implement a configurable exclusion list for file extensions that are hidden unless "-a" is provided.
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
#include <glob.h>

/* Global base path used in the comparator and filter function */
static const char *base_path;

/* Global flag to indicate if all files should be shown (if "-a" is provided) */
static int show_all = 0;

/* Configurable list of file extensions to be hidden unless "-a" is specified */
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
 * Check if a string contains any wildcard characters.
 */
int contains_wildcard(const char *str) {
    return (strchr(str, '*') || strchr(str, '?') || strchr(str, '['));
}

/*
 * Main function with internal wildcard expansion.
 *
 * For each command-line argument (other than options), if it contains a wildcard,
 * use glob() with GLOB_NOCHECK to expand it to matching paths.
 * Then, separate the expanded paths into files and directories and list them.
 */
int main(int argc, char *argv[]) {
    /* First pass: expand wildcards in the arguments */
    int expanded_count = 0;
    /* Allocate enough space for expanded arguments. Worst-case: each argument expands to several paths. */
    char **expanded_args = malloc(sizeof(char*) * argc * 10);
    if (!expanded_args) {
        fprintf(stderr, "list: memory allocation failed\n");
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            /* Option handled later; copy it as is */
            expanded_args[expanded_count++] = strdup(argv[i]);
        } else if (contains_wildcard(argv[i])) {
            glob_t glob_result;
            /* GLOB_NOCHECK: if no matches, return the pattern itself */
            if (glob(argv[i], GLOB_NOCHECK, NULL, &glob_result) == 0) {
                for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                    expanded_args[expanded_count++] = strdup(glob_result.gl_pathv[j]);
                }
            } else {
                /* On glob() failure, use the original argument */
                expanded_args[expanded_count++] = strdup(argv[i]);
            }
            globfree(&glob_result);
        } else {
            expanded_args[expanded_count++] = strdup(argv[i]);
        }
    }

    /* If no non-option arguments provided, default to current directory */
    if (expanded_count == 0) {
        expanded_args[expanded_count++] = strdup(".");
    }

    /* Prepare separate lists for files and directories */
    char **file_paths = malloc(sizeof(char*) * expanded_count);
    char **dir_paths = malloc(sizeof(char*) * expanded_count);
    if (!file_paths || !dir_paths) {
        fprintf(stderr, "list: memory allocation failed\n");
        return EXIT_FAILURE;
    }
    int file_count = 0, dir_count = 0;

    /* Process each expanded argument */
    for (int i = 0; i < expanded_count; i++) {
        if (strcmp(expanded_args[i], "-a") == 0) {
            show_all = 1;
            free(expanded_args[i]);
            continue;
        }
        struct stat st;
        if (stat(expanded_args[i], &st) == -1) {
            fprintf(stderr, "list: cannot access '%s': %s\n", expanded_args[i], strerror(errno));
            free(expanded_args[i]);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            dir_paths[dir_count++] = expanded_args[i];
        } else {
            file_paths[file_count++] = expanded_args[i];
        }
    }
    free(expanded_args);

    /* First, list files (non-directory entries) if any */
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

    /* Next, list directories if any */
    for (int i = 0; i < dir_count; i++) {
        /* If more than one directory or if files were also listed, print a directory header */
        if (dir_count > 1 || file_count > 0) {
            printf("\n%s:\n", dir_paths[i]);
        }
        list_directory(dir_paths[i]);
        free(dir_paths[i]);
    }
    free(dir_paths);
    return EXIT_SUCCESS;
}
