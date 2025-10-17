#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
    struct winsize ws;
    int fds[] = {STDOUT_FILENO, STDIN_FILENO, STDERR_FILENO};
    size_t count = sizeof(fds) / sizeof(fds[0]);
    size_t i;
    int found = 0;

    for (i = 0; i < count; ++i) {
        if (ioctl(fds[i], TIOCGWINSZ, &ws) == 0) {
            found = 1;
            break;
        }

        if (errno != ENOTTY && errno != EBADF) {
            perror("_GETWIDTH: ioctl");
            return EXIT_FAILURE;
        }
    }

    if (!found) {
        fprintf(stderr, "_GETWIDTH: unable to determine terminal size\n");
        return EXIT_FAILURE;
    }

    if (ws.ws_col == 0) {
        fprintf(stderr, "_GETWIDTH: reported width is zero\n");
        return EXIT_FAILURE;
    }

    if (printf("%u\n", (unsigned int)ws.ws_col) < 0) {
        perror("_GETWIDTH: printf");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
