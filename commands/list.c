#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* Convert file mode to a permission string (like ls -l) */
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

int main(int argc, char *argv[]) {
    const char *dir_path = (argc < 2) ? "." : argv[1];
    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, NULL, alphasort);
    if (n < 0) {
        perror("scandir");
        return EXIT_FAILURE;
    }

    /* Print header */
    printf("%-30s %-11s %-10s %-20s\n", "Filename", "Permissions", "Size", "Last Modified");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        struct dirent *entry = namelist[i];
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == -1) {
            perror("stat");
            free(namelist[i]);
            continue;
        }

        char perms[11];
        mode_to_string(st.st_mode, perms);

        /* Format modification time */
        char timebuf[20];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);

        printf("%-30s %-11s %-10ld %-20s\n",
               entry->d_name, perms, (long)st.st_size, timebuf);

        free(namelist[i]);
    }
    free(namelist);
    return EXIT_SUCCESS;
}
