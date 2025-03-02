/*
 * libconsole.c
 *
 * This file contains the implementation of the prettyprint function,
 * which prints a message to stdout one character at a time, with a
 * configurable delay (in milliseconds) between each character.
 *
 * Design Principles:
 * - Use only standard, cross-platform C libraries (stdio.h, time.h).
 * - Use -std=c11 and plain C.
 * - The function delays are implemented using clock() for portability.
 *
 * Note: The delay mechanism here uses a busy wait loop with clock()
 *       which may not be very efficient, but it works with standard C.
 */

#include <stdio.h>
#include <time.h>

// prettyprint prints the provided message one character at a time.
// delay_ms specifies the delay between printing each character in milliseconds.
void prettyprint(const char *message, unsigned int delay_ms) {
    // Calculate delay in clock ticks. CLOCKS_PER_SEC gives the number of clock ticks per second.
    clock_t delay_ticks = (delay_ms * CLOCKS_PER_SEC) / 1000;
    
    // Iterate through each character in the message.
    for (const char *p = message; *p != '\0'; p++) {
        // Print the character.
        putchar(*p);
        fflush(stdout);  // Ensure the character is output immediately.

        // Start a busy wait loop to create the delay.
        clock_t start_time = clock();
        while ((clock() - start_time) < delay_ticks)
            ;  // Busy-wait loop.
    }
    // Print a newline after the message.
    putchar('\n');
}
