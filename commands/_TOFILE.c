#define _POSIX_C_SOURCE 200112L

#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Use `_TOFILE -file <path> --start` to begin logging terminal output within BUDOSTACK.\n");
    printf("Run `_TOFILE --stop` to finish and close the log file.\n");
    printf("Logging is coordinated by the main shell; this helper documents the expected flags.\n");
    return 0;
}
