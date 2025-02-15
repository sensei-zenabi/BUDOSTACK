#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: move <source> <destination>\n");
        return EXIT_FAILURE;
    }
    if (rename(argv[1], argv[2]) != 0) {
        perror("Error moving file");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
