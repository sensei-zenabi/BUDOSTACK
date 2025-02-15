#include <stdio.h>

int main() {
    printf("Available commands:\n");
    printf("  hello     : Print a greeting message.\n");
    printf("  help      : Display this help message.\n");
    printf("  list      : List contents of a directory (default is current directory).\n");
    printf("  display   : Display the contents of a file.\n");
    printf("  copy      : Copy a file from source to destination.\n");
    printf("  move      : Move (rename) a file from source to destination.\n");
    printf("  remove    : Remove a file.\n");
    printf("  update    : Create an empty file or update its modification time.\n");
    printf("  makedir   : Create a new directory.\n");
    printf("  rmdir     : Remove an empty directory.\n");
    printf("  exit      : Exit the terminal.\n");
    return 0;
}
