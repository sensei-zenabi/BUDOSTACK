#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <glob.h>
#include <limits.h>

#define BUFFER_SIZE 8192

typedef enum {
    ACTION_COPY,
    ACTION_MOVE,
    ACTION_DELETE
} Action;

static const char *get_basename(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? (base + 1) : path;
}

static char *c_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

static int prompt_yes_no(const char *message) {
    static FILE *tty = NULL;
    char buffer[8];
    if (!tty) {
        tty = fopen("/dev/tty", "r+");
    }
    FILE *io = tty ? tty : stdin;
    fprintf(tty ? tty : stdout, "%s [y/N]: ", message);
    fflush(tty ? tty : stdout);
    if (!fgets(buffer, sizeof(buffer), io)) {
        return 0;
    }
    if (!strchr(buffer, '\n')) {
        int ch = 0;
        while ((ch = fgetc(io)) != '\n' && ch != EOF) {
        }
    }
    return (buffer[0] == 'y' || buffer[0] == 'Y');
}

static int create_parent_dirs(const char *path) {
    char *path_copy = c_strdup(path);
    if (!path_copy) {
        perror("malloc failed");
        return -1;
    }

    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        return 0;
    }
    *last_slash = '\0';
    if (path_copy[0] == '\0') {
        free(path_copy);
        return 0;
    }

    char temp[PATH_MAX] = "";
    const char *p = path_copy;
    if (p[0] == '/') {
        strcpy(temp, "/");
        p++;
    }

    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (strlen(temp) + len + 2 >= sizeof(temp)) {
            fprintf(stderr, "Path too long while creating directories: %s\n", path);
            free(path_copy);
            return -1;
        }
        strncat(temp, p, len);
        if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
            perror("mkdir failed");
            free(path_copy);
            return -1;
        }
        if (!slash) {
            break;
        }
        strcat(temp, "/");
        p = slash + 1;
    }

    free(path_copy);
    return 0;
}

static int copy_file(const char *src, const char *dest, int force) {
    struct stat st;
    if (stat(dest, &st) == 0) {
        if (!force) {
            char question[PATH_MAX + 64];
            snprintf(question, sizeof(question), "Destination file exists: '%s'. Overwrite?", dest);
            if (!prompt_yes_no(question)) {
                return 0;
            }
        }
    }

    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "Error opening source file '%s': %s\n", src, strerror(errno));
        return -1;
    }
    if (create_parent_dirs(dest) != 0) {
        fclose(in);
        return -1;
    }
    FILE *out = fopen(dest, "wb");
    if (!out) {
        fprintf(stderr, "Error opening destination file '%s': %s\n", dest, strerror(errno));
        fclose(in);
        return -1;
    }
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, bytes, out) != bytes) {
            fprintf(stderr, "Error writing to destination file '%s': %s\n", dest, strerror(errno));
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int copy_item(const char *src, const char *dest, int force);

static int copy_directory(const char *src, const char *dest, int force) {
    struct stat st_dest;
    if (stat(dest, &st_dest) == 0) {
        if (!S_ISDIR(st_dest.st_mode)) {
            fprintf(stderr, "Destination exists and is not a directory: %s\n", dest);
            return -1;
        }
        if (!force) {
            char question[PATH_MAX + 64];
            snprintf(question, sizeof(question), "Destination directory exists: '%s'. Merge contents?", dest);
            if (!prompt_yes_no(question)) {
                return 0;
            }
        }
    } else {
        if (mkdir(dest, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Error creating directory '%s': %s\n", dest, strerror(errno));
            return -1;
        }
    }

    DIR *dir = opendir(src);
    if (!dir) {
        fprintf(stderr, "Error opening source directory '%s': %s\n", src, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[PATH_MAX];
        char dest_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name);

        if (copy_item(src_path, dest_path, force) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int copy_item(const char *src, const char *dest, int force) {
    struct stat st_src;
    if (stat(src, &st_src) != 0) {
        fprintf(stderr, "Error accessing source '%s': %s\n", src, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st_src.st_mode)) {
        return copy_directory(src, dest, force);
    }
    if (S_ISREG(st_src.st_mode)) {
        return copy_file(src, dest, force);
    }

    fprintf(stderr, "Skipping unsupported source type: '%s'\n", src);
    return 0;
}

static int delete_item(const char *path, int force);

static int delete_directory(const char *path, int force) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Error opening directory '%s': %s\n", path, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (delete_item(fullpath, force) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);

    if (!force) {
        char question[PATH_MAX + 64];
        snprintf(question, sizeof(question), "Delete directory '%s'?", path);
        if (!prompt_yes_no(question)) {
            return 0;
        }
    }
    if (rmdir(path) != 0) {
        fprintf(stderr, "Error removing directory '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int delete_item(const char *path, int force) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "Error accessing '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return delete_directory(path, force);
    }

    if (!force) {
        char question[PATH_MAX + 64];
        if (S_ISLNK(st.st_mode)) {
            snprintf(question, sizeof(question), "Delete link '%s'?", path);
        } else {
            snprintf(question, sizeof(question), "Delete file '%s'?", path);
        }
        if (!prompt_yes_no(question)) {
            return 0;
        }
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "Error removing file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int move_item(const char *src, const char *dest, int force, int *skipped) {
    struct stat st_src;
    if (stat(src, &st_src) != 0) {
        fprintf(stderr, "Error accessing source '%s': %s\n", src, strerror(errno));
        return -1;
    }

    struct stat st_dest;
    int dest_exists = (stat(dest, &st_dest) == 0);
    if (dest_exists && !force) {
        char question[PATH_MAX + 64];
        if (S_ISDIR(st_dest.st_mode)) {
            snprintf(question, sizeof(question), "Destination directory exists: '%s'. Merge contents?", dest);
        } else {
            snprintf(question, sizeof(question), "Destination file exists: '%s'. Overwrite?", dest);
        }
        if (!prompt_yes_no(question)) {
            *skipped = 1;
            return 0;
        }
    }

    if (rename(src, dest) == 0) {
        *skipped = 0;
        return 0;
    }

    if (errno != EXDEV && errno != ENOENT && errno != EEXIST && errno != ENOTEMPTY) {
        perror("Error moving item");
        return -1;
    }

    if (S_ISDIR(st_src.st_mode)) {
        int any_skipped = 0;
        if (create_parent_dirs(dest) != 0) {
            return -1;
        }
        if (mkdir(dest, 0755) != 0 && errno != EEXIST) {
            perror("Error creating destination directory");
            return -1;
        }

        DIR *dir = opendir(src);
        if (!dir) {
            perror("Error opening source directory");
            return -1;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char src_path[PATH_MAX];
            char dest_path[PATH_MAX];
            snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name);
            int entry_skipped = 0;
            if (move_item(src_path, dest_path, force, &entry_skipped) != 0) {
                closedir(dir);
                return -1;
            }
            if (entry_skipped) {
                any_skipped = 1;
            }
        }
        closedir(dir);
        if (!any_skipped) {
            if (rmdir(src) != 0) {
                perror("Error removing source directory");
                return -1;
            }
        }
        *skipped = any_skipped;
        return 0;
    }

    if (copy_file(src, dest, 1) != 0) {
        return -1;
    }
    if (remove(src) != 0) {
        perror("Error removing source file after copy");
        return -1;
    }
    *skipped = 0;
    return 0;
}

static void print_help(FILE *stream) {
    fprintf(stream, "Usage:  do -action <source> <destination> -f\n");
    fprintf(stream, "\n");
    fprintf(stream, "Description:\n");
    fprintf(stream, "\n");
    fprintf(stream, "  Generic command to copy, move, and delete files and folders.\n");
    fprintf(stream, "  Prompts in case of conflicts found and lets user decide\n");
    fprintf(stream, "  independently regarding every file how to resolve. In case of\n");
    fprintf(stream, "  delete, prompts before each delete. Supports various search\n");
    fprintf(stream, "  capabilities, including *.*, *.txt, note.*, *note.*, note*.*,\n");
    fprintf(stream, "  *note*.*, *note.txt, note*.txt, *note*.txt, note.ex*, note.*xe,\n");
    fprintf(stream, "  etc...\n");
    fprintf(stream, "\n");
    fprintf(stream, "Arguments:\n");
    fprintf(stream, "\n");
    fprintf(stream, "  -action : cp = copy; mv = move; del=delete\n");
    fprintf(stream, "  -f      : (Optional) If used, uses brute force and does not prompt.\n");
    fprintf(stream, "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(stderr);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help(stdout);
        return EXIT_SUCCESS;
    }

    Action action;
    if (strcmp(argv[1], "-cp") == 0) {
        action = ACTION_COPY;
    } else if (strcmp(argv[1], "-mv") == 0) {
        action = ACTION_MOVE;
    } else if (strcmp(argv[1], "-del") == 0) {
        action = ACTION_DELETE;
    } else {
        print_help(stderr);
        return EXIT_FAILURE;
    }

    int force = 0;
    const char *args[2];
    int arg_count = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            force = 1;
        } else {
            if (arg_count < 2) {
                args[arg_count++] = argv[i];
            } else {
                print_help(stderr);
                return EXIT_FAILURE;
            }
        }
    }

    if ((action == ACTION_DELETE && arg_count != 1) ||
        (action != ACTION_DELETE && arg_count != 2)) {
        print_help(stderr);
        return EXIT_FAILURE;
    }

    glob_t matches;
    memset(&matches, 0, sizeof(matches));
    int glob_result = glob(args[0], 0, NULL, &matches);
    if (glob_result == GLOB_NOMATCH || matches.gl_pathc == 0) {
        fprintf(stderr, "No matches found for '%s'\n", args[0]);
        globfree(&matches);
        return EXIT_FAILURE;
    }
    if (glob_result != 0) {
        fprintf(stderr, "Error expanding pattern '%s'\n", args[0]);
        globfree(&matches);
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    if (action == ACTION_DELETE) {
        for (size_t i = 0; i < matches.gl_pathc; i++) {
            if (delete_item(matches.gl_pathv[i], force) != 0) {
                result = EXIT_FAILURE;
                break;
            }
        }
        globfree(&matches);
        return result;
    }

    const char *destination = args[1];
    struct stat st_dest;
    int dest_exists = (stat(destination, &st_dest) == 0);
    int dest_is_dir = (dest_exists && S_ISDIR(st_dest.st_mode));

    size_t dest_len = strlen(destination);
    if (!dest_is_dir && dest_len > 0 && destination[dest_len - 1] == '/') {
        dest_is_dir = 1;
    }
    if (matches.gl_pathc > 1) {
        dest_is_dir = 1;
        if (dest_exists && !S_ISDIR(st_dest.st_mode)) {
            fprintf(stderr, "Destination must be a directory for multiple sources.\n");
            globfree(&matches);
            return EXIT_FAILURE;
        }
    }
    if (dest_is_dir && !dest_exists) {
        if (mkdir(destination, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Error creating directory '%s': %s\n", destination, strerror(errno));
            globfree(&matches);
            return EXIT_FAILURE;
        }
    }

    for (size_t i = 0; i < matches.gl_pathc; i++) {
        const char *src = matches.gl_pathv[i];
        char dest_path[PATH_MAX];
        if (dest_is_dir) {
            snprintf(dest_path, sizeof(dest_path), "%s/%s", destination, get_basename(src));
        } else {
            strncpy(dest_path, destination, sizeof(dest_path));
            dest_path[sizeof(dest_path) - 1] = '\0';
        }

        if (action == ACTION_COPY) {
            if (copy_item(src, dest_path, force) != 0) {
                result = EXIT_FAILURE;
                break;
            }
        } else {
            int skipped = 0;
            if (move_item(src, dest_path, force, &skipped) != 0) {
                result = EXIT_FAILURE;
                break;
            }
        }
    }

    globfree(&matches);
    return result;
}
