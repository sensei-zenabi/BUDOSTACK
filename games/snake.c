#define _XOPEN_SOURCE 600  // Feature test macro to expose usleep
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>

// Board dimensions tuned for an 80x42 terminal window
#define WIDTH 78
#define HEIGHT 40
// Maximum snake length equals total board cells
#define MAX_SNAKE_LENGTH (WIDTH * HEIGHT)

// Delay tuning (in microseconds) to control speed progression
#define INITIAL_DELAY 10000
#define MIN_DELAY 10000
#define SPEED_STEP 2500

// Global delay variable (in microseconds) controlling game speed
unsigned int delay_time = INITIAL_DELAY;

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

static void drawStaticBoard(void);
static void drawCell(int x, int y, const char *symbol);
static void drawStatus(void);

// Flag to indicate game over state
int game_over = 0;

// Terminal settings storage so we can restore them
struct termios orig_termios;

// Function to restore the original terminal settings
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Function to set terminal to raw mode for non-blocking key input
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode); // restore settings on exit
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); // disable echo and canonical mode
    raw.c_cc[VMIN] = 0;  // no minimum characters for non-blocking input
    raw.c_cc[VTIME] = 1; // timeout (0.1 seconds) for read
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Initialize or restart the game: reset snake and fruit positions, direction, game_over flag, and delay
void initGame() {
    // Reset snake length, direction, and delay
    snake_length = 3;
    dir = RIGHT;
    delay_time = INITIAL_DELAY;
    // Start snake at center going right
    snake[0].x = WIDTH / 2;
    snake[0].y = HEIGHT / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;
    
    // Seed random and place the first fruit
    srand(time(NULL));
    fruit.x = rand() % WIDTH;
    fruit.y = rand() % HEIGHT;

    game_over = 0;
}

// Read a single character from input (non-blocking)
char getInput() {
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    if(n == 1)
        return c;
    return 0;
}

// Update the snake's movement direction based on user input.
// Supports arrow keys and WASD controls. 'q' quits and 'r' restarts.
int updateDirection() {
    char c = getInput();
    if(c == 0)
        return 0;
    // Check for escape sequence (arrow keys)
    if(c == '\033') {
        char seq[2];
        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return 0;
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return 0;
        if(seq[0] == '[') {
            if(seq[1] == 'A' && dir != DOWN)       // Up arrow
                dir = UP;
            else if(seq[1] == 'B' && dir != UP)      // Down arrow
                dir = DOWN;
            else if(seq[1] == 'C' && dir != LEFT)    // Right arrow
                dir = RIGHT;
            else if(seq[1] == 'D' && dir != RIGHT)   // Left arrow
                dir = LEFT;
        }
    } else {
        // WASD controls, plus r to restart and q to quit
        if((c == 'w' || c == 'W') && dir != DOWN)
            dir = UP;
        else if((c == 's' || c == 'S') && dir != UP)
            dir = DOWN;
        else if((c == 'a' || c == 'A') && dir != RIGHT)
            dir = LEFT;
        else if((c == 'd' || c == 'D') && dir != LEFT)
            dir = RIGHT;
        else if(c == 'q' || c == 'Q')
            exit(0);
        else if(c == 'r' || c == 'R') {
            initGame();
            return 1;
        }
    }
    return 0;
}

// Update the snake position based on the current direction and check for collisions.
// Instead of exiting on collision, set game_over to 1.
typedef struct {
    Point previous_head;
    Point tail;
    int removed_tail;
    int ate_fruit;
} MoveResult;

MoveResult updateSnake() {
    MoveResult result = {0};
    // Calculate new head position
    Point old_head = snake[0];
    Point old_tail = snake[snake_length - 1];
    Point new_head = old_head;
    switch(dir) {
        case UP:    new_head.y--; break;
        case DOWN:  new_head.y++; break;
        case LEFT:  new_head.x--; break;
        case RIGHT: new_head.x++; break;
    }
    // Check collision with walls
    if(new_head.x < 0 || new_head.x >= WIDTH || new_head.y < 0 || new_head.y >= HEIGHT) {
        game_over = 1;
        return result;
    }
    // Check collision with itself
    for (int i = 0; i < snake_length; i++) {
        if(snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = 1;
            return result;
        }
    }
    // Check if fruit is eaten
    int ate = (new_head.x == fruit.x && new_head.y == fruit.y);
    int new_length = snake_length + (ate ? 1 : 0);
    if(new_length > MAX_SNAKE_LENGTH)
        new_length = MAX_SNAKE_LENGTH;

    // Move snake segments: shift each segment to the position of the previous one
    for (int i = new_length - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake_length = new_length;
    snake[0] = new_head;

    result.previous_head = old_head;

    if(ate) {
        result.ate_fruit = 1;
        // Calculate score (number of apples eaten)
        int score = snake_length - 3;
        // Progressively speed up with each apple until reaching the minimum delay
        if(delay_time > MIN_DELAY) {
            int new_delay = INITIAL_DELAY - (score * SPEED_STEP);
            if(new_delay < (int)MIN_DELAY)
                new_delay = MIN_DELAY;
            delay_time = (unsigned int)new_delay;
        }
        // Place new fruit ensuring it does not appear on the snake
        int valid = 0;
        while(!valid) {
            fruit.x = rand() % WIDTH;
            fruit.y = rand() % HEIGHT;
            valid = 1;
            for (int i = 0; i < snake_length; i++) {
                if(snake[i].x == fruit.x && snake[i].y == fruit.y) {
                    valid = 0;
                    break;
                }
            }
        }
    } else {
        result.tail = old_tail;
        result.removed_tail = 1;
    }

    return result;
}

static void moveCursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

static void drawCell(int x, int y, const char *symbol) {
    moveCursor(y + 2, x + 2); // offset for borders
    printf("%s", symbol);
}

static void drawStaticBoard(void) {
    printf("\033[2J\033[H");

    // Top border
    printf("┌");
    for (int x = 0; x < WIDTH; x++)
        printf("─");
    printf("┐\n");

    // Side borders
    for (int y = 0; y < HEIGHT; y++) {
        printf("│\033[%d;%dH│\n", y + 2, WIDTH + 2);
    }

    // Bottom border
    printf("└");
    for (int x = 0; x < WIDTH; x++)
        printf("─");
    printf("┘\n");
}

static void drawStatus(void) {
    moveCursor(HEIGHT + 3, 1);
    printf("\033[KScore: %d\n", snake_length - 3);
    if(game_over)
        printf("\033[KGame Over!\n");
    else
        printf("\033[K                \n");
    printf("\033[KPress 'r' to restart, 'q' to quit.\n");
}

static void drawFullBoard(void) {
    drawStaticBoard();
    for (int i = 0; i < snake_length; i++) {
        const char *symbol = (i == 0) ? "\u2588" : "\u2593";
        drawCell(snake[i].x, snake[i].y, symbol);
    }
    drawCell(fruit.x, fruit.y, "*");
    drawStatus();
    fflush(stdout);
}

// Main game loop: if game_over is set, wait for 'r' (restart) or 'q' (quit)
int main() {
    enableRawMode();
    initGame();
    drawFullBoard();

    while(1) {
        if(!game_over) {
            int restarted = updateDirection();  // process user input
            if(restarted) {
                drawFullBoard();
                continue;
            }
            MoveResult move = updateSnake();      // update snake's position and check collisions
            if(!game_over) {
                drawCell(move.previous_head.x, move.previous_head.y, "\u2593");
                if(move.removed_tail)
                    drawCell(move.tail.x, move.tail.y, " ");
                drawCell(snake[0].x, snake[0].y, "\u2588");
                if(move.ate_fruit)
                    drawCell(fruit.x, fruit.y, "*");
            }
        } else {
            char c = getInput();
            if(c == 'r' || c == 'R') {
                initGame();
                drawFullBoard();
                continue;
            } else if(c == 'q' || c == 'Q') {
                break;
            }
        }

        drawStatus();
        fflush(stdout);

        usleep(delay_time); // delay to control game speed
    }
    
    return 0;
}
