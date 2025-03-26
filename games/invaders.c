#define _POSIX_C_SOURCE 200809L  // Must be the very first line to expose POSIX APIs

/*
 * Space Invaders Clone for Linux Terminal
 *
 * Design Notes:
 * - The game board is a 40x20 grid drawn with borders using ANSI escape codes.
 * - The player's ship is represented by 'A' at the bottom row.
 * - A single bullet (represented by '|') is allowed at a time.
 * - Invaders (represented by 'W') are arranged in a grid (3 rows x 8 columns)
 *   with a fixed horizontal spacing. They move as a group.
 * - Invader group movement: Every 5 frames, the group moves one step horizontally.
 *   If any invader would hit the board edge, the group drops one row and reverses direction.
 * - Input is handled in raw mode with non-blocking reads (using termios and select).
 * - The game updates at ~10fps (100ms per frame).
 * - A global score increases by 10 for each invader shot.
 *
 * Compilation: gcc -std=c11 -Wall -O2 -o space_invaders games/invaders.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <string.h>
#include <time.h>

#define BOARD_WIDTH 40
#define BOARD_HEIGHT 20

#define INV_ROWS 3
#define INV_COLS 8

/* Function to sleep for a given number of milliseconds */
void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/* Global terminal settings backup */
static struct termios orig_termios;

/* Restore original terminal settings on exit */
void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
}

/* Set terminal to raw mode for nonblocking input */
void set_conio_terminal_mode() {
    struct termios new_termios;
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));
    new_termios.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_termios);
    atexit(reset_terminal_mode);
}

/* Check if a key has been pressed (nonblocking) */
int kbhit() {
    struct timeval tv = {0, 0};
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    return select(1, &readfds, NULL, NULL, &tv) > 0;
}

/* Get one character from input */
int getch() {
    int r;
    unsigned char c;
    if ((r = read(0, &c, sizeof(c))) < 0)
        return r;
    else
        return c;
}

/* Structure for the player's bullet */
typedef struct {
    int active;
    int x, y;
} Bullet;

/* Global game state variables */
int player_x;
Bullet bullet;
int invaders[INV_ROWS][INV_COLS]; // 1 = alive, 0 = dead
int invader_offset_x, invader_offset_y;
int invader_dir; // 1 = moving right, -1 = moving left
int frame_count = 0;
int game_over = 0;
int game_win = 0;
int score = 0;  // Global score variable

/* Initialize game state */
void init_game() {
    int i, j;
    player_x = BOARD_WIDTH / 2;
    bullet.active = 0;
    score = 0;
    // Initialize all invaders to alive
    for (i = 0; i < INV_ROWS; i++) {
        for (j = 0; j < INV_COLS; j++) {
            invaders[i][j] = 1;
        }
    }
    // Starting position for the invader group
    invader_offset_x = 3;
    invader_offset_y = 1;
    invader_dir = 1;
}

/* Process input: arrow keys and space */
void process_input() {
    while (kbhit()) {
        int c = getch();
        if (c == 27) { // possible escape sequence for arrow keys
            if (kbhit() && getch() == 91) {
                int dir = getch();
                if (dir == 68) { // Left arrow
                    if (player_x > 0)
                        player_x--;
                } else if (dir == 67) { // Right arrow
                    if (player_x < BOARD_WIDTH - 1)
                        player_x++;
                }
            }
        } else if (c == ' ') {
            // Fire bullet if none is active
            if (!bullet.active) {
                bullet.active = 1;
                bullet.x = player_x;
                bullet.y = BOARD_HEIGHT - 2;
            }
        }
    }
}

/* Update bullet position and check for collision with invaders */
void update_bullet() {
    if (bullet.active) {
        bullet.y--; // move bullet up
        if (bullet.y < 0) {
            bullet.active = 0;
            return;
        }
        // Check collision with any invader
        for (int i = 0; i < INV_ROWS; i++) {
            for (int j = 0; j < INV_COLS; j++) {
                if (invaders[i][j]) {
                    int inv_x = invader_offset_x + j * 4;
                    int inv_y = invader_offset_y + i * 2;
                    if (bullet.x == inv_x && bullet.y == inv_y) {
                        invaders[i][j] = 0; // invader hit
                        bullet.active = 0;
                        score += 10;  // Increase score
                    }
                }
            }
        }
    }
}

/* Update invader positions every few frames */
void update_invaders() {
    // Update invaders every 5 frames
    if (frame_count % 5 != 0)
        return;

    int leftmost = BOARD_WIDTH, rightmost = 0;
    int any_alive = 0;
    // Find horizontal boundaries of alive invaders
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (invaders[i][j]) {
                any_alive = 1;
                int inv_x = invader_offset_x + j * 4;
                if (inv_x < leftmost)
                    leftmost = inv_x;
                if (inv_x > rightmost)
                    rightmost = inv_x;
            }
        }
    }
    // If no invaders are alive, set win flag
    if (!any_alive) {
        game_win = 1;
        return;
    }
    // Check if the group hits the edge
    if ((invader_dir == 1 && rightmost + 1 >= BOARD_WIDTH) ||
        (invader_dir == -1 && leftmost - 1 < 0)) {
        invader_offset_y++; // drop down
        invader_dir *= -1;  // reverse direction
    } else {
        invader_offset_x += invader_dir;
    }
    // Check if invaders have reached the player's row
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (invaders[i][j]) {
                int inv_y = invader_offset_y + i * 2;
                if (inv_y >= BOARD_HEIGHT - 1) {
                    game_over = 1;
                }
            }
        }
    }
}

/* Update game state: bullet and invaders */
void update_game() {
    update_bullet();
    update_invaders();
}

/* Render the game board with borders and a SCORE field */
void draw_game() {
    char board[BOARD_HEIGHT][BOARD_WIDTH + 1];
    // Initialize board with spaces
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        for (int j = 0; j < BOARD_WIDTH; j++) {
            board[i][j] = ' ';
        }
        board[i][BOARD_WIDTH] = '\0';
    }
    // Draw invaders
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (invaders[i][j]) {
                int x = invader_offset_x + j * 4;
                int y = invader_offset_y + i * 2;
                if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT)
                    board[y][x] = 'W';
            }
        }
    }
    // Draw bullet
    if (bullet.active) {
        if (bullet.x >= 0 && bullet.x < BOARD_WIDTH && bullet.y >= 0 && bullet.y < BOARD_HEIGHT)
            board[bullet.y][bullet.x] = '|';
    }
    // Draw player (ship) with 'A'
    if (player_x >= 0 && player_x < BOARD_WIDTH)
        board[BOARD_HEIGHT - 1][player_x] = 'A';
    
    // Clear screen and move cursor to top-left
    printf("\033[H\033[J");
    // Print SCORE field
    printf("SCORE: %d\n", score);
    
    // Draw top border
    printf("+");
    for (int j = 0; j < BOARD_WIDTH; j++) {
        printf("-");
    }
    printf("+\n");
    
    // Print board with vertical borders
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        printf("|%s|\n", board[i]);
    }
    
    // Draw bottom border
    printf("+");
    for (int j = 0; j < BOARD_WIDTH; j++) {
        printf("-");
    }
    printf("+\n");
    
    // Print game status messages if game over or win
    if (game_over) {
        printf("\nGame Over! Invaders reached your ship.\n");
    }
    if (game_win) {
        printf("\nYou Win! All invaders eliminated.\n");
    }
}

/* Main game loop */
int main(void) {
    set_conio_terminal_mode();
    init_game();
    while (1) {
        if (game_over || game_win)
            break;
        process_input();
        update_game();
        draw_game();
        frame_count++;
        sleep_ms(100); // Sleep 100ms => ~10fps
    }
    // Final draw to show end message
    draw_game();
    return 0;
}
