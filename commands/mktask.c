// mktask.c
// This command creates an empty task file named "<taskname>.task".
// Design: Keep it simple with standard I/O. No extra libraries are needed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check that exactly one argument is provided (the task name)
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <taskname>\n", argv[0]);
        return 1;
    }

    // Build the file name by appending ".task" to the provided task name
    char filename[256];
    snprintf(filename, sizeof(filename), "%s.task", argv[1]);

    // Open the file in write mode; "w" creates an empty file (or truncates an existing one)
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Error creating task file");
        return 1;
    }
    fclose(fp);

    printf("Task file '%s' created successfully.\n", filename);
    return 0;
}
