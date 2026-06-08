#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("_TERM_ACTIVE\n");
    printf("Prints 1 when running inside ./apps/terminal, otherwise 0.\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage();
        return EXIT_SUCCESS;
    }

    const char *state = getenv("BUDOSTACK_TERM_ACTIVE");
    if (state && strcmp(state, "TRUE") == 0) {
        printf("1\n");
    } else {
        printf("0\n");
    }

    return EXIT_SUCCESS;
}
