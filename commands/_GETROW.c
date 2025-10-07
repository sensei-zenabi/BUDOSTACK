#define _POSIX_C_SOURCE 200809L

#include "../lib/cursorpos.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void report_error(const char *program) {
    if (errno == ETIMEDOUT) {
        fprintf(stderr, "%s: timed out while waiting for cursor position\n", program);
    } else if (errno == EILSEQ) {
        fprintf(stderr, "%s: unexpected cursor position response\n", program);
    } else {
        perror(program);
    }
}

int main(void) {
    int row = 0;
    int col = 0;

    if (cursorpos_query(&row, &col) != 0) {
        report_error("_GETROW");
        return EXIT_FAILURE;
    }

    printf("%d\n", row);
    return EXIT_SUCCESS;
}
