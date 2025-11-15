#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * cleaner.c - Interactive cleanup for orphaned executables.
 *
 * The program discovers executable files under the BUDOSTACK root that do not
 * have a sibling source file with the same name plus the .c extension. For each
 * orphaned executable it prompts the user for confirmation before deleting the
 * file.
 */

static int prompt_delete(const char *display_path)
{
    char buffer[16];

    printf("Delete executable without source: %s? [y/N]: ", display_path);
    fflush(stdout);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        putchar('\n');
        return 0;
    }

    return buffer[0] == 'y' || buffer[0] == 'Y';
}

static const char *relative_path(const char *root, size_t root_len, const char *path)
{
    if (root_len == 0) {
        return path;
    }

    if (strncmp(path, root, root_len) != 0) {
        return path;
    }

    if (path[root_len] == '\0') {
        return path + root_len;
    }

    if (path[root_len] == '/') {
        return path + root_len + 1;
    }

    return path;
}

static int handle_entry(const char *path, const char *root, size_t root_len)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        perror(path);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        return 0;
    }

    if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        return 0;
    }

    char source_path[PATH_MAX];
    int needed = snprintf(source_path, sizeof(source_path), "%s.c", path);

    if (needed < 0 || (size_t)needed >= sizeof(source_path)) {
        fprintf(stderr, "Path too long: %s\n", path);
        return -1;
    }

    if (stat(source_path, &st) == 0) {
        return 0;
    }

    if (errno != ENOENT) {
        perror(source_path);
        return -1;
    }

    const char *display = relative_path(root, root_len, path);

    if (!prompt_delete(display)) {
        return 0;
    }

    if (remove(path) != 0) {
        perror(path);
    } else {
        printf("Removed %s\n", display);
    }

    return 0;
}

static int scan_directory(const char *directory, const char *root, size_t root_len)
{
    DIR *dir = opendir(directory);

    if (dir == NULL) {
        perror(directory);
        return -1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        if (strcmp(name, ".git") == 0) {
            continue;
        }

        char path[PATH_MAX];
        int needed = snprintf(path, sizeof(path), "%s/%s", directory, name);

        if (needed < 0 || (size_t)needed >= sizeof(path)) {
            fprintf(stderr, "Path too long: %s/%s\n", directory, name);
            closedir(dir);
            return -1;
        }

        struct stat st;

        if (lstat(path, &st) != 0) {
            perror(path);
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (S_ISLNK(st.st_mode)) {
                continue;
            }

            if (scan_directory(path, root, root_len) != 0) {
                closedir(dir);
                return -1;
            }

            continue;
        }

        if (handle_entry(path, root, root_len) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

static int determine_root(char *root, size_t size)
{
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);

    if (len < 0 || len >= (ssize_t)sizeof(exe_path)) {
        perror("/proc/self/exe");
        return -1;
    }

    exe_path[len] = '\0';

    char *slash = strrchr(exe_path, '/');

    if (slash == NULL) {
        fprintf(stderr, "Unable to determine executable directory\n");
        return -1;
    }

    *slash = '\0';

    slash = strrchr(exe_path, '/');

    if (slash == NULL) {
        fprintf(stderr, "Unable to determine repository root\n");
        return -1;
    }

    *slash = '\0';

    if (exe_path[0] == '\0') {
        exe_path[0] = '/';
        exe_path[1] = '\0';
    }

    if (realpath(exe_path, root) == NULL) {
        perror(exe_path);
        return -1;
    }

    if (strnlen(root, size) >= size) {
        fprintf(stderr, "Root path too long\n");
        return -1;
    }

    return 0;
}

int main(void)
{
    char root[PATH_MAX];

    if (determine_root(root, sizeof(root)) != 0) {
        return EXIT_FAILURE;
    }

    if (scan_directory(root, root, strlen(root)) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
