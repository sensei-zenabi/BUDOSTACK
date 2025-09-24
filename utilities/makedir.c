#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: makedir <directory>\n");
        return EXIT_FAILURE;
    }
    if (mkdir(argv[1], 0755) != 0) {
        perror("Error creating directory");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
