#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr, "Usage: _TERM_SCALE <118x66|354x198|1|3>\n");
}

int main(int argc, char *argv[]) {
    const char *terminal_env = getenv("BUDOSTACK_TERMINAL");
    if (!terminal_env || strcmp(terminal_env, "1") != 0) {
        fprintf(stderr, "_TERM_SCALE: this command must be run inside the BUDOSTACK terminal.\n");
        return EXIT_FAILURE;
    }

    if (argc != 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *arg = argv[1];
    const char *target = NULL;

    if (strcmp(arg, "118x66") == 0 || strcmp(arg, "1") == 0 || strcmp(arg, "default") == 0 || strcmp(arg, "small") == 0) {
        target = "118x66";
    } else if (strcmp(arg, "354x198") == 0 || strcmp(arg, "3") == 0 || strcmp(arg, "large") == 0 || strcmp(arg, "triple") == 0) {
        target = "354x198";
    } else {
        print_usage();
        return EXIT_FAILURE;
    }

    if (printf("\033]777;term-scale=%s\007", target) < 0) {
        fprintf(stderr, "_TERM_SCALE: failed to emit control sequence.\n");
        return EXIT_FAILURE;
    }
    fflush(stdout);
    fprintf(stderr, "_TERM_SCALE: requested %s terminal view.\n", target);
    return EXIT_SUCCESS;
}
