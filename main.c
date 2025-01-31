#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define CLEAR_SCREEN "cls"
    #define MKDIR(dir) _mkdir(dir)
    #define RMDIR(dir) _rmdir(dir)
    #define CHDIR(dir) _chdir(dir)
    #define GETCWD(buffer, size) _getcwd(buffer, size)
#else
    #include <sys/types.h>
    #define CLEAR_SCREEN "clear"
    #define MKDIR(dir) mkdir(dir, 0777)
    #define RMDIR(dir) rmdir(dir)
    #define CHDIR(dir) chdir(dir)
    #define GETCWD(buffer, size) getcwd(buffer, size)
#endif

// Define file structure for sorting in ls
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
void cmd_touch(char *file);
void cmd_clear();
void help();

// Comparison function for sorting filenames alphabetically
int compare(const void *a, const void *b) {
    return strcmp(((FileInfo *)a)->name, ((FileInfo *)b)->name);
}

int main() {
    char command[256];

    printf("\nWelcome to Mini Terminal (Cross-Platform)\n");
    printf("This terminal is intended to run .sv files\n");
    printf("Type 'help' for a full list of commands.\n\n");

    while (1) {
        printf("> ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;  // Remove newline

        if (strcmp(command, "exit") == 0) {
            printf("Exiting terminal...\n");
            break;
        }

        process_command(command);
    }

    return 0;
}

// Process user command
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
    } else if (strncmp(command, "touch ", 6) == 0) {
        cmd_touch(command + 6);
    } else if (strcmp(command, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(command, "help") == 0) {
        help();
    } else {
        printf("Unknown command: %s\n", command);
    }
}

// Change directory
void cmd_cd(char *dir) {
    if (CHDIR(dir) == 0) {
        printf("Changed directory to: %s\n", dir);
    } else {
        perror("cd failed");
    }
}

// Print working directory
void cmd_pwd() {
    char cwd[1024];
    if (GETCWD(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd failed");
    }
}

// List directory contents with sorting, file size, and type
void cmd_ls() {
#ifdef _WIN32
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile("*", &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("No files found.\n");
        return;
    }

    FileInfo files[1024];
    int count = 0;

    do {
        if (count >= 1024) break; // Prevent overflow
        strcpy(files[count].name, findFileData.cFileName);
        files[count].size = findFileData.nFileSizeLow;
        files[count].type = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'D' : 'F';
        count++;
    } while (FindNextFile(hFind, &findFileData));

    FindClose(hFind);

    // Sort files alphabetically
    qsort(files, count, sizeof(FileInfo), compare);

    // Print column headers
    printf("%-30s %-12s %-5s\n", "Filename", "Size (bytes)", "Type");
    printf("------------------------------------------------------\n");

    // Print files
    for (int i = 0; i < count; i++) {
        printf("%-30s %-12lu %c\n", files[i].name, files[i].size, files[i].type);
    }

#else
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
                files[count].type = S_ISDIR(fileStat.st_mode) ? 'D' : 'F';
            } else {
                files[count].size = 0;
                files[count].type = '?'; // Unknown type
            }
            count++;
        }
        closedir(d);
    }

    // Sort filenames alphabetically
    qsort(files, count, sizeof(FileInfo), compare);

    // Print column headers
    printf("%-30s %-12s %-5s\n", "Filename", "Size (bytes)", "Type");
    printf("------------------------------------------------------\n");

    // Print sorted files
    for (int i = 0; i < count; i++) {
        printf("%-30s %-12ld %c\n", files[i].name, files[i].size, files[i].type);
    }
#endif
}

// Create directory
void cmd_mkdir(char *dir) {
    if (MKDIR(dir) == 0) {
        printf("Directory created: %s\n", dir);
    } else {
        perror("mkdir failed");
    }
}

// Remove directory
void cmd_rmdir(char *dir) {
    if (RMDIR(dir) == 0) {
        printf("Directory removed: %s\n", dir);
    } else {
        perror("rmdir failed");
    }
}

// Remove file
void cmd_rm(char *file) {
    if (remove(file) == 0) {
        printf("File removed: %s\n", file);
    } else {
        perror("rm failed");
    }
}

// Create empty file (touch)
void cmd_touch(char *file) {
    FILE *fp = fopen(file, "w");
    if (fp) {
        fclose(fp);
        printf("File created: %s\n", file);
    } else {
        perror("touch failed");
    }
}

// Clear terminal screen
void cmd_clear() {
    system(CLEAR_SCREEN);
}

// Display help menu
void help() {
    printf("Available commands:\n");
    printf("  cd <dir>   - Change directory\n");
    printf("  pwd        - Print working directory\n");
    printf("  ls         - List files in directory\n");
    printf("  mkdir <dir>- Create directory\n");
    printf("  rmdir <dir>- Remove empty directory\n");
    printf("  rm <file>  - Delete file\n");
    printf("  touch <file> - Create empty file\n");
    printf("  clear      - Clear screen\n");
    printf("  help       - Show this menu\n");
    printf("  exit       - Close terminal\n");
}
