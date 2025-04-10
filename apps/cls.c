#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

// Main entry point of the animated clear-screen application
int main(void) {
    struct winsize w;
    
    // Try to determine the size of the terminal window.
    // If this fails, default to 24 rows and 80 columns.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        w.ws_row = 24;
        w.ws_col = 80;
    }
    
    // Hide the cursor during animation.
    printf("\033[?25l");
    fflush(stdout);
    
    // The following nested loops clear the screen in an animated way.
    // The outer loop goes row by row (from top to bottom).
    // The inner loop clears the current row gradually from left to right.
	int subtract_time = 25 * 500;
	int erasetime = 500 * 1000;
	// Pause for 200 microseconds between each column.
    struct timespec delay;
	delay.tv_sec = 0;	
    delay.tv_nsec = erasetime; // 200 microseconds = 200,000 nanoseconds

    for (int row = 1; row <= w.ws_row; row++) {
    	if (erasetime > 100) {
		   	erasetime = erasetime - subtract_time;
	    }
    	delay.tv_nsec = erasetime;
        // For each column in the current row, print a space at that position.
        for (int col = 1; col <= w.ws_col; col++) {
            // Move the cursor to the specific row and column, then print a space.
            printf("\033[%d;%dH ", row, col);
            fflush(stdout); // Flush to make the change visible immediately.            
            nanosleep(&delay, NULL);
        }
    }
    
    // Optionally, reposition the cursor at the bottom of the screen.
    printf("\033[%d;1H", w.ws_row);
    
    // Show the cursor again once the animation is complete.
    printf("\033[?25h");
    fflush(stdout);
    
    return 0;
}
