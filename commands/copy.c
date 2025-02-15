#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: copy <source> <destination>\n");
        return EXIT_FAILURE;
    }
    FILE *src = fopen(argv[1], "rb");
    if (!src) {
        perror("Error opening source file");
        return EXIT_FAILURE;
    }
    FILE *dest = fopen(argv[2], "wb");
    if (!dest) {
        perror("Error opening destination file");
        fclose(src);
        return EXIT_FAILURE;
    }
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, src)) > 0) {
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            perror("Error writing to destination file");
            fclose(src);
            fclose(dest);
            return EXIT_FAILURE;
        }
    }
    fclose(src);
    fclose(dest);
    return EXIT_SUCCESS;
}
