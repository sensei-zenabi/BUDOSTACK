#include <stdio.h>

int main(void) {
    // ANSI escape sequence:
    // "\033[H" moves the cursor to the top-left corner.
    // "\033[J" clears the screen from the cursor down.
    // Together they clear the entire screen.
    printf("\033[H\033[J");
    
    // Flush the output to ensure the escape sequence is sent to the terminal immediately.
    fflush(stdout);
    
    return 0;
}
