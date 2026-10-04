#define _POSIX_C_SOURCE 200809L  // Must be the very first line to expose POSIX APIs

/*
 * Space Invaders Clone for Linux Terminal
 *
 * Design Notes:
 * - The game board is a 40x20 grid drawn with borders using ANSI escape codes.
 * - The player's ship is represented by 'A' at the bottom row.
 * - A single bullet (represented by '|') is allowed at a time.
 * - Invaders (represented by 'W') are arranged in a grid (4 rows x 12 columns)
 *   with a fixed horizontal spacing. They move as a group.
 * - Invader group movement: Every 5 frames, the group moves one step horizontally.
 *   If any invader would hit the board edge, the group drops one row and reverses direction.
 * - Input is handled in raw mode with non-blocking reads (using termios and select).
 * - The game updates at ~10fps (100ms per frame).
 * - A global score increases by 10 for each invader shot.
 * - The blinking cursor is hidden during the game.
 * - Press "Q" to quit and "R" to restart the game.
 * - Key instructions are displayed below the game area.
 *
 * Modifications:
 * - When game over (or win) occurs, all movement stops and only 'r' (restart) and 'q' (quit) keys work.
 * - Collision detection for the bullet is done before moving the bullet so that invaders near the bottom can be hit.
 *
 * Compilation: gcc -std=c11 -Wall -O2 -o space_invaders invaders.c
 */

#include "../lib/terminal_layout.h"
#include "../lib/terminal_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define BOARD_WIDTH 40
#define BOARD_HEIGHT 20

#define INV_ROWS 4
#define INV_COLS 12

#define INV_SPACING_X 3
#define INV_SPACING_Y 2

/* Function to sleep for a given number of milliseconds */
void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

static int quit_requested;

static void reset_terminal_mode(void) {
    budostack_terminal_input_stop();
}

static void set_conio_terminal_mode(void) {
    if (budostack_terminal_input_start() != 0) {
        exit(EXIT_FAILURE);
    }
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
unsigned int frame_count = 0;
int game_over = 0;
int game_win = 0;
int score = 0;  // Global score variable

/* Initialize game state */
void init_game(void) {
    int i, j;
    player_x = BOARD_WIDTH / 2;
    bullet.active = 0;
    score = 0;
    game_over = 0;
    game_win = 0;
    frame_count = 0;
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

/* Process input: arrow keys, space, Q to quit, and R to restart.
 * When game over or win, only R and Q are processed.
 */
void process_input(void) {
    int c;
    while ((c = budostack_terminal_read_key(0)) != BUDOSTACK_KEY_NONE) {
        if (c == BUDOSTACK_KEY_EOF || c == 'q' || c == 'Q') {
            quit_requested = 1;
            return;
        }
        if (c == 'r' || c == 'R') {
            init_game();
            return;
        }
        if (game_over || game_win) {
            continue;
        }
        if ((c == BUDOSTACK_KEY_LEFT || c == 'a' || c == 'A') && player_x > 0) {
            player_x--;
        } else if ((c == BUDOSTACK_KEY_RIGHT || c == 'd' || c == 'D') && player_x < BOARD_WIDTH - 1) {
            player_x++;
        } else if (c == ' ' && !bullet.active) {
            bullet.active = 1;
            bullet.x = player_x;
            bullet.y = BOARD_HEIGHT - 2;
        }
    }
}

/* Update bullet position and check for collision with invaders.
 * Collision is checked at the bullet's current position before moving it.
 */
static int hit_invader(void) {
    if (!bullet.active) {
        return 0;
    }
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (invaders[i][j] && bullet.x == invader_offset_x + j * INV_SPACING_X &&
                bullet.y == invader_offset_y + i * INV_SPACING_Y) {
                invaders[i][j] = 0;
                bullet.active = 0;
                score += 10;
                game_win = 1;
                for (int row = 0; row < INV_ROWS; row++) {
                    for (int col = 0; col < INV_COLS; col++) {
                        if (invaders[row][col]) {
                            game_win = 0;
                        }
                    }
                }
                return 1;
            }
        }
    }
    return 0;
}

void update_bullet(void) {
    if (!bullet.active || hit_invader()) {
        return;
    }
    bullet.y--;
    if (bullet.y < 0) {
        bullet.active = 0;
    } else {
        (void)hit_invader();
    }
}

/* Update invader positions every few frames */
void update_invaders(void) {
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
                int inv_x = invader_offset_x + j * INV_SPACING_X;
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
                int inv_y = invader_offset_y + i * INV_SPACING_Y;
                if (inv_y >= BOARD_HEIGHT - 1) {
                    game_over = 1;
                }
            }
        }
    }
}

/* Update game state: bullet and invaders.
 * No movement is performed if game is over or won.
 */
void update_game(void) {
    if (game_over || game_win || budostack_get_target_cols() < BOARD_WIDTH ||
        budostack_get_target_rows() < BOARD_HEIGHT + 6) {
        return;
    }
    update_bullet();
    if (!game_win) {
        update_invaders();
        /* Check the new bullet/formation positions without advancing twice. */
        (void)hit_invader();
        if (game_win) {
            game_over = 0;
        }
    }
}

/* Render the game board with borders and a SCORE field */
void draw_game(void) {
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
                int x = invader_offset_x + j * INV_SPACING_X;
                int y = invader_offset_y + i * INV_SPACING_Y;
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
    
    char cells[BOARD_HEIGHT][BOARD_WIDTH];
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        memcpy(cells[y], board[y], BOARD_WIDTH);
    }
    char status[100];
    snprintf(status, sizeof(status), "INVADERS  Score: %d%s", score,
             game_over ? "  Game Over!" : game_win ? "  You Win!" : "");
    budostack_draw_terminal_grid(&cells[0][0], BOARD_WIDTH, BOARD_HEIGHT, status,
                                 "Arrows move Space fire R restart Q quit");
}

/* Main game loop */
int main(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    set_conio_terminal_mode();
    init_game();
    while (!quit_requested) {
        process_input();
        if (quit_requested) {
            break;
        }
        update_game();
        draw_game();
        frame_count++;
        sleep_ms(100); // Sleep 100ms => ~10fps
    }
    reset_terminal_mode();
    return 0;
}
