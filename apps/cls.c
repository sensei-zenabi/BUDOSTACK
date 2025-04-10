/* cls_sweep_redesign.c */
/* Compile with: gcc -std=c11 -Wall -Wextra -pedantic -o cls_sweep_redesign cls_sweep_redesign.c */
/* This version performs a CRT-like sweep by restricting the scroll region 
   to all but the bottom row, then clearing rows one-by-one with a delay.
   Finally, it restores the scroll region and clears the bottom row without triggering autoscroll. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

int main(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        perror("ioctl");
        return 1;
    }
    int rows = w.ws_row;
    struct timespec req = { .tv_sec = 0, .tv_nsec = 20000000 }; // 20 ms delay

    /* Disable auto‑wrap (to help avoid bottom-row issues) and hide the cursor for a cleaner effect */
    printf("\033[?7l\033[?25l");
    fflush(stdout);

    /* Set the scrolling region to exclude the bottom row.
       This confines any scroll activity to rows 1 through rows-1. */
    printf("\033[1;%dr", rows - 1);
    fflush(stdout);

    /* Animate the sweep on rows 1 to rows-1 */
    for (int i = 1; i < rows; i++) {
        // Move the cursor to row i, column 1 and clear that line.
        printf("\033[%d;1H\033[2K", i);
        fflush(stdout);
        nanosleep(&req, NULL);
    }

    /* Restore the full scrolling region */
    printf("\033[r");
    fflush(stdout);

    /* Clear the bottom row separately.
       Since it is not in a scrollable region now, clearing it shouldn’t trigger autoscroll. */
    printf("\033[%d;1H\033[2K", rows);
    fflush(stdout);

    /* Re-enable auto‑wrap and show the cursor */
    printf("\033[?7h\033[?25h");
    fflush(stdout);

    /* Optionally, move the cursor to the home position */
    printf("\033[H");
    fflush(stdout);

    return 0;
}
