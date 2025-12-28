#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void print_help(void) {
    printf("Usage: rename <source> <destination>\n\n");
    printf("Description:\n\n");
    printf("  Command to rename files and folders easily without hazzle. Handles\n");
    printf("  automatically creating the new file and/or folder and deleting the\n");
    printf("  old one.\n\n");
    printf("Arguments:\n\n");
    printf("  <source>       : File or folder to be renamed\n");
    printf("  <destination>  : New instance of the file or folder with a new name\n\n");
    printf("Example Use Cases:\n\n");
    printf("  Rename a file:\n\n");
    printf("    > rename ./myfile.txt ./newfile.txt\n\n");
    printf("    Result:\n\n");
    printf("      Renames the myfile.txt as newfile.txt and deletes the original\n");
    printf("      after new one has been created.\n\n");
    printf("  Rename a folder:\n\n");
    printf("    > rename ./documents/misc ./documents/exams\n\n");
    printf("    Result:\n\n");
    printf("      Renames the folder \"misc\" under documents as \"exams\" and deletes\n");
    printf("      the original after new folder has been created and all content\n");
    printf("      under it including nested folders have been moved under the new\n");
    printf("      folder.\n");
}

static char *join_paths(const char *base, const char *name) {
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    size_t total = base_len + 1 + name_len + 1;
    char *path = malloc(total);

    if (path == NULL)
        return NULL;

    if (snprintf(path, total, "%s/%s", base, name) < 0) {
        free(path);
        return NULL;
    }

    return path;
}

static int copy_file_contents(int src_fd, int dst_fd) {
    char buffer[8192];

    for (;;) {
        ssize_t bytes = read(src_fd, buffer, sizeof(buffer));

        if (bytes == 0)
            break;
        if (bytes < 0)
            return -1;

        ssize_t written = 0;
        while (written < bytes) {
            ssize_t chunk = write(dst_fd, buffer + written, (size_t)(bytes - written));

            if (chunk < 0)
                return -1;
            written += chunk;
        }
    }

    return 0;
}

static int copy_file(const char *src, const char *dst, mode_t mode) {
    int src_fd = -1;
    int dst_fd = -1;
    int status = -1;

    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("rename: open source");
        goto cleanup;
    }

    dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dst_fd < 0) {
        perror("rename: open destination");
        goto cleanup;
    }

    if (copy_file_contents(src_fd, dst_fd) != 0) {
        perror("rename: copy file");
        goto cleanup;
    }

    if (fchmod(dst_fd, mode) != 0) {
        perror("rename: chmod destination");
        goto cleanup;
    }

    status = 0;

cleanup:
    if (src_fd >= 0)
        close(src_fd);
    if (dst_fd >= 0)
        close(dst_fd);

    return status;
}

static int copy_symlink(const char *src, const char *dst) {
    char buffer[4096];
    ssize_t length = readlink(src, buffer, sizeof(buffer) - 1);

    if (length < 0) {
        perror("rename: readlink");
        return -1;
    }

    buffer[length] = '\0';

    if (symlink(buffer, dst) != 0) {
        perror("rename: symlink");
        return -1;
    }

    return 0;
}

static int copy_entry(const char *src, const char *dst, const struct stat *info);

static int copy_directory(const char *src, const char *dst, mode_t mode) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int status = -1;

    if (mkdir(dst, mode) != 0) {
        perror("rename: mkdir destination");
        return -1;
    }

    dir = opendir(src);
    if (dir == NULL) {
        perror("rename: opendir source");
        goto cleanup;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat entry_info;
        char *src_path = NULL;
        char *dst_path = NULL;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        src_path = join_paths(src, entry->d_name);
        dst_path = join_paths(dst, entry->d_name);

        if (src_path == NULL || dst_path == NULL) {
            fprintf(stderr, "rename: out of memory\n");
            free(src_path);
            free(dst_path);
            goto cleanup;
        }

        if (lstat(src_path, &entry_info) != 0) {
            perror("rename: lstat source entry");
            free(src_path);
            free(dst_path);
            goto cleanup;
        }

        if (copy_entry(src_path, dst_path, &entry_info) != 0) {
            free(src_path);
            free(dst_path);
            goto cleanup;
        }

        free(src_path);
        free(dst_path);
    }

    if (closedir(dir) != 0) {
        perror("rename: closedir source");
        dir = NULL;
        goto cleanup;
    }

    dir = NULL;
    status = 0;

cleanup:
    if (dir != NULL)
        closedir(dir);

    return status;
}

static int copy_entry(const char *src, const char *dst, const struct stat *info) {
    if (S_ISREG(info->st_mode)) {
        return copy_file(src, dst, info->st_mode & 0777);
    }

    if (S_ISDIR(info->st_mode)) {
        return copy_directory(src, dst, info->st_mode & 0777);
    }

    if (S_ISLNK(info->st_mode)) {
        return copy_symlink(src, dst);
    }

    fprintf(stderr, "rename: unsupported file type for %s\n", src);
    return -1;
}

static int remove_entry(const char *path, const struct stat *info);

static int remove_directory(const char *path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int status = -1;

    dir = opendir(path);
    if (dir == NULL) {
        perror("rename: opendir cleanup");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat entry_info;
        char *entry_path = NULL;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        entry_path = join_paths(path, entry->d_name);
        if (entry_path == NULL) {
            fprintf(stderr, "rename: out of memory\n");
            goto cleanup;
        }

        if (lstat(entry_path, &entry_info) != 0) {
            perror("rename: lstat cleanup");
            free(entry_path);
            goto cleanup;
        }

        if (remove_entry(entry_path, &entry_info) != 0) {
            free(entry_path);
            goto cleanup;
        }

        free(entry_path);
    }

    if (closedir(dir) != 0) {
        perror("rename: closedir cleanup");
        dir = NULL;
        goto cleanup;
    }

    dir = NULL;

    if (rmdir(path) != 0) {
        perror("rename: rmdir");
        goto cleanup;
    }

    status = 0;

cleanup:
    if (dir != NULL)
        closedir(dir);

    return status;
}

static int remove_entry(const char *path, const struct stat *info) {
    if (S_ISDIR(info->st_mode))
        return remove_directory(path);

    if (unlink(path) != 0) {
        perror("rename: unlink");
        return -1;
    }

    return 0;
}

static int ensure_destination_available(const char *destination) {
    struct stat info;

    if (lstat(destination, &info) == 0) {
        fprintf(stderr, "rename: destination already exists: %s\n", destination);
        return -1;
    }

    if (errno != ENOENT) {
        perror("rename: destination check");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    const char *source = NULL;
    const char *destination = NULL;
    struct stat info;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (argc != 3) {
        print_help();
        return 1;
    }

    source = argv[1];
    destination = argv[2];

    if (lstat(source, &info) != 0) {
        perror("rename: source");
        return 1;
    }

    if (ensure_destination_available(destination) != 0)
        return 1;

    if (rename(source, destination) == 0)
        return 0;

    if (errno != EXDEV) {
        perror("rename: rename");
        return 1;
    }

    if (copy_entry(source, destination, &info) != 0)
        return 1;

    if (remove_entry(source, &info) != 0)
        return 1;

    return 0;
}
