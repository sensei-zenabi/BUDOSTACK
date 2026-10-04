#define _XOPEN_SOURCE 600  // Feature test macro to expose usleep
#include "../lib/terminal_layout.h"
#include "../lib/terminal_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Board dimensions. Match width and height so the playfield remains square when
// rendered with the 8x8 terminal font.
#define WIDTH 20
#define HEIGHT 20
// Maximum snake length
#define MAX_SNAKE_LENGTH 100

// Minimum delay (in microseconds) to ensure game remains playable
#define MIN_DELAY 30000

// Global delay variable (in microseconds) controlling game speed
unsigned int delay_time = 100000;

// Enum for snake movement directions
enum Direction { UP, DOWN, LEFT, RIGHT };

// Structure to represent a point on the board
typedef struct {
    int x;
    int y;
} Point;

// Global snake array, its current length, and direction
Point snake[MAX_SNAKE_LENGTH];
int snake_length = 3;
enum Direction dir = RIGHT;

// Global fruit position
Point fruit;

// Flag to indicate game over state
int game_over = 0;

static int quit_requested;

static void disableRawMode(void) {
    budostack_terminal_input_stop();
}

static void enableRawMode(void) {
    if (budostack_terminal_input_start() != 0) {
        exit(EXIT_FAILURE);
    }
}

static void placeFruit(void) {
    Point empty[WIDTH * HEIGHT];
    int count = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int occupied = 0;
            for (int k = 0; k < snake_length; k++) {
                if (snake[k].x == x && snake[k].y == y) {
                    occupied = 1;
                    break;
                }
            }
            if (!occupied) {
                empty[count++] = (Point){x, y};
            }
        }
    }
    if (count == 0) {
        game_over = 1;
        return;
    }
    fruit = empty[rand() % count];
}

// Initialize or restart the game: reset snake and fruit positions, direction, game_over flag, and delay
void initGame(void) {
    // Reset snake length, direction, and delay
    snake_length = 3;
    dir = RIGHT;
    delay_time = 100000;
    // Start snake at center going right
    snake[0].x = WIDTH / 2;
    snake[0].y = HEIGHT / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;
    
    // Place fruit outside the snake
    placeFruit();
    
    game_over = 0;
}

void updateDirection(void) {
    int c = budostack_terminal_read_key(0);
    if (c == BUDOSTACK_KEY_EOF || c == 'q' || c == 'Q') {
        quit_requested = 1;
    } else if (c == 'r' || c == 'R') {
        initGame();
    } else if (!game_over) {
        if ((c == BUDOSTACK_KEY_UP || c == 'w' || c == 'W') && dir != DOWN) {
            dir = UP;
        } else if ((c == BUDOSTACK_KEY_DOWN || c == 's' || c == 'S') && dir != UP) {
            dir = DOWN;
        } else if ((c == BUDOSTACK_KEY_LEFT || c == 'a' || c == 'A') && dir != RIGHT) {
            dir = LEFT;
        } else if ((c == BUDOSTACK_KEY_RIGHT || c == 'd' || c == 'D') && dir != LEFT) {
            dir = RIGHT;
        }
    }
}

// Update the snake position based on the current direction and check for collisions.
// Instead of exiting on collision, set game_over to 1.
void updateSnake(void) {
    // Calculate new head position
    Point new_head = snake[0];
    switch(dir) {
        case UP:    new_head.y--; break;
        case DOWN:  new_head.y++; break;
        case LEFT:  new_head.x--; break;
        case RIGHT: new_head.x++; break;
    }
    // Check collision with walls
    if(new_head.x < 0 || new_head.x >= WIDTH || new_head.y < 0 || new_head.y >= HEIGHT) {
        game_over = 1;
        return;
    }
    // Check collision with itself
    int growing = new_head.x == fruit.x && new_head.y == fruit.y;
    for (int i = 0; i < snake_length - (growing ? 0 : 1); i++) {
        if(snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = 1;
            return;
        }
    }
    // Move snake segments: shift each segment to the position of the previous one
    for (int i = snake_length < MAX_SNAKE_LENGTH ? snake_length : MAX_SNAKE_LENGTH - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = new_head;
    
    // Check if fruit is eaten
    if(new_head.x == fruit.x && new_head.y == fruit.y) {
        snake_length++;
        if(snake_length >= MAX_SNAKE_LENGTH)
            snake_length = MAX_SNAKE_LENGTH;
        // Calculate score (number of apples eaten)
        int score = snake_length - 3;
        // Speed up after every 5 apples, reducing delay by 10000 microseconds until a minimum delay is reached
        if(score % 5 == 0 && delay_time > MIN_DELAY) {
            delay_time -= 10000;
        }
        if (snake_length == MAX_SNAKE_LENGTH) {
            game_over = 1;
        } else {
            placeFruit();
        }
    }
}

// Draw the game board using line-drawing characters for the borders,
// and display the snake, fruit, score, and instructions.
void drawBoard(void) {
    uint32_t board[HEIGHT][WIDTH];
    char status[80];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            board[y][x] = ' ';
        }
    }
    board[fruit.y][fruit.x] = '*';
    for (int k = snake_length - 1; k >= 0; k--) {
        board[snake[k].y][snake[k].x] = k == 0 ? 0x2588u : 0x2593u;
    }
    snprintf(status, sizeof(status), "SNAKE  Score: %d%s", snake_length - 3,
             game_over ? (snake_length == MAX_SNAKE_LENGTH ? "  You Win!" : "  Game Over!") : "");
    budostack_draw_terminal_grid(&board[0][0], WIDTH, HEIGHT, status,
                                 "WASD/Arrows R restart Q quit");
}

// Main game loop: if game_over is set, wait for 'r' (restart) or 'q' (quit)
int main(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    enableRawMode();
    srand((unsigned int)time(NULL));
    initGame();
    while (!quit_requested) {
        updateDirection();
        if (quit_requested) {
            break;
        }
        if (!game_over && budostack_get_target_cols() >= WIDTH + 2 &&
            budostack_get_target_rows() >= HEIGHT + 6) {
            updateSnake();
        }
        drawBoard();
        usleep(delay_time);
    }
    disableRawMode();

    return 0;
}
