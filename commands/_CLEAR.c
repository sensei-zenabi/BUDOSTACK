#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    static const char clear_sequence[] = "\x1b[2J\x1b[H";
    size_t offset = 0u;
    const size_t total = sizeof(clear_sequence) - 1u;

    while (offset < total) {
        ssize_t written = write(STDOUT_FILENO, clear_sequence + offset, total - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_CLEAR: write");
            return EXIT_FAILURE;
        }
        offset += (size_t)written;
    }

    return EXIT_SUCCESS;
}
