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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>

void* _draw_create(int width, int height);
void  _draw_destroy(void* ctx);
void  _draw_clear(void* ctx, int value);
void  _draw_rect(void* ctx, int x, int y, int w, int h, int value);
void  _draw_fill_rect(void* ctx, int x, int y, int w, int h, int value);
void  _draw_render_to_stdout(void* ctx);

#define BOARD_WIDTH 80
#define BOARD_HEIGHT 20

#define INV_ROWS 4
#define INV_COLS 12

#define CELL_PIX_W 2
#define CELL_PIX_H 4

#define SCORE_FILE "games/invaders_scores.txt"
#define MAX_NAME_LEN 32
#define MAX_SCORES 100

typedef struct {
    char name[MAX_NAME_LEN];
    int score;
} ScoreEntry;

void* draw_ctx = NULL;
int scale = 1;
int cell_w = CELL_PIX_W;
int cell_h = CELL_PIX_H;
int leaderboard_shown = 0;
int running = 1;

int determineScale() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return 1;
    int avail_w = ws.ws_col * 2;
    int avail_h = (ws.ws_row - 3) * 4;
    int max_scale_w = avail_w / (BOARD_WIDTH * CELL_PIX_W);
    int max_scale_h = avail_h / (BOARD_HEIGHT * CELL_PIX_H);
    int s = max_scale_w < max_scale_h ? max_scale_w : max_scale_h;
    if (s < 1)
        s = 1;
    return s;
}

/* Function to sleep for a given number of milliseconds */
void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/* Global terminal settings backup */
static struct termios orig_termios;

/* Restore original terminal settings on exit and show the cursor */
void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
    // Show the cursor when exiting
    printf("\033[?25h");
}

/* Set terminal to raw mode for nonblocking input and hide the cursor */
void set_conio_terminal_mode() {
    struct termios new_termios;
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));
    new_termios.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_termios);
    // Hide the cursor during the game
    printf("\033[?25l");
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
    game_over = 0;
    game_win = 0;
    frame_count = 0;
    leaderboard_shown = 0;
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
void process_input() {
    while (kbhit()) {
        int c = getch();
        // When game over or win, restrict input to 'r' (restart) and 'q' (quit)
        if (game_over || game_win) {
            if (c == 'q' || c == 'Q') {
                running = 0;
                return;
            }
            if (c == 'r' || c == 'R') {
                init_game();
                return;
            }
            // Ignore any other key
            continue;
        }
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
        } else if (c == 'q' || c == 'Q') {
            running = 0;
        } else if (c == 'r' || c == 'R') {
            init_game();
        }
    }
}

/* Update bullet position and check for collision with invaders.
 * Collision is checked at the bullet's current position before moving it.
 */
void update_bullet() {
    if (bullet.active) {
        // Check collision at the bullet's current position
        for (int i = 0; i < INV_ROWS; i++) {
            for (int j = 0; j < INV_COLS; j++) {
                if (invaders[i][j]) {
                    int inv_x = invader_offset_x + j * 4;
                    int inv_y = invader_offset_y + i * 2;
                    if (bullet.x == inv_x && bullet.y == inv_y) {
                        invaders[i][j] = 0; // invader hit
                        bullet.active = 0;
                        score += 10;  // Increase score
                        return;
                    }
                }
            }
        }
        // Move bullet up after collision check
        bullet.y--;
        if (bullet.y < 0) {
            bullet.active = 0;
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

/* Update game state: bullet and invaders.
 * No movement is performed if game is over or won.
 */
void update_game() {
    if (game_over || game_win)
        return;
    update_bullet();
    update_invaders();
}

void recordScore(int s) {
    char name[MAX_NAME_LEN];
    reset_terminal_mode();
    printf("Enter name: ");
    fflush(stdout);
    if (fgets(name, sizeof(name), stdin) == NULL)
        strcpy(name, "anon");
    name[strcspn(name, "\n")] = '\0';
    set_conio_terminal_mode();
    FILE* f = fopen(SCORE_FILE, "a");
    if (f) {
        fprintf(f, "%s %d\n", name, s);
        fclose(f);
    }
}

void showScores() {
    FILE* f = fopen(SCORE_FILE, "r");
    if (!f) {
        printf("No scores yet.\n");
        return;
    }
    ScoreEntry entries[MAX_SCORES];
    int count = 0;
    while (count < MAX_SCORES && fscanf(f, "%31s %d", entries[count].name, &entries[count].score) == 2) {
        count++;
    }
    fclose(f);
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (entries[j].score > entries[i].score) {
                ScoreEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    printf("Leaderboard:\n");
    for (int i = 0; i < count; i++) {
        printf("%d. %s - %d\n", i + 1, entries[i].name, entries[i].score);
    }
}

/* Render the game board with borders and a SCORE field */
void draw_game() {
    int board_w = BOARD_WIDTH * cell_w;
    int board_h = BOARD_HEIGHT * cell_h;

    printf("\033[H\033[J");
    _draw_clear(draw_ctx, 0);
    _draw_rect(draw_ctx, 0, 0, board_w, board_h, 1);

    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (invaders[i][j]) {
                int x = (invader_offset_x + j * 4) * cell_w;
                int y = (invader_offset_y + i * 2) * cell_h;
                _draw_fill_rect(draw_ctx, x, y, cell_w, cell_h, 1);
            }
        }
    }

    if (bullet.active) {
        _draw_fill_rect(draw_ctx, bullet.x * cell_w, bullet.y * cell_h, cell_w, cell_h, 1);
    }

    _draw_fill_rect(draw_ctx, player_x * cell_w, (BOARD_HEIGHT - 1) * cell_h, cell_w, cell_h, 1);

    _draw_render_to_stdout(draw_ctx);

    printf("SCORE: %d\n", score);
    if (game_over)
        printf("\nGame Over! Invaders reached your ship.\n");
    if (game_win)
        printf("\nYou Win! All invaders eliminated.\n");
    printf("Controls: Arrow keys to move, Space to fire, Q to quit, R to restart\n");
}

/* Main game loop */
int main(void) {
    set_conio_terminal_mode();
    init_game();
    scale = determineScale();
    cell_w = CELL_PIX_W * scale;
    cell_h = CELL_PIX_H * scale;
    draw_ctx = _draw_create(BOARD_WIDTH * cell_w, BOARD_HEIGHT * cell_h);
    while (running) {
        process_input();
        update_game();
        draw_game();
        if (game_over || game_win) {
            if (!leaderboard_shown) {
                recordScore(score);
                leaderboard_shown = 1;
            }
            showScores();
        }
        frame_count++;
        sleep_ms(100); // Sleep 100ms => ~10fps
    }
    _draw_destroy(draw_ctx);
    reset_terminal_mode();
    return 0;
}
