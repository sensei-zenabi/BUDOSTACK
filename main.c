#define _POSIX_C_SOURCE 200809L  // Enable POSIX extensions (e.g., strdup)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <termios.h>   // For terminal raw mode (POSIX)
#include <ctype.h>     // For isprint()

#define CLEAR_SCREEN "clear"
#define MKDIR(dir) mkdir(dir, 0777)
#define RMDIR(dir) rmdir(dir)
#define CHDIR(dir) chdir(dir)
#define GETCWD(buffer, size) getcwd(buffer, size)

// All the functions imported from other files (preferably only one per file!)
extern void cmd_teach_sv(char *filename);
extern void cmd_run_sv(char *filename);

// Structure for storing file information for ls sorting
typedef struct {
    char name[256];
    long size;
    char type;
} FileInfo;

// Function Declarations
void process_command(char *command);
void cmd_cd(char *dir);
void cmd_pwd();
void cmd_ls();
void cmd_mkdir(char *dir);
void cmd_rmdir(char *dir);
void cmd_rm(char *file);
void cmd_clear();
void cmd_create_sv(char *filename);
void cmd_delete_sv(char *filename);
void help();
void cmd_copy_sv(char *args);  // New function: copy a .sv model file

// Autocomplete-related function declarations
char *get_input_with_autocomplete(const char *prompt);
void autocomplete(char *buffer, int *pos);
int starts_with(const char *str, const char *prefix);
int ends_with(const char *str, const char *suffix);
int get_command_matches(const char *prefix, char matches[][256], int max_matches);
int get_directory_matches(const char *prefix, char matches[][256], int max_matches, int mode);

// Comparison function for sorting filenames alphabetically
int compare(const void *a, const void *b) {
    return strcmp(((FileInfo *)a)->name, ((FileInfo *)b)->name);
}

int main() {
    // Main loop: read commands from user with autocomplete support.
    printf("\nWelcome to Mini Terminal (Linux Only)\n");
    printf("This terminal is intended to run .sv files (AI models).\n");
    printf("Type 'help' for a full list of commands.\n\n");

    while (1) {
        // Use our custom input function that supports autocomplete.
        char *command = get_input_with_autocomplete("> ");
        if (!command) {
            continue;
        }
        // Exit command terminates the terminal.
        if (strcmp(command, "exit") == 0) {
            printf("Exiting terminal...\n");
            free(command);
            break;
        }

        process_command(command);
        free(command);
    }

    return 0;
}

/* 
 * Process the user command by checking its prefix and calling the proper function.
 * Also restricts file-related commands to only operate on .sv files.
 */
void process_command(char *command) {
    if (strncmp(command, "cd ", 3) == 0) {
        cmd_cd(command + 3);
    } else if (strcmp(command, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(command, "ls") == 0) {
        cmd_ls();
    } else if (strncmp(command, "mkdir ", 6) == 0) {
        cmd_mkdir(command + 6);
    } else if (strncmp(command, "rmdir ", 6) == 0) {
        cmd_rmdir(command + 6);
    } else if (strncmp(command, "rm ", 3) == 0) {
        cmd_rm(command + 3);
    } else if (strcmp(command, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(command, "help") == 0) {
        help();
    } else if (strncmp(command, "create ", 7) == 0) {
        cmd_create_sv(command + 7);
    } else if (strncmp(command, "delete ", 7) == 0) {
        cmd_delete_sv(command + 7);
    } else if (strncmp(command, "copy ", 5) == 0) {
        // New command: copy a .sv file (format: copy <source>.sv <destination>.sv)
        cmd_copy_sv(command + 5);
    } else if (strncmp(command, "teach ", 6) == 0) {
        // Check that the file name ends with .sv before teaching.
        char *arg = command + 6;
        if (!ends_with(arg, ".sv")) {
            printf("Error: Only .sv files can be taught.\n");
            return;
        }
        cmd_teach_sv(arg);
    } else if (strncmp(command, "run ", 4) == 0) {  
        // Check that the file name ends with .sv before running.
        char *arg = command + 4;
        if (!ends_with(arg, ".sv")) {
            printf("Error: Only .sv files can be run.\n");
            return;
        }
        cmd_run_sv(arg);
    } else {
        printf("Unknown command: %s\n", command);
    }
}

/* 
 * Change directory to the one specified.
 */
void cmd_cd(char *dir) {
    if (CHDIR(dir) == 0) {
        printf("Changed directory to: %s\n", dir);
    } else {
        perror("cd failed");
    }
}

/*
 * Print the current working directory.
 */
void cmd_pwd() {
    char cwd[1024];
    if (GETCWD(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd failed");
    }
}

/*
 * List files in the current directory.
 * For regular files with a .sv extension, mark type as 'M' (model) instead of 'F'.
 */
void cmd_ls() {
    DIR *d;
    struct dirent *dir;
    struct stat fileStat;
    FileInfo files[1024];
    int count = 0;

    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (count >= 1024) break; // Prevent overflow
            strcpy(files[count].name, dir->d_name);
            if (stat(dir->d_name, &fileStat) == 0) {
                files[count].size = fileStat.st_size;
                if (S_ISDIR(fileStat.st_mode)) {
                    files[count].type = 'D';
                } else if (!S_ISDIR(fileStat.st_mode) && ends_with(dir->d_name, ".sv")) {
                    files[count].type = 'M'; // .sv file (AI model)
                } else {
                    files[count].type = 'F';
                }
            } else {
                files[count].size = 0;
                files[count].type = '?'; // Unknown type
            }
            count++;
        }
        closedir(d);
    }

    // Sort filenames alphabetically.
    qsort(files, count, sizeof(FileInfo), compare);

    // Print header.
    printf("%-30s %-12s %-5s\n", "Filename", "Size (bytes)", "Type");
    printf("------------------------------------------------------\n");

    // Print each file's information.
    for (int i = 0; i < count; i++) {
        printf("%-30s %-12ld %c\n", files[i].name, files[i].size, files[i].type);
    }
}

/*
 * Create a new directory.
 */
void cmd_mkdir(char *dir) {
    if (MKDIR(dir) == 0) {
        printf("Directory created: %s\n", dir);
    } else {
        perror("mkdir failed");
    }
}

/*
 * Remove an empty directory.
 */
void cmd_rmdir(char *dir) {
    if (RMDIR(dir) == 0) {
        printf("Directory removed: %s\n", dir);
    } else {
        perror("rmdir failed");
    }
}

/*
 * Remove a file. Only files with a .sv extension may be removed.
 */
void cmd_rm(char *file) {
    if (!ends_with(file, ".sv")) {
        printf("Error: Only .sv files can be removed using rm.\n");
        return;
    }
    if (remove(file) == 0) {
        printf("File removed: %s\n", file);
    } else {
        perror("rm failed");
    }
}

/*
 * Create a new .sv file.
 */
void cmd_create_sv(char *filename) {
    if (!ends_with(filename, ".sv")) {
        printf("Error: Filename must have .sv extension\n");
        return;
    }

    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Failed to create script");
        return;
    }

    printf("Script created: %s\n", filename);
    fclose(file);
}

/*
 * Delete an existing .sv file.
 */
void cmd_delete_sv(char *filename) {
    if (!ends_with(filename, ".sv")) {
        printf("Error: Only .sv files can be deleted\n");
        return;
    }

    if (remove(filename) == 0) {
        printf("Script deleted: %s\n", filename);
    } else {
        perror("Failed to delete script");
    }
}

/*
 * Create a copy of an existing .sv file with a new user-defined name.
 * Usage: copy <source>.sv <destination>.sv
 */
void cmd_copy_sv(char *args) {
    char src[256], dest[256];
    // Expect exactly two arguments: source and destination.
    if (sscanf(args, "%255s %255s", src, dest) != 2) {
        printf("Usage: copy <source>.sv <destination>.sv\n");
        return;
    }
    if (!ends_with(src, ".sv") || !ends_with(dest, ".sv")) {
        printf("Error: Both source and destination must have .sv extension\n");
        return;
    }
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) {
        perror("Failed to open source file");
        return;
    }
    FILE *fdest = fopen(dest, "wb");
    if (!fdest) {
        perror("Failed to open destination file");
        fclose(fsrc);
        return;
    }
    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fsrc)) > 0) {
        fwrite(buffer, 1, bytes, fdest);
    }
    fclose(fsrc);
    fclose(fdest);
    printf("Copied %s to %s\n", src, dest);
}

/*
 * Clear the terminal screen.
 */
void cmd_clear() {
    system(CLEAR_SCREEN);
}

/*
 * Display the help menu with a list of available commands.
 */
void help() {
    printf("\nAvailable commands:\n");
    printf("  cd <dir>               - Change directory\n");
    printf("  pwd                    - Print working directory\n");
    printf("  ls                     - List files in directory\n");
    printf("  mkdir <dir>            - Create directory\n");
    printf("  rmdir <dir>            - Remove empty directory\n");
    printf("  rm <file>              - Delete .sv file\n");
    printf("  clear                  - Clear screen\n");
    printf("  help                   - Show this menu\n");
    printf("  create <model>.sv      - Create a new .sv model file\n");
    printf("  delete <model>.sv      - Delete an existing .sv model file\n");
    printf("  copy <src>.sv <dest>.sv- Copy a .sv model file\n");
    printf("  teach <model>.sv       - Start teaching a model (.sv file)\n");
    printf("  run <model>.sv         - Run an existing model (.sv file)\n");
    printf("  exit                   - Close terminal\n\n");
}

/* ------------------------- Autocomplete Functions ------------------------- */

/*
 * get_input_with_autocomplete():
 *   Reads user input from the terminal with support for autocomplete (using Tab).
 *   It puts the terminal into raw mode, processes each keypress (including backspace
 *   and tab), and returns the complete command line.
 *   (The returned string must be freed by the caller.)
 */
char* get_input_with_autocomplete(const char *prompt) {
    static char buffer[256];
    int pos = 0;
    memset(buffer, 0, sizeof(buffer));

    // Save original terminal attributes and set raw mode (disable canonical mode and echo)
    struct termios orig_termios, raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // Print prompt
    printf("%s", prompt);
    fflush(stdout);

    while (1) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) break;
        if (c == '\r' || c == '\n') {
            putchar('\n');
            break;
        } else if (c == 127 || c == '\b') { // Handle backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c == '\t') { // Tab key triggers autocomplete
            autocomplete(buffer, &pos);
            fflush(stdout);
        } else if (isprint(c)) {
            if (pos < 255) {
                buffer[pos] = c;
                pos++;
                buffer[pos] = '\0';
                putchar(c);
                fflush(stdout);
            }
        }
        // Other control characters are ignored.
    }

    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    return strdup(buffer);
}

/*
 * autocomplete():
 *   Finds the last token in the current input buffer and attempts to complete it.
 *   Depending on the context (command or file argument), it will search either the list
 *   of supported commands or the current directory for matching names.
 */
void autocomplete(char *buffer, int *pos) {
    int len = *pos;
    int token_start = 0;
    // Find the start index of the last token (after the last space)
    for (int i = len - 1; i >= 0; i--) {
        if (buffer[i] == ' ') {
            token_start = i + 1;
            break;
        }
    }
    char token[256];
    int token_len = len - token_start;
    strncpy(token, buffer + token_start, token_len);
    token[token_len] = '\0';

    // Determine if we are completing the command (first token) or a file argument.
    int is_command = (token_start == 0);
    char matches[100][256]; // Array to hold up to 100 possible completions
    int match_count = 0;

    if (is_command) {
        // Autocomplete using supported commands.
        match_count = get_command_matches(token, matches, 100);
    } else {
        // Parse the command word (the first token)
        char command_word[64];
        sscanf(buffer, "%63s", command_word);
        if (strcmp(command_word, "cd") == 0) {
            // For "cd", complete only directory names.
            match_count = get_directory_matches(token, matches, 100, 1);
        } else if (strcmp(command_word, "teach") == 0 || strcmp(command_word, "run") == 0 ||
                   strcmp(command_word, "create") == 0 || strcmp(command_word, "delete") == 0 ||
                   strcmp(command_word, "rm") == 0 || strcmp(command_word, "copy") == 0) {
            // For file-management commands, complete only .sv files.
            // For "copy", only autocomplete the first argument (source).
            int space_count = 0;
            for (int i = 0; i < len; i++) {
                if (buffer[i] == ' ') space_count++;
            }
            if (strcmp(command_word, "copy") == 0 && space_count >= 2) {
                // Do not autocomplete the destination file.
                return;
            }
            match_count = get_directory_matches(token, matches, 100, 2);
        } else {
            // Other commands: no autocomplete for arguments.
            return;
        }
    }

    if (match_count == 0) {
        // No matches found.
        return;
    }

    // Find the longest common prefix among all matches.
    char common[256];
    strcpy(common, matches[0]);
    for (int i = 1; i < match_count; i++) {
        int j = 0;
        while (common[j] && matches[i][j] && common[j] == matches[i][j])
            j++;
        common[j] = '\0';
    }

    int common_len = (int)strlen(common);
    if (common_len > token_len) {
        // Append the additional characters from the common prefix to the buffer.
        for (int i = token_len; i < common_len; i++) {
            if (*pos < 255) {
                buffer[*pos] = common[i];
                (*pos)++;
                buffer[*pos] = '\0';
                putchar(common[i]);
            }
        }
    } else {
        // If no further completion is possible, list all matching options.
        putchar('\n');
        for (int i = 0; i < match_count; i++) {
            printf("%s\t", matches[i]);
        }
        putchar('\n');
        // Reprint the prompt and the current input buffer.
        printf("> %s", buffer);
        fflush(stdout);
    }
}

/*
 * starts_with():
 *   Returns 1 if 'str' begins with 'prefix'; otherwise returns 0.
 */
int starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*prefix != *str)
            return 0;
        prefix++;
        str++;
    }
    return 1;
}

/*
 * ends_with():
 *   Returns 1 if 'str' ends with 'suffix'; otherwise returns 0.
 */
int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return 0;
    return (strcmp(str + lenstr - lensuffix, suffix) == 0);
}

/*
 * get_command_matches():
 *   Searches the list of supported commands for those starting with 'prefix'.
 *   The matching command names are stored in 'matches'.
 */
int get_command_matches(const char *prefix, char matches[][256], int max_matches) {
    char *commands[] = {"cd", "pwd", "ls", "mkdir", "rmdir", "rm", "clear", "help",
                        "create", "delete", "teach", "run", "copy", "exit"};
    int count = 0;
    int num_commands = sizeof(commands) / sizeof(commands[0]);
    for (int i = 0; i < num_commands; i++) {
        if (starts_with(commands[i], prefix)) {
            strncpy(matches[count], commands[i], 256);
            count++;
            if (count >= max_matches)
                break;
        }
    }
    return count;
}

/*
 * get_directory_matches():
 *   Reads the current directory and stores names that start with 'prefix' into 'matches'.
 *   The mode parameter determines the type of entries to match:
 *     mode == 1: Only directories.
 *     mode == 2: Only regular files ending with ".sv".
 */
int get_directory_matches(const char *prefix, char matches[][256], int max_matches, int mode) {
    DIR *d = opendir(".");
    if (!d)
        return 0;
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(d)) != NULL) {
        if (starts_with(entry->d_name, prefix)) {
            struct stat st;
            if (stat(entry->d_name, &st) == 0) {
                if (mode == 1 && S_ISDIR(st.st_mode)) {
                    strncpy(matches[count], entry->d_name, 256);
                    count++;
                    if (count >= max_matches)
                        break;
                } else if (mode == 2 && !S_ISDIR(st.st_mode) && ends_with(entry->d_name, ".sv")) {
                    strncpy(matches[count], entry->d_name, 256);
                    count++;
                    if (count >= max_matches)
                        break;
                }
            }
        }
    }
    closedir(d);
    return count;
}
