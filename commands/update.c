#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: update <file>\n");
        return EXIT_FAILURE;
    }
    struct stat st;
    int exists = (stat(argv[1], &st) == 0);
    if (!exists) {
        FILE *fp = fopen(argv[1], "w");
        if (!fp) {
            perror("Error creating file");
            return EXIT_FAILURE;
        }
        fclose(fp);
    } else {
        struct utimbuf new_times;
        new_times.actime = st.st_atime;
        new_times.modtime = time(NULL);
        if (utime(argv[1], &new_times) != 0) {
            perror("Error updating modification time");
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
