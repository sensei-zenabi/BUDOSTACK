#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#define BUFFER_SIZE 4096

// Helper function: Extracts basename from a path.
const char *get_basename(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? (base + 1) : path;
}

// Copies a single file from src to dest.
// Returns 0 on success, non-zero on error.
int copy_file(const char *src, const char *dest) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "Error opening source file '%s': %s\n", src, strerror(errno));
        return 1;
    }
    FILE *out = fopen(dest, "wb");
    if (!out) {
        fprintf(stderr, "Error opening destination file '%s': %s\n", dest, strerror(errno));
        fclose(in);
        return 1;
    }
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, in)) > 0) {
        if (fwrite(buffer, 1, bytes, out) != bytes) {
            fprintf(stderr, "Error writing to destination file '%s': %s\n", dest, strerror(errno));
            fclose(in);
            fclose(out);
            return 1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

// Recursively copies a directory from src to dest.
// Returns 0 on success, non-zero on error.
int copy_directory(const char *src, const char *dest) {
    // Create destination directory.
    if (mkdir(dest, 0755) != 0) {
        if (errno != EEXIST) {
            fprintf(stderr, "Error creating directory '%s': %s\n", dest, strerror(errno));
            return 1;
        }
    }

    DIR *dir = opendir(src);
    if (!dir) {
        fprintf(stderr, "Error opening source directory '%s': %s\n", src, strerror(errno));
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Construct full source and destination paths.
        char src_path[1024];
        char dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name);

        struct stat st;
        if (stat(src_path, &st) != 0) {
            fprintf(stderr, "Error getting status of '%s': %s\n", src_path, strerror(errno));
            closedir(dir);
            return 1;
        }
        if (S_ISDIR(st.st_mode)) {
            // Recursive call for subdirectory.
            if (copy_directory(src_path, dest_path) != 0) {
                closedir(dir);
                return 1;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Copy file.
            if (copy_file(src_path, dest_path) != 0) {
                closedir(dir);
                return 1;
            }
        } else {
            fprintf(stderr, "Skipping unsupported file type: '%s'\n", src_path);
        }
    }
    closedir(dir);
    return 0;
}

// Main function: determines whether source is a file or directory and acts accordingly.
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: copy <source> <destination>\n");
        return EXIT_FAILURE;
    }
    
    struct stat st_src, st_dest;
    if (stat(argv[1], &st_src) != 0) {
        fprintf(stderr, "Error accessing source '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    
    int dest_is_dir = 0;
    if (stat(argv[2], &st_dest) == 0 && S_ISDIR(st_dest.st_mode)) {
        dest_is_dir = 1;
    } else {
        // If destination does not exist, we can try to infer if it's a directory by checking for trailing '/'
        size_t len = strlen(argv[2]);
        if (argv[2][len - 1] == '/' || argv[2][len - 1] == '\\') {
            dest_is_dir = 1;
        }
    }
    
    // Handle file copy.
    if (S_ISREG(st_src.st_mode)) {
        char dest_path[1024];
        if (dest_is_dir) {
            // Append basename of source file to destination folder.
            const char *base = get_basename(argv[1]);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", argv[2], base);
        } else {
            strncpy(dest_path, argv[2], sizeof(dest_path));
            dest_path[sizeof(dest_path)-1] = '\0';
        }
        if (copy_file(argv[1], dest_path) != 0) {
            return EXIT_FAILURE;
        }
    } else if (S_ISDIR(st_src.st_mode)) {
        // Handle directory copy.
        char dest_path[1024];
        if (dest_is_dir) {
            // If destination is a directory, copy source folder into destination
            const char *base = get_basename(argv[1]);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", argv[2], base);
        } else {
            // Otherwise, treat destination as the new folder name.
            strncpy(dest_path, argv[2], sizeof(dest_path));
            dest_path[sizeof(dest_path)-1] = '\0';
        }
        if (copy_directory(argv[1], dest_path) != 0) {
            return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "Unsupported source type: '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
