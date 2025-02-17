#include <stdio.h>
#include <time.h>

/*
 * This simple application demonstrates basic control flow and time handling in C.
 * Design principles used:
 * 1. **Portability:** Only standard C libraries are used (<stdio.h> and <time.h>),
 *    ensuring cross-platform compatibility with the -std=c11 flag.
 * 2. **Simplicity:** The program uses a straightforward for-loop to count from 0 to 10.
 * 3. **Deterministic Timing:** A busy-wait loop is used to delay for 1 second between prints.
 *    (Note: While busy-waiting is not CPU efficient, it is used here for simplicity
 *     and adherence to the "standard cross-platform libraries only" requirement.)
 */

int main(void) {
    for (int i = 0; i <= 10; i++) {
        printf("%d\n", i);
        fflush(stdout);  // Ensure immediate output
        
        // Record the current time and busy-wait until one second has passed
        time_t start = time(NULL);
        while (time(NULL) - start < 1) {
            ;  // Busy-wait loop for roughly 1 second
        }
    }
    
    printf("GO!\n");
    return 0;
}
