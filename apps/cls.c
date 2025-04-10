#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

// Main entry point of the animated clear-screen application
int main(void) {
    struct winsize w;
    
    // Try to determine the size of the terminal window.
    // If this fails, default to 24 rows and 80 columns.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
        w.ws_col = 80;
    }
    
    // The following nested loops clear the screen in an animated way.
    // The outer loop goes row by row (from top to bottom).
    // The inner loop clears the current row gradually from left to right.
    for (int row = 1; row <= w.ws_row; row++) {
        // For each column in the current row, print a space at that position.
        for (int col = 1; col <= w.ws_col; col++) {
            // Move the cursor to the specific row and column, then print a space.
            printf("\033[%d;%dH ", row, col);
            fflush(stdout); // Flush to make the change visible immediately.
            usleep(200);   // Pause for 2 milliseconds between each column.
        }
        usleep(500); // Additional delay after finishing a row.
    }
    
    // Optionally, reposition the cursor at the bottom of the screen.
    printf("\033[%d;1H", w.ws_row);
    fflush(stdout);
    
    return 0;
}
