#include <stdio.h>
#include <time.h>		// For time delay function

// delay function using busy-wait based on clock()
void delay(double seconds) {
    // Capture the starting clock time
    clock_t start_time = clock();
    // Loop until the elapsed time in seconds is at least the desired delay
    while ((double)(clock() - start_time) / CLOCKS_PER_SEC < seconds) {
        // Busy waiting: doing nothing while waiting for the time to pass.
    }
}

// delayPrint() prints the provided string one character at a time,
// waiting for delayTime seconds between each character.
void delayPrint(const char *str, double delayTime) {
    // Iterate through each character of the string until the null terminator.
    for (int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);         // Print the current character.
        fflush(stdout);          // Flush the output buffer to display the character immediately.
        delay(delayTime);        // Wait for delayTime seconds before printing the next character.
    }
    //putchar('\n');  // Optionally, print a newline at the end.
}

int main() {

	delayPrint("STARTING TASK...", 0.3);
    
    return 0;
}
