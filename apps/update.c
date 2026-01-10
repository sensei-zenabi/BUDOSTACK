#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct StringList {
    char **items;
    size_t count;
};

static void free_string_list(struct StringList *list) {
    if (list == NULL || list->items == NULL) {
        return;
    }

    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static int append_string(struct StringList *list, const char *value) {
    if (list == NULL || value == NULL) {
        return -1;
    }

    char **new_items = realloc(list->items, (list->count + 1) * sizeof(*new_items));
    if (new_items == NULL) {
        perror("realloc");
        return -1;
    }
    list->items = new_items;

    size_t length = strlen(value);
    list->items[list->count] = malloc(length + 1);
    if (list->items[list->count] == NULL) {
        perror("malloc");
        return -1;
    }

    memcpy(list->items[list->count], value, length + 1);
    list->count += 1;
    return 0;
}

static int run_system_command(const char *command, const char *friendly_name) {
    if (command == NULL || friendly_name == NULL) {
        fprintf(stderr, "Internal error: missing command description.\n");
        return -1;
    }

    printf("\n%s\n", friendly_name);
    fflush(stdout);

    int status = system(command);
    if (status == -1) {
        perror("system");
        fprintf(stderr, "Unable to run '%s'.\n", command);
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        fprintf(stderr, "The step '%s' did not finish successfully (code %d).\n", friendly_name, code);
        return -1;
    }

    printf("Done.\n");
    return 0;
}

static int git_available(void) {
    int status = system("git --version > /dev/null 2>&1");
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Git is required but not available. Please install Git and try again.\n");
        return -1;
    }
    return 0;
}

static int detect_repository_root(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return -1;
    }

    FILE *pipe = popen("git rev-parse --show-toplevel 2>/dev/null", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    if (fgets(buffer, (int)size, pipe) == NULL) {
        fprintf(stderr, "This tool must be run inside the BUDOSTACK repository.\n");
        pclose(pipe);
        return -1;
    }
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    int exit_status = pclose(pipe);
    if (exit_status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(exit_status) || WEXITSTATUS(exit_status) != 0) {
        fprintf(stderr, "Failed to determine the repository root.\n");
        return -1;
    }

    if (chdir(buffer) != 0) {
        perror("chdir");
        fprintf(stderr, "Unable to switch to repository root '%s'.\n", buffer);
        return -1;
    }
    return 0;
}

static bool ask_yes_no(const char *question) {
    char answer[16];

    while (true) {
        printf("%s [y/n]: ", question);
        fflush(stdout);
        if (fgets(answer, sizeof(answer), stdin) == NULL) {
            return false;
        }
        size_t len = strlen(answer);
        if (len > 0 && answer[len - 1] == '\n') {
            answer[len - 1] = '\0';
            len -= 1;
        }
        if (len == 0) {
            continue;
        }
        char c = (char)tolower((unsigned char)answer[0]);
        if (c == 'y') {
            return true;
        }
        if (c == 'n') {
            return false;
        }
        printf("Please answer with 'y' or 'n'.\n");
    }
}

static int read_first_line(const char *command, char *buffer, size_t size) {
    if (command == NULL || buffer == NULL || size == 0) {
        return -1;
    }

    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    if (fgets(buffer, (int)size, pipe) == NULL) {
        buffer[0] = '\0';
    } else {
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[len - 1] = '\0';
            len -= 1;
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }

    return 0;
}

static int load_tags(struct StringList *tags) {
    if (tags == NULL) {
        return -1;
    }

    FILE *pipe = popen("git tag --sort=version:refname", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len -= 1;
        }
        if (len == 0) {
            continue;
        }
        if (append_string(tags, line) != 0) {
            pclose(pipe);
            return -1;
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Unable to read release tags from the remote repository.\n");
        return -1;
    }

    return 0;
}

static int load_tracked_files(struct StringList *files) {
    if (files == NULL) {
        return -1;
    }

    FILE *pipe = popen("git ls-files -z", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    char path[PATH_MAX];
    size_t index = 0;
    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (ch == '\0') {
            if (index > 0) {
                path[index] = '\0';
                if (append_string(files, path) != 0) {
                    pclose(pipe);
                    return -1;
                }
                index = 0;
            }
            continue;
        }
        if (index + 1 >= sizeof(path)) {
            fprintf(stderr, "Tracked file path is too long.\n");
            pclose(pipe);
            return -1;
        }
        path[index++] = (char)ch;
    }

    if (index > 0) {
        path[index] = '\0';
        if (append_string(files, path) != 0) {
            pclose(pipe);
            return -1;
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Unable to read the list of tracked files.\n");
        return -1;
    }

    return 0;
}

static int ensure_parent_dirs(const char *path) {
    char buffer[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(buffer)) {
        fprintf(stderr, "Path is too long.\n");
        return -1;
    }

    memcpy(buffer, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (buffer[i] == '/') {
            buffer[i] = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
            buffer[i] = '/';
        }
    }
    return 0;
}

static int copy_file(const char *source, const char *destination) {
    FILE *src = fopen(source, "rb");
    if (src == NULL) {
        perror("fopen");
        fprintf(stderr, "Unable to read '%s'.\n", source);
        return -1;
    }

    if (ensure_parent_dirs(destination) != 0) {
        fclose(src);
        return -1;
    }

    FILE *dst = fopen(destination, "wb");
    if (dst == NULL) {
        perror("fopen");
        fprintf(stderr, "Unable to write '%s'.\n", destination);
        fclose(src);
        return -1;
    }

    char buffer[8192];
    size_t read_size;
    while ((read_size = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, read_size, dst) != read_size) {
            perror("fwrite");
            fclose(src);
            fclose(dst);
            return -1;
        }
    }
    if (ferror(src)) {
        perror("fread");
        fclose(src);
        fclose(dst);
        return -1;
    }

    struct stat info;
    if (stat(source, &info) == 0) {
        if (chmod(destination, info.st_mode) != 0) {
            perror("chmod");
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

static int backup_tracked_files(const struct StringList *files, const char *backup_dir) {
    if (files == NULL || backup_dir == NULL) {
        return -1;
    }

    for (size_t i = 0; i < files->count; ++i) {
        char destination[PATH_MAX];
        int written = snprintf(destination, sizeof(destination), "%s/%s", backup_dir, files->items[i]);
        if (written < 0 || written >= (int)sizeof(destination)) {
            fprintf(stderr, "Backup path for '%s' is too long.\n", files->items[i]);
            return -1;
        }
        if (copy_file(files->items[i], destination) != 0) {
            return -1;
        }
    }

    return 0;
}

static int restore_tracked_files(const struct StringList *files, const char *backup_dir) {
    if (files == NULL || backup_dir == NULL) {
        return -1;
    }

    for (size_t i = 0; i < files->count; ++i) {
        char source[PATH_MAX];
        int written = snprintf(source, sizeof(source), "%s/%s", backup_dir, files->items[i]);
        if (written < 0 || written >= (int)sizeof(source)) {
            fprintf(stderr, "Restore path for '%s' is too long.\n", files->items[i]);
            return -1;
        }
        if (copy_file(source, files->items[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

static void print_intro(void) {
    printf("==============================================\n");
    printf(" Welcome to the BUDOSTACK Update Assistant\n");
    printf("==============================================\n\n");
    printf("This helper checks for new official releases and\n");
    printf("guides you through applying updates safely.\n\n");
}

int main(void) {
    print_intro();

    if (git_available() != 0) {
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (detect_repository_root(repo_root, sizeof(repo_root)) != 0) {
        return EXIT_FAILURE;
    }

    printf("Using repository at: %s\n", repo_root);

    if (run_system_command("git fetch --tags --prune origin 2>&1", "Checking GitHub for available releases...") != 0) {
        return EXIT_FAILURE;
    }

    struct StringList tags = {0};
    if (load_tags(&tags) != 0) {
        free_string_list(&tags);
        return EXIT_FAILURE;
    }

    if (tags.count == 0U) {
        printf("\nNo release tags were found on the remote repository.\n");
        free_string_list(&tags);
        return EXIT_SUCCESS;
    }

    const char *latest_tag = tags.items[tags.count - 1];

    char current_tag[256];
    bool have_current_tag = true;
    if (read_first_line("git describe --tags --abbrev=0 2>/dev/null", current_tag, sizeof(current_tag)) != 0 ||
        current_tag[0] == '\0') {
        have_current_tag = false;
        snprintf(current_tag, sizeof(current_tag), "unknown");
    }

    printf("\nCurrent version: %s\n", current_tag);
    printf("Latest release: %s\n", latest_tag);

    if (have_current_tag && strcmp(current_tag, latest_tag) == 0) {
        printf("\nYour BUDOSTACK is already up to date.\n");
        free_string_list(&tags);
        return EXIT_SUCCESS;
    }

    if (!ask_yes_no("\nA newer release is available. Do you want to update now")) {
        printf("Update cancelled. Your files were left untouched.\n");
        free_string_list(&tags);
        return EXIT_SUCCESS;
    }

    printf("\nPreparing a safety backup of tracked files...\n");
    struct StringList tracked_files = {0};
    if (load_tracked_files(&tracked_files) != 0) {
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    char backup_dir[PATH_MAX] = "/tmp/budostack_backup_XXXXXX";
    if (mkdtemp(backup_dir) == NULL) {
        perror("mkdtemp");
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    printf("Backing up %zu tracked files...\n", tracked_files.count);
    if (backup_tracked_files(&tracked_files, backup_dir) != 0) {
        fprintf(stderr, "Backup failed. Aborting the update.\n");
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    char previous_head[256];
    if (read_first_line("git rev-parse HEAD 2>/dev/null", previous_head, sizeof(previous_head)) != 0 ||
        previous_head[0] == '\0') {
        fprintf(stderr, "Unable to read the current commit hash.\n");
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    char checkout_command[PATH_MAX];
    int written = snprintf(checkout_command, sizeof(checkout_command), "git checkout -f %s 2>&1", latest_tag);
    if (written < 0 || written >= (int)sizeof(checkout_command)) {
        fprintf(stderr, "Release tag '%s' is too long.\n", latest_tag);
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    printf("\nUpdating to %s...\n", latest_tag);
    if (run_system_command(checkout_command, "Applying the new release...") != 0) {
        fprintf(stderr, "Update failed. Restoring your previous version...\n");
        char rollback_command[PATH_MAX];
        int rollback_written = snprintf(rollback_command, sizeof(rollback_command), "git checkout -f %s 2>&1", previous_head);
        if (rollback_written > 0 && rollback_written < (int)sizeof(rollback_command)) {
            run_system_command(rollback_command, "Rolling back Git state...");
        }
        if (restore_tracked_files(&tracked_files, backup_dir) != 0) {
            fprintf(stderr, "Rollback failed. Please restore from %s manually.\n", backup_dir);
        } else {
            printf("Rollback completed. Your previous files have been restored.\n");
        }
        free_string_list(&tags);
        free_string_list(&tracked_files);
        return EXIT_FAILURE;
    }

    char cleanup_command[PATH_MAX];
    int cleanup_written = snprintf(cleanup_command, sizeof(cleanup_command), "rm -rf '%s'", backup_dir);
    if (cleanup_written > 0 && cleanup_written < (int)sizeof(cleanup_command)) {
        run_system_command(cleanup_command, "Removing temporary backup...");
    }

    printf("\nUpdate complete! You are now on release %s.\n", latest_tag);
    printf("Untracked files were left untouched.\n");

    free_string_list(&tags);
    free_string_list(&tracked_files);
    return EXIT_SUCCESS;
}
