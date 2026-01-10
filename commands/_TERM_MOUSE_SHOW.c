#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_MOUSE_SHOW <enable|disable>\n");
    fprintf(stderr, "  Shows (enable) or hides (disable) the mouse cursor in the terminal.\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv || !argv[1]) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *action = argv[1];
    const char *osc_action = NULL;

    if (strcmp(action, "enable") == 0) {
        osc_action = "show";
    } else if (strcmp(action, "disable") == 0) {
        osc_action = "hide";
    } else {
        fprintf(stderr, "_TERM_MOUSE_SHOW: action must be 'enable' or 'disable'.\n");
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\x1b]777;mouse=%s\a", osc_action) < 0) {
        perror("_TERM_MOUSE_SHOW: printf");
        return EXIT_FAILURE;
    }

    if (fflush(stdout) != 0) {
        perror("_TERM_MOUSE_SHOW: fflush");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
