#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define TARGET_COLS 118
#define TARGET_ROWS 66

int main() {
    struct winsize w;

    // Get terminal size
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd == -1) {
        perror("open /dev/tty");
        return 1;
    }

    if (ioctl(tty_fd, TIOCGWINSZ, &w) == -1) {
        perror("ioctl");
        close(tty_fd);
        return 1;
    }
    close(tty_fd);

    int term_cols = w.ws_col;
    int term_rows = w.ws_row;

    // Clear the screen
    printf("\033[2J\033[H");

    // Print 66 rows of 117 dots
    for (int r = 0; r < TARGET_ROWS; r++) {
        for (int c = 0; c < TARGET_COLS - 1; c++) { // 117 columns
            printf(".");
        }
        printf("\n");
    }

    // Print terminal size info on the next line (row 67)
    printf("Target terminal size: %dx%d, current size: %dx%d\n",
           TARGET_COLS, TARGET_ROWS, term_cols, term_rows);

    return 0;
}

