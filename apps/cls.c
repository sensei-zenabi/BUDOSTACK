/* cls.c */
/* Compile with: gcc -std=c11 -Wall -Wextra -pedantic -o cls cls.c */
/* Clears the terminal row by row like an old TV CRT sweep. */

/* Define _POSIX_C_SOURCE to enable POSIX declarations such as nanosleep */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

/* Clears a single row at the specified position */
void clear_row(int row) {
    /* Move the cursor to the beginning of the row */
    printf("\033[%d;1H", row);
    /* Clear the entire line */
    printf("\033[2K");
    fflush(stdout);
}

int main(void) {
    struct winsize w;

    /* Get terminal size */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        perror("ioctl");
        return 1;
    }

    int rows = w.ws_row;
    /* Define a 20-millisecond delay */
    struct timespec req = { .tv_sec = 0, .tv_nsec = 20000000 };

    /* Hide the cursor during the sweep */
    printf("\033[?25l");

    /* Perform CRT-like sweep from top to bottom */
    for (int i = 1; i <= rows; i++) {
        clear_row(i);
        nanosleep(&req, NULL);
    }

    /* Move cursor to home and show it again */
    printf("\033[H");
    printf("\033[?25h");

    return 0;
}
