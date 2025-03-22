/*
 * move.c - A simple move command that supports moving files and directories.
 *
 * Design principles:
 * - Use plain C (C11) with only standard libraries.
 * - Use rename() when possible; if that fails due to different filesystems (EXDEV) 
 *   or missing destination directories, perform a manual copy and delete.
 * - For directories, recursively copy the contents and then remove the source.
 * - Create destination parent directories if they do not exist.
 *
 * Compile with: cc -std=c11 -o move move.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>  // For rmdir

#define BUFFER_SIZE 8192

// Custom strdup implementation using standard C library functions.
static char *c_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy)
        memcpy(copy, s, len);
    return copy;
}

// Function to create parent directories recursively.
int create_parent_dirs(const char *path) {
    char *path_copy = c_strdup(path);
    if (!path_copy) {
        perror("c_strdup failed");
        return -1;
    }
    // Remove the trailing file component.
    for (int i = (int)strlen(path_copy) - 1; i >= 0; i--) {
        if (path_copy[i] == '/') {
            path_copy[i] = '\0';
            break;
        }
    }
    // If there is no directory part, nothing to do.
    if (strlen(path_copy) == 0) {
        free(path_copy);
        return 0;
    }
    // Recursively create parent directories.
    char temp[1024] = {0};
    char *p = path_copy;
    // If the path starts with '/', include it.
    if (p[0] == '/') {
        strcpy(temp, "/");
        p++;
    }
    while (*p) {
        char *slash = strchr(p, '/');
        if (slash) {
            strncat(temp, p, slash - p);
            strcat(temp, "/");
            p = slash + 1;
        } else {
            strcat(temp, p);
            p += strlen(p);
        }
        if (mkdir(temp, 0755) != 0) {
            if (errno != EEXIST) {
                perror("mkdir failed");
                free(path_copy);
                return -1;
            }
        }
    }
    free(path_copy);
    return 0;
}

// Function to copy a file from src to dest.
int copy_file(const char *src, const char *dest) {
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) {
        perror("Error opening source file");
        return -1;
    }
    // Ensure destination parent directories exist.
    if (create_parent_dirs(dest) != 0) {
        fclose(fsrc);
        return -1;
    }
    FILE *fdest = fopen(dest, "wb");
    if (!fdest) {
        perror("Error opening destination file");
        fclose(fsrc);
        return -1;
    }
    char buffer[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, BUFFER_SIZE, fsrc)) > 0) {
        if (fwrite(buffer, 1, n, fdest) != n) {
            perror("Error writing to destination file");
            fclose(fsrc);
            fclose(fdest);
            return -1;
        }
    }
    fclose(fsrc);
    fclose(fdest);
    return 0;
}

// Recursive function to move a file or directory from src to dest.
int move_item(const char *src, const char *dest) {
    struct stat statbuf;
    if (stat(src, &statbuf) != 0) {
        perror("Error stating source");
        return -1;
    }
    // If source is a directory, handle recursively.
    if (S_ISDIR(statbuf.st_mode)) {
        // Create destination directory.
        if (create_parent_dirs(dest) != 0) {
            return -1;
        }
        if (mkdir(dest, 0755) != 0) {
            if (errno != EEXIST) {
                perror("Error creating destination directory");
                return -1;
            }
        }
        // Open source directory.
        DIR *dir = opendir(src);
        if (!dir) {
            perror("Error opening source directory");
            return -1;
        }
        struct dirent *entry;
        int result = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char src_path[1024], dest_path[1024];
            snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name);
            if (move_item(src_path, dest_path) != 0) {
                result = -1;
                break;
            }
        }
        closedir(dir);
        if (rmdir(src) != 0) {
            perror("Error removing source directory");
            result = -1;
        }
        return result;
    } else {
        // Source is a file.
        if (rename(src, dest) == 0) {
            return 0;
        } else {
            if (errno == EXDEV || errno == ENOENT) {
                if (copy_file(src, dest) != 0) {
                    return -1;
                }
                if (remove(src) != 0) {
                    perror("Error removing source file after copy");
                    return -1;
                }
                return 0;
            } else {
                perror("Error moving file");
                return -1;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: move <source> <destination>\n");
        return EXIT_FAILURE;
    }
    if (move_item(argv[1], argv[2]) != 0) {
        fprintf(stderr, "Move operation failed.\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
