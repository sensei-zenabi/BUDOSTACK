#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define FLOPPY_BYTES 1474560UL

static int accumulate_path(const char *path, off_t *total);

static int add_directory(const char *path, off_t *total)
{
    DIR *dir = opendir(path);

    if (dir == NULL) {
        fprintf(stderr, "floppycheck: failed to open directory '%s': %s\n", path, strerror(errno));
        return -1;
    }

    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);

        if (entry == NULL) {
            if (errno != 0) {
                int saved = errno;
                closedir(dir);
                fprintf(stderr, "floppycheck: error reading directory '%s': %s\n", path, strerror(saved));
                return -1;
            }

            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);
        int needs_separator = (path_len > 0 && path[path_len - 1] != '/');
        size_t total_len = path_len + needs_separator + name_len + 1;
        char *child_path = malloc(total_len);

        if (child_path == NULL) {
            fprintf(stderr, "floppycheck: out of memory building path\n");
            closedir(dir);
            return -1;
        }

        if (needs_separator) {
            snprintf(child_path, total_len, "%s/%s", path, entry->d_name);
        } else {
            snprintf(child_path, total_len, "%s%s", path, entry->d_name);
        }

        if (accumulate_path(child_path, total) != 0) {
            free(child_path);
            closedir(dir);
            return -1;
        }

        free(child_path);
    }

    if (closedir(dir) != 0) {
        fprintf(stderr, "floppycheck: failed to close directory '%s': %s\n", path, strerror(errno));
        return -1;
    }

    return 0;
}

static int accumulate_path(const char *path, off_t *total)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        fprintf(stderr, "floppycheck: cannot stat '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return add_directory(path, total);
    }

    *total += st.st_size;
    return 0;
}

static unsigned long disks_required(off_t total_bytes)
{
    if (total_bytes <= 0) {
        return 0;
    }

    return (unsigned long)((total_bytes + (off_t)FLOPPY_BYTES - 1) / (off_t)FLOPPY_BYTES);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file-or-directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *target = argv[1];
    off_t total_bytes = 0;

    if (accumulate_path(target, &total_bytes) != 0) {
        return EXIT_FAILURE;
    }

    unsigned long required = disks_required(total_bytes);
    double precise = 0.0;

    if (FLOPPY_BYTES > 0) {
        precise = (double)total_bytes / (double)FLOPPY_BYTES;
    }

    printf("\nTarget: %s\n", target);
    printf("Total size: %jd bytes\n", (intmax_t)total_bytes);
    printf("Standard 1.44MB floppy: %lu bytes\n", (unsigned long)FLOPPY_BYTES);
    printf("Exact usage: %.6f floppies\n", precise);
    printf("Disks required (rounded up): %lu\n", required);
    printf("\n");

    return EXIT_SUCCESS;
}
