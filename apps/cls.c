/* cls.c - Sweeping terminal clear animation
 *
 * This program uses ANSI escape sequences and POSIX functions
 * to animate the clearing of the terminal contents from top down.
 *
 * It first obtains the terminal dimensions using ioctl() on STDOUT,
 * then sequentially moves the cursor to each line and clears that line.
 * A delay between each line produces the sweeping clearing effect.
 *
 * Note!: There is no portable way to “capture” the current terminal content,
 * so this program operates directly on what is currently displayed.
 */

#define _POSIX_C_SOURCE 200112L  // Enable POSIX.1-2001 features

#include "../lib/termbg.h"

#include <stdio.h>
#include <unistd.h>     // for STDOUT_FILENO
#include <stdlib.h>
#include <sys/ioctl.h>  // for ioctl() and winsize
#include <time.h>       // for nanosleep()

int main(void)
{
    struct winsize w;

    /* Hide the cursor during the animation */
    printf("\033[?25l");
    fflush(stdout);

    /* Try to get the size of the terminal.
       If ioctl() fails, default to 24 rows x 80 columns. */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) < 0) {
        w.ws_row = 24;
        w.ws_col = 80;
    }

    /* Optionally, a short delay can be inserted before starting the animation,
       for example to let the user see the current terminal content.
       Uncomment the block below if desired.
       
    {
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 500000000; // 500 milliseconds delay
        nanosleep(&delay, NULL);
    }
    */
    
    /* Sweeping clear: For each row from the top to the bottom,
       move the cursor there and clear the line. */
    for (unsigned short row = 1; row <= w.ws_row; row++) {
        /* Move cursor to the beginning of the current row.
           ANSI escape sequence: ESC [ <row> ; 1 H */
        printf("\033[%hu;1H", row);
        /* Clear the entire line: ESC [ 2K */
        printf("\033[2K");
        fflush(stdout);
        /* Delay between clearing lines for the sweeping effect.
           25,000 microseconds equals 25 milliseconds per line. */
        {
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = 25000 * 1000;  // 25,000 microseconds = 25ms
            nanosleep(&req, NULL);
        }
    }

    /* Clear the scrollback buffer so previous history is removed as well. */
    printf("\033[3J");

    /* Restore the cursor visibility */
    printf("\033[?25h");

    /* Move the cursor to home position */
    printf("\033[H");
    fflush(stdout);

    termbg_clear();

    return 0;
}
