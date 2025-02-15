#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

int main(int argc, char *argv[]) {
    const char *path = ".";
    if (argc > 1) {
        path = argv[1];
    }
    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir failed");
        return EXIT_FAILURE;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }
    closedir(dir);
    return EXIT_SUCCESS;
}
