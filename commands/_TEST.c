#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define TARGET_COLS 79
#define TARGET_ROWS 44
#define TEXT_BAR_ROWS 2

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

    int term_cols = w.ws_col-1;
    int term_rows = w.ws_row-1;

    // Clear the screen
    printf("\033[2J\033[H");

    const int grid_rows = TARGET_ROWS - TEXT_BAR_ROWS;
    const int content_rows = grid_rows > 0 ? grid_rows - 1 : 0;

    for (int r = 0; r < content_rows; r++) {
        char row_digit = (char)('0' + (r % 10));
        putchar(row_digit);
        for (int c = 1; c < TARGET_COLS; c++) {
            putchar('.');
        }
        putchar('\n');
    }

    if (grid_rows > 0) {
        char bottom_row_digit = (char)('0' + ((grid_rows - 1) % 10));
        putchar(bottom_row_digit);
        for (int c = 1; c < TARGET_COLS; c++) {
            char col_digit = (char)('0' + ((c) % 10));
            putchar(col_digit);
        }
        putchar('\n');
    }

    // Print terminal size info on the next line (row 67)
    printf("Target terminal size: %dx%d, current size: %dx%d\n",
           TARGET_COLS, TARGET_ROWS, term_cols, term_rows);

    return 0;
}
