#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: rmdir <directory>\n");
        return EXIT_FAILURE;
    }
    if (rmdir(argv[1]) != 0) {
        perror("Error removing directory");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
