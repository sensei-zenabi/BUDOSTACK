#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: remove <file>\n");
        return EXIT_FAILURE;
    }
    if (remove(argv[1]) != 0) {
        perror("Error removing file");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
